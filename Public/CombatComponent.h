// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatTypes.h"
#include "CombatComponent.generated.h"

class AWeaponBase;
class UAnimMontage;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponSwitchedSignature, EWeaponType, NewWeaponType, AWeaponBase*, NewWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboSectionChangedSignature, int32, SectionIndex, FName, SectionName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatStateChangedSignature, ECombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackEndedSignature);

/**
 * Universal Combat Component:
 * - Manages simultaneously owned weapons and deterministic weapon switching.
 * - Robust state machine: Distinct ActiveSection vs RequestedNextSection tracking.
 * - Time-stamped input buffer with automatic expiration.
 * - Protection against repeated input skipping combo sections.
 * - Dynamic montage section discovery (zero hardcoded section names).
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class COMBATSYSTEM_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// -----------------------------------------------------------
	// Weapon Inventory & Switching
	// -----------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	TMap<EWeaponType, TSubclassOf<AWeaponBase>> WeaponClasses;

	/** Explicit deterministic weapon cycle order */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	TArray<EWeaponType> WeaponCycleOrder = { EWeaponType::Sword, EWeaponType::BullHeadHammer, EWeaponType::Unarmed };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	EWeaponType DefaultWeaponType = EWeaponType::Sword;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	TMap<EWeaponType, TObjectPtr<AWeaponBase>> SpawnedWeapons;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	TObjectPtr<AWeaponBase> CurrentWeapon = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	/** If true, weapon switch requests during an attack are queued until the attack finishes */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Weapons")
	bool bQueueWeaponSwitchDuringAttack = true;

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons")
	void EquipWeapon(EWeaponType NewWeaponType);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons")
	void SwitchToNextWeapon();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapons")
	AWeaponBase* GetWeapon(EWeaponType Type) const;

	// -----------------------------------------------------------
	// Attack Input & Combo State Machine
	// -----------------------------------------------------------
	/** Time window (seconds) during which an attack input remains valid in the buffer */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Buffer", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float InputBufferDuration = 0.35f;

	UFUNCTION(BlueprintCallable, Category = "Combat|Attack")
	void HandleAttackInput();

	/** Called by UAnimNotifyState_ComboWindow */
	void NotifyComboWindowBegin(const UAnimSequenceBase* Animation);
	void NotifyComboWindowEnd(const UAnimSequenceBase* Animation);

	/** Called by UAnimNotifyState_MeleeTrace */
	void NotifyMeleeTraceBegin(const UAnimSequenceBase* Animation);
	void NotifyMeleeTraceTick(const UAnimSequenceBase* Animation, float FrameDeltaTime);
	void NotifyMeleeTraceEnd(const UAnimSequenceBase* Animation);

	/** Notifies that a new section actually began playing */
	UFUNCTION(BlueprintCallable, Category = "Combat|Combo")
	void NotifySectionEntered(int32 SectionIndex, FName SectionName);

	UFUNCTION(BlueprintCallable, Category = "Combat|Attack")
	void ResetCombo();

	// -----------------------------------------------------------
	// State Queries
	// -----------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	ECombatState CurrentCombatState = ECombatState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 ActiveSectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 PendingSectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 TotalSectionsInMontage = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bComboWindowOpen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bTransitionCommittedForWindow = false;

	// -----------------------------------------------------------
	// Delegates
	// -----------------------------------------------------------
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponSwitchedSignature OnWeaponSwitched;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnComboSectionChangedSignature OnComboSectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnCombatStateChangedSignature OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnAttackEndedSignature OnAttackEnded;

protected:
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> OwningCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage = nullptr;

	/** Timestamp of the last valid attack input */
	double LastAttackInputTimestamp = -1.0;

	/** Flag indicating an attack input is buffered */
	bool bHasBufferedAttackInput = false;

	/** Queued weapon switch target */
	TOptional<EWeaponType> QueuedWeaponSwitch;

	/** Montage callbacks */
	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	bool StartNewCombo();
	bool TryQueueSectionTransition();
	void CommitTransitionToNextSection();
	void SetCombatState(ECombatState NewState);
	void ClearInputBuffer();
	bool IsInputBufferValid() const;
};
