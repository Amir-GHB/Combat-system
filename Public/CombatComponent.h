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
 * Universal Combat Component (UE 5.8 Compatible):
 * - Manages simultaneously owned weapons and safe, deterministic weapon switching.
 * - Robust state machine: Strictly decoupled ActiveSectionIndex vs PendingSectionIndex.
 * - Time-stamped input buffer with automatic expiration and single-transition-per-window policy.
 * - Attack generation tokens to guard against stale montage callbacks.
 * - Dedicated ActiveAttackWeapon tracking ensuring cleanup never targets the wrong weapon.
 * - Dynamic montage section discovery (zero hardcoded section names or counts).
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

	/** Called by UAnimNotify_SectionEntry when a section actually begins playing */
	UFUNCTION(BlueprintCallable, Category = "Combat|Combo")
	void NotifySectionEntered(FName SectionName, const UAnimSequenceBase* Animation);

	UFUNCTION(BlueprintCallable, Category = "Combat|Attack")
	void ResetCombo();

	// -----------------------------------------------------------
	// State Queries
	// -----------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	ECombatState CurrentCombatState = ECombatState::Idle;

	/** The section index actively playing on screen */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 ActiveSectionIndex = INDEX_NONE;

	/** The next section index requested and linked, but not yet playing */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 PendingSectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	int32 TotalSectionsInMontage = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State")
	bool bComboWindowOpen = false;

	/** Guard: exactly one transition accepted per combo window */
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

	/** Active attack montage asset */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage = nullptr;

	/** Explicit tracking of the weapon that initiated the attack */
	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponBase> ActiveAttackWeapon = nullptr;

	/** Attack generation token: increments on every new attack to reject stale montage callbacks */
	uint64 CurrentAttackToken = 0;

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
	void CommitSectionTransition(int32 NewSectionIndex, FName NewSectionName);
	void SetCombatState(ECombatState NewState);
	void ClearInputBuffer();
	bool IsInputBufferValid() const;
	void StopActiveAttack(bool bInterrupted);
};
