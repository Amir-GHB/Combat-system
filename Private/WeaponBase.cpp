// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "WeaponBase.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "DamageHandlerComponent.h"

AWeaponBase::AWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false; // Tracing updates are driven by animation notifies

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	RootComponent = RootSceneComponent;

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	WeaponMesh->SetGenerateOverlapEvents(false);
}

void AWeaponBase::BeginPlay()
{
	Super::BeginPlay();
	OwningCharacter = Cast<ACharacter>(GetOwner());
}

void AWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelTrace();
	Super::EndPlay(EndPlayReason);
}

void AWeaponBase::AttachToHand(ACharacter* TargetCharacter)
{
	if (!TargetCharacter || !TargetCharacter->GetMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AWeaponBase::AttachToHand] TargetCharacter or Mesh is null!"));
		return;
	}

	OwningCharacter = TargetCharacter;
	SetOwner(TargetCharacter);

	AttachToComponent(
		TargetCharacter->GetMesh(),
		FAttachmentTransformRules::SnapToTargetIncludingScale,
		WeaponConfig.HandSocketName
	);

	SetWeaponHidden(false);
}

void AWeaponBase::AttachToHolster(ACharacter* TargetCharacter)
{
	if (!TargetCharacter || !TargetCharacter->GetMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AWeaponBase::AttachToHolster] TargetCharacter or Mesh is null!"));
		return;
	}

	OwningCharacter = TargetCharacter;
	SetOwner(TargetCharacter);

	AttachToComponent(
		TargetCharacter->GetMesh(),
		FAttachmentTransformRules::SnapToTargetIncludingScale,
		WeaponConfig.HolsterSocketName
	);

	SetWeaponHidden(false);
}

void AWeaponBase::SetWeaponHidden(bool bHidden)
{
	SetActorHiddenInGame(bHidden);
}

void AWeaponBase::SampleSocketLocations(TMap<FName, FVector>& OutLocations) const
{
	OutLocations.Empty(WeaponConfig.TraceSocketNames.Num());

	if (!WeaponMesh) return;

	for (const FName& SocketName : WeaponConfig.TraceSocketNames)
	{
		OutLocations.Add(SocketName, WeaponMesh->GetSocketLocation(SocketName));
	}
}

void AWeaponBase::BeginTrace()
{
	bIsTracing = true;
	DamagedActorsThisSwing.Empty();
	SampleSocketLocations(PreviousSocketLocations);
}

