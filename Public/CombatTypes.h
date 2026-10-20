// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CombatTypes.generated.h"

class AWeaponBase;
class UAnimMontage;

/**
 * Supported weapon classifications. Extensible for project-specific weapons.
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Unarmed        UMETA(DisplayName = "Unarmed / Martial Arts"),
	Sword          UMETA(DisplayName = "One-Handed / Longsword"),
	BullHeadHammer UMETA(DisplayName = "Bull Head Hammer"),
	CustomWeapon   UMETA(DisplayName = "Custom Extensible Weapon")
};

/**
 * Explicit combat state machine tracking character combat lifecycle.
 */
UENUM(BlueprintType)
enum class ECombatState : uint8
{
	Idle           UMETA(DisplayName = "Idle"),
	Attacking      UMETA(DisplayName = "Attacking"),
	ComboWindow    UMETA(DisplayName = "Combo Window Active"),
	Recovering     UMETA(DisplayName = "Recovering / Blend Out"),
	SwappingWeapon UMETA(DisplayName = "Swapping Weapon"),
	Staggered      UMETA(DisplayName = "Staggered / Hit Stun"),
	Dead           UMETA(DisplayName = "Dead")
};

/**
 * Damage category classification for mitigation, sound, and VFX responses.
 */
UENUM(BlueprintType)
enum class EDamageCategory : uint8
{
	Physical UMETA(DisplayName = "Physical Generic"),
	Slashing UMETA(DisplayName = "Slashing (Sword)"),
	Blunt    UMETA(DisplayName = "Blunt / Crushing (Hammer)"),
	Bludgeon UMETA(DisplayName = "Hand-to-Hand Impact (Unarmed)"),
	Piercing UMETA(DisplayName = "Piercing Thrust")
};

/**
 * Structured payload dispatched to DamageHandlerComponent and damage delegates.
 */
USTRUCT(BlueprintType)
struct FDamageInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float BaseDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float PoiseDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	float KnockbackImpulse = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	EDamageCategory DamageCategory = EDamageCategory::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	EWeaponType WeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	bool bIsCriticalHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	TWeakObjectPtr<AActor> DamageCauser = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage")
	TWeakObjectPtr<AWeaponBase> InstigatorWeapon = nullptr;

	/**
	 * Canonical calculated raw damage before armor or resistance mitigation.
	 * Guards against non-finite values (NaN / Inf) and clamps BaseDamage and Multiplier to >= 0.0.
	 * Preserves intentional zero damage and applies the multiplier exactly once.
	 */
	FORCEINLINE float GetCalculatedRawDamage() const
	{
		if (!FMath::IsFinite(BaseDamage) || !FMath::IsFinite(DamageMultiplier))
		{
			return 0.0f;
		}
		const float ValidBase = FMath::Max(0.0f, BaseDamage);
		const float ValidMultiplier = FMath::Max(0.0f, DamageMultiplier);
		return ValidBase * ValidMultiplier;
	}

	/**
	 * Normalizes all numerical fields in-place to ensure no non-finite or negative values propagate.
	 * Preserves legitimate zero damage while guaranteeing non-negative finite bounds.
	 */
	FORCEINLINE void Normalize()
	{
		BaseDamage = (!FMath::IsFinite(BaseDamage) || BaseDamage < 0.0f) ? 0.0f : BaseDamage;
		DamageMultiplier = (!FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.0f) ? 0.0f : DamageMultiplier;
		PoiseDamage = (!FMath::IsFinite(PoiseDamage) || PoiseDamage < 0.0f) ? 0.0f : PoiseDamage;
		KnockbackImpulse = (!FMath::IsFinite(KnockbackImpulse) || KnockbackImpulse < 0.0f) ? 0.0f : KnockbackImpulse;
	}
};

/**
 * Data-driven configuration struct for weapon attributes and runtime behavior.
 */
USTRUCT(BlueprintType)
struct FWeaponConfig : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identification")
	EWeaponType WeaponType = EWeaponType::Sword;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Identification")
	FText WeaponDisplayName = FText::FromString(TEXT("Default Weapon"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Animation")
	TObjectPtr<UAnimMontage> AttackComboMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float BaseDamage = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float PoiseDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	float KnockbackImpulse = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage")
	EDamageCategory DamageCategory = EDamageCategory::Slashing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sockets")
	FName HandSocketName = FName("Hand_R_Socket");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sockets")
	FName HolsterSocketName = FName("Holster_Back_Socket");

	/** Sockets sampled along the weapon geometry for swept continuous collision */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	TArray<FName> TraceSocketNames = { FName("Trace_Start"), FName("Trace_Mid"), FName("Trace_End") };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	float TraceRadius = 14.0f;

	/** Maximum performance-capped interpolation substeps per tick for fast weapon rotations / large frame deltas */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Trace", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxTraceSubsteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> TraceCollisionChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Trace")
	bool bDrawDebugTraces = false;
};
