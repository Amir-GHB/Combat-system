// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "Weapons/Weapon_Unarmed.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

AWeapon_Unarmed::AWeapon_Unarmed()
{
	WeaponConfig.WeaponType = EWeaponType::Unarmed;
	WeaponConfig.WeaponDisplayName = FText::FromString(TEXT("Unarmed Combat"));
	WeaponConfig.BaseDamage = 20.0f;
	WeaponConfig.PoiseDamage = 15.0f;
	WeaponConfig.KnockbackImpulse = 300.0f;
	WeaponConfig.DamageCategory = EDamageCategory::Bludgeon;
	WeaponConfig.TraceRadius = 18.0f;

	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(false);
	}
}

void AWeapon_Unarmed::SampleSocketLocations(TMap<FName, FVector>& OutLocations) const
{
	OutLocations.Empty(ValidatedTraceSockets.Num());

	if (!OwningCharacter.IsValid() || !OwningCharacter->GetMesh()) return;

	USkeletalMeshComponent* SkelMesh = OwningCharacter->GetMesh();
	for (const FName& SName : ValidatedTraceSockets)
	{
		OutLocations.Add(SName, SkelMesh->GetSocketLocation(SName));
	}
}

void AWeapon_Unarmed::BeginTrace()
{
	if (!OwningCharacter.IsValid() || !OwningCharacter->GetMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AWeapon_Unarmed::BeginTrace] OwningCharacter or SkeletalMesh is null!"));
		return;
	}

	USkeletalMeshComponent* SkelMesh = OwningCharacter->GetMesh();

	// Validate skeletal sockets exist on the character mesh
	ValidatedTraceSockets.Empty();
	for (const FName& SName : SkeletalTraceSockets)
	{
		if (SkelMesh->DoesSocketExist(SName))
		{
			ValidatedTraceSockets.Add(SName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[AWeapon_Unarmed::BeginTrace] Skeletal socket '%s' not found on character mesh '%s'!"),
				*SName.ToString(), *GetNameSafe(SkelMesh->GetSkeletalMeshAsset()));
		}
	}

	if (ValidatedTraceSockets.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[AWeapon_Unarmed::BeginTrace] No valid skeletal sockets found on character! Aborting unarmed trace."));
		bIsTracing = false;
		return;
	}

	bIsTracing = true;
	DamagedActorsThisSwing.Empty();
	PreviousSocketLocations.Empty();

	SampleSocketLocations(PreviousSocketLocations);
}

void AWeapon_Unarmed::TickTrace()
{
	if (!bIsTracing || !OwningCharacter.IsValid() || !OwningCharacter->GetMesh() || ValidatedTraceSockets.IsEmpty()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	TMap<FName, FVector> CurrentLocations;
	SampleSocketLocations(CurrentLocations);

	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(WeaponConfig.TraceRadius);
	FCollisionQueryParams QueryParams(TEXT("UnarmedTrace"), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwningCharacter.Get());

	TArray<FHitResult> AllHits;

	for (const auto& Pair : CurrentLocations)
	{
		const FName& SName = Pair.Key;
		const FVector& Curr = Pair.Value;
		const FVector* Prev = PreviousSocketLocations.Find(SName);
		const FVector Start = Prev ? *Prev : Curr;

		TArray<FHitResult> Hits;
		World->SweepMultiByChannel(
			Hits,
			Start,
			Curr,
			FQuat::Identity,
			WeaponConfig.TraceCollisionChannel,
			SphereShape,
			QueryParams
		);
		AllHits.Append(Hits);

		if (WeaponConfig.bDrawDebugTraces)
		{
			DrawDebugCapsule(World, (Start + Curr) * 0.5f, (Start - Curr).Size() * 0.5f + WeaponConfig.TraceRadius, WeaponConfig.TraceRadius, FRotationMatrix::MakeFromZ(Curr - Start).ToQuat(), FColor::Cyan, false, 0.3f);
		}
	}

	for (const FHitResult& Hit : AllHits)
	{
		AActor* HitActor = Hit.GetActor();
		if (CanDamageActor(HitActor) && !DamagedActorsThisSwing.Contains(HitActor))
		{
			DamagedActorsThisSwing.Add(HitActor);
			ProcessHit(Hit);
		}
	}

	PreviousSocketLocations = CurrentLocations;
}