void AWeaponBase::TickTrace()
{
	if (!bIsTracing || !WeaponMesh) return;

	UWorld* World = GetWorld();
	if (!World) return;

	TMap<FName, FVector> CurrentSocketLocations;
	SampleSocketLocations(CurrentSocketLocations);

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	if (OwningCharacter.IsValid())
	{
		ActorsToIgnore.Add(OwningCharacter.Get());
	}

	FCollisionShape SphereShape = FCollisionShape::MakeSphere(WeaponConfig.TraceRadius);
	FCollisionQueryParams QueryParams(TEXT("MeleeWeaponTrace"), false, this);
	QueryParams.AddIgnoredActors(ActorsToIgnore);

	TArray<FHitResult> AllHitResults;

	// 1. Continuous Sweeps: Sweep each socket from PreviousLocation to CurrentLocation
	for (const auto& Pair : CurrentSocketLocations)
	{
		const FName& SocketName = Pair.Key;
		const FVector& CurrentPos = Pair.Value;
		const FVector* PrevPosPtr = PreviousSocketLocations.Find(SocketName);
		const FVector PrevPos = PrevPosPtr ? *PrevPosPtr : CurrentPos;

		TArray<FHitResult> SocketHits;
		World->SweepMultiByChannel(
			SocketHits,
			PrevPos,
			CurrentPos,
			FQuat::Identity,
			WeaponConfig.TraceCollisionChannel,
			SphereShape,
			QueryParams
		);

		AllHitResults.Append(SocketHits);

		if (WeaponConfig.bDrawDebugTraces)
		{
			DrawDebugCapsule(
				World,
				(PrevPos + CurrentPos) * 0.5f,
				(PrevPos - CurrentPos).Size() * 0.5f + WeaponConfig.TraceRadius,
				WeaponConfig.TraceRadius,
				FRotationMatrix::MakeFromZ(CurrentPos - PrevPos).ToQuat(),
				FColor::Green,
				false,
				0.4f
			);
		}
	}

	// 2. Segment Sweeps: Sweep between adjacent sockets along weapon length
	if (WeaponConfig.TraceSocketNames.Num() >= 2)
	{
		for (int32 i = 0; i < WeaponConfig.TraceSocketNames.Num() - 1; ++i)
		{
			const FName& S1 = WeaponConfig.TraceSocketNames[i];
			const FName& S2 = WeaponConfig.TraceSocketNames[i + 1];

			const FVector* P1 = CurrentSocketLocations.Find(S1);
			const FVector* P2 = CurrentSocketLocations.Find(S2);

			if (P1 && P2)
			{
				TArray<FHitResult> SegmentHits;
				World->SweepMultiByChannel(
					SegmentHits,
					*P1,
					*P2,
					FQuat::Identity,
					WeaponConfig.TraceCollisionChannel,
					SphereShape,
					QueryParams
				);
				AllHitResults.Append(SegmentHits);
			}
		}
	}

	// 3. Deduplicate and validate hits
	for (const FHitResult& Hit : AllHitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (CanDamageActor(HitActor) && !DamagedActorsThisSwing.Contains(HitActor))
		{
			DamagedActorsThisSwing.Add(HitActor);
			ProcessHit(Hit);
		}
	}

	PreviousSocketLocations = CurrentSocketLocations;
}

void AWeaponBase::EndTrace()
{
	bIsTracing = false;
	DamagedActorsThisSwing.Empty();
	PreviousSocketLocations.Empty();
}

void AWeaponBase::CancelTrace()
{
	EndTrace();
}

bool AWeaponBase::CanDamageActor(const AActor* TargetActor) const
{
	if (!TargetActor || TargetActor->IsPendingKillPending())
	{
		return false;
	}

	if (TargetActor == this || (OwningCharacter.IsValid() && TargetActor == OwningCharacter.Get()))
	{
		return false;
	}

	return true;
}

void AWeaponBase::ProcessHit(const FHitResult& HitResult)
{
	AActor* TargetActor = HitResult.GetActor();
	if (!TargetActor || !OwningCharacter.IsValid()) return;

	// Build structured damage payload from data-driven configuration
	FDamageInfo DamageInfo;
	DamageInfo.BaseDamage = WeaponConfig.BaseDamage;
	DamageInfo.PoiseDamage = WeaponConfig.PoiseDamage;
	DamageInfo.KnockbackImpulse = WeaponConfig.KnockbackImpulse;
	DamageInfo.DamageCategory = WeaponConfig.DamageCategory;
	DamageInfo.WeaponType = WeaponConfig.WeaponType;
	DamageInfo.DamageMultiplier = 1.0f;
	DamageInfo.DamageCauser = this;
	DamageInfo.InstigatorWeapon = this;

	// 1. First-class: Target has UDamageHandlerComponent
	UDamageHandlerComponent* TargetHandler = TargetActor->FindComponentByClass<UDamageHandlerComponent>();
	if (TargetHandler)
	{
		TargetHandler->ApplyDamage(OwningCharacter.Get(), TargetActor, DamageInfo, HitResult);
		return;
	}

	// 2. Fallback: Native Unreal Engine TakeDamage
	FPointDamageEvent PointDamageEvent(
		DamageInfo.BaseDamage,
		HitResult,
		HitResult.ImpactNormal,
		nullptr
	);

	TargetActor->TakeDamage(
		DamageInfo.BaseDamage,
		PointDamageEvent,
		OwningCharacter->GetController(),
		this
	);
}
