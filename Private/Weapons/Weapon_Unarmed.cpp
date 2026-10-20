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

void AWeapon_Unarmed::BeginTrace()
{
	bIsTracing = true;
	DamagedActorsThisSwing.Empty();
	PreviousSocketLocations.Empty();

	if (OwningCharacter.IsValid() && OwningCharacter->GetMesh())
	{
		USkeletalMeshComponent* SkelMesh = OwningCharacter->GetMesh();
		for (const FName& SName : SkeletalTraceSockets)
		{
			PreviousSocketLocations.Add(SName, SkelMesh->GetSocketLocation(SName));
		}
	}
}

void AWeapon_Unarmed::TickTrace()
{
	if (!bIsTracing || !OwningCharacter.IsValid() || !OwningCharacter->GetMesh()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	USkeletalMeshComponent* SkelMesh = OwningCharacter->GetMesh();
	TMap<FName, FVector> CurrentLocations;
	for (const FName& SName : SkeletalTraceSockets)
	{
		CurrentLocations.Add(SName, SkelMesh->GetSocketLocation(SName));
	}

	FCollisionShape SphereShape = FCollisionShape::MakeSphere(WeaponConfig.TraceRadius);
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
