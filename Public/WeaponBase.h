// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatTypes.h"
#include "WeaponBase.generated.h"

class UStaticMeshComponent;
class ACharacter;

/**
 * Base weapon actor responsible for physical/skeletal attachment,
 * multi-point continuous swept melee hit detection, and damage routing.
 */
UCLASS(Blueprintable, ClassGroup=(Combat))
class COMBATSYSTEM_API AWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AWeaponBase();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// -----------------------------------------------------------
	// Components
	// -----------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	// -----------------------------------------------------------
	// Weapon Configuration
	// -----------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	FWeaponConfig WeaponConfig;

	// -----------------------------------------------------------
	// Attachment Controls
	// -----------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Weapon|Equip")
	virtual void AttachToHand(ACharacter* TargetCharacter);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Equip")
	virtual void AttachToHolster(ACharacter* TargetCharacter);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Equip")
	virtual void SetWeaponHidden(bool bHidden);

	// -----------------------------------------------------------
	// Melee Tracing Interface
	// -----------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void BeginTrace();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void TickTrace();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void EndTrace();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Trace")
	virtual void CancelTrace();

	// Target Validation Filter
	virtual bool CanDamageActor(const AActor* TargetActor) const;

	// Getters
	FORCEINLINE bool IsTracing() const { return bIsTracing; }
	FORCEINLINE EWeaponType GetWeaponType() const { return WeaponConfig.WeaponType; }
	FORCEINLINE UAnimMontage* GetComboMontage() const { return WeaponConfig.AttackComboMontage; }
	FORCEINLINE const FWeaponConfig& GetConfig() const { return WeaponConfig; }

protected:
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> OwningCharacter;

	/** Internal trace state */
	bool bIsTracing = false;

	/** Previous tick socket locations for continuous multi-point swept capsule volumes */
	TMap<FName, FVector> PreviousSocketLocations;

	/** Deduplication cache: actors already damaged during this single attack swing */
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> DamagedActorsThisSwing;

	/** Processes a validated hit and dispatches damage to the target */
	virtual void ProcessHit(const FHitResult& HitResult);

	/** Helper to sample current socket locations */
	void SampleSocketLocations(TMap<FName, FVector>& OutLocations) const;
};
