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
	OutLocations.Empty(ValidatedTraceSockets.Num());

	if (!WeaponMesh) return;

	for (const FName& SocketName : ValidatedTraceSockets)
	{
		OutLocations.Add(SocketName, WeaponMesh->GetSocketLocation(SocketName));
	}
}

void AWeaponBase::BeginTrace()
{
	if (!WeaponMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AWeaponBase::BeginTrace] Cannot begin trace: WeaponMesh is null on %s"), *GetName());
		return;
	}

	// Validate sockets on the static mesh before enabling trace
	ValidatedTraceSockets.Empty();
	for (const FName& SocketName : WeaponConfig.TraceSocketNames)
	{
		if (WeaponMesh->DoesSocketExist(SocketName))
		{
			ValidatedTraceSockets.Add(SocketName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[AWeaponBase::BeginTrace] Trace socket '%s' does not exist on mesh '%s'!"),
				*SocketName.ToString(), *GetNameSafe(WeaponMesh->GetStaticMesh()));
		}
	}

	if (ValidatedTraceSockets.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[AWeaponBase::BeginTrace] No valid trace sockets found for %s! Aborting trace."), *GetName());
		bIsTracing = false;
		return;
	}

	bIsTracing = true;
	DamagedActorsThisSwing.Empty();
	SampleSocketLocations(PreviousSocketLocations);
}

void AWeaponBase::TickTrace()
{
	if (!bIsTracing || !WeaponMesh || ValidatedTraceSockets.IsEmpty()) return;

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

	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(WeaponConfig.TraceRadius);
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

	// 2. Segment & Diagonal Sweeps: Sweep between adjacent sockets on current frame
	// and cross-sweep diagonals (PrevS1->CurrS2 and PrevS2->CurrS1) to cover fast rotational weapon sweeps
	if (ValidatedTraceSockets.Num() >= 2)
	{
		for (int32 i = 0; i < ValidatedTraceSockets.Num() - 1; ++i)
		{
			const FName& S1 = ValidatedTraceSockets[i];
			const FName& S2 = ValidatedTraceSockets[i + 1];

			const FVector* CurrP1 = CurrentSocketLocations.Find(S1);
			const FVector* CurrP2 = CurrentSocketLocations.Find(S2);
			const FVector* PrevP1 = PreviousSocketLocations.Find(S1);
			const FVector* PrevP2 = PreviousSocketLocations.Find(S2);

			if (CurrP1 && CurrP2)
			{
				// Current blade segment
				TArray<FHitResult> SegmentHits;
				World->SweepMultiByChannel(SegmentHits, *CurrP1, *CurrP2, FQuat::Identity, WeaponConfig.TraceCollisionChannel, SphereShape, QueryParams);
				AllHitResults.Append(SegmentHits);

				// Cross diagonal sweeps to catch angular rotational slices
				if (PrevP1 && PrevP2)
				{
					TArray<FHitResult> CrossHits1;
					World->SweepMultiByChannel(CrossHits1, *PrevP1, *CurrP2, FQuat::Identity, WeaponConfig.TraceCollisionChannel, SphereShape, QueryParams);
					AllHitResults.Append(CrossHits1);

					TArray<FHitResult> CrossHits2;
					World->SweepMultiByChannel(CrossHits2, *PrevP2, *CurrP1, FQuat::Identity, WeaponConfig.TraceCollisionChannel, SphereShape, QueryParams);
					AllHitResults.Append(CrossHits2);
				}
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
	ValidatedTraceSockets.Empty();
}

void AWeaponBase::CancelTrace()
{
	EndTrace();
}

bool AWeaponBase::CanDamageActor(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor) || TargetActor->IsPendingKillPending())
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

	// Build structured damage payload using data-driven config and active attack multiplier
	FDamageInfo DamageInfo;
	DamageInfo.BaseDamage = WeaponConfig.BaseDamage;
	DamageInfo.DamageMultiplier = CurrentDamageMultiplier;
	DamageInfo.PoiseDamage = WeaponConfig.PoiseDamage;
	DamageInfo.KnockbackImpulse = WeaponConfig.KnockbackImpulse;
	DamageInfo.DamageCategory = WeaponConfig.DamageCategory;
	DamageInfo.WeaponType = WeaponConfig.WeaponType;
	DamageInfo.DamageCauser = this;
	DamageInfo.InstigatorWeapon = this;

	// Calculate unified raw damage consistently across both handler and native fallback
	const float CalculatedRawDamage = DamageInfo.GetCalculatedRawDamage();

	// 1. Primary: Target implements UDamageHandlerComponent
	UDamageHandlerComponent* TargetHandler = TargetActor->FindComponentByClass<UDamageHandlerComponent>();
	if (TargetHandler)
	{
		TargetHandler->ApplyDamage(OwningCharacter.Get(), TargetActor, DamageInfo, HitResult);
		return;
	}

	// 2. Fallback: Native Unreal Engine TakeDamage
	FPointDamageEvent PointDamageEvent(
		CalculatedRawDamage,
		HitResult,
		HitResult.ImpactNormal,
		nullptr
	);

	TargetActor->TakeDamage(
		CalculatedRawDamage,
		PointDamageEvent,
		OwningCharacter->GetController(),
		this
	);
}
