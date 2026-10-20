// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "CombatComponent.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "WeaponBase.h"

UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningCharacter = Cast<ACharacter>(GetOwner());
	if (!OwningCharacter.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Spawn weapons deterministically
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningCharacter.Get();
	SpawnParams.Instigator = OwningCharacter.Get();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const auto& Pair : WeaponClasses)
	{
		const EWeaponType WType = Pair.Key;
		const TSubclassOf<AWeaponBase>& WClass = Pair.Value;

		if (WClass)
		{
			AWeaponBase* Spawned = World->SpawnActor<AWeaponBase>(WClass, SpawnParams);
			if (Spawned)
			{
				SpawnedWeapons.Add(WType, Spawned);
				Spawned->AttachToHolster(OwningCharacter.Get());
			}
		}
	}

	// Equip starting weapon
	EquipWeapon(DefaultWeaponType);

	// Bind montage lifecycle delegates
	if (OwningCharacter->GetMesh() && OwningCharacter->GetMesh()->GetAnimInstance())
	{
		UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance();
		AnimInstance->OnMontageEnded.AddDynamic(this, &UCombatComponent::HandleMontageEnded);
		AnimInstance->OnMontageBlendingOut.AddDynamic(this, &UCombatComponent::HandleMontageBlendingOut);
	}
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetCombo();

	if (CurrentWeapon)
	{
		CurrentWeapon->CancelTrace();
	}

	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::EquipWeapon(EWeaponType NewWeaponType)
{
	if (!OwningCharacter.IsValid()) return;

	// If currently attacking, handle according to configured switching policy
	if (CurrentCombatState == ECombatState::Attacking || CurrentCombatState == ECombatState::ComboWindow)
	{
		if (bQueueWeaponSwitchDuringAttack)
		{
			QueuedWeaponSwitch = NewWeaponType;
			UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Weapon switch to %d queued until attack finishes."), (int32)NewWeaponType);
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Weapon switch rejected: Attack active and queuing disabled."));
			return;
		}
	}

	// Holster existing weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->CancelTrace();
		CurrentWeapon->AttachToHolster(OwningCharacter.Get());
	}

	// Retrieve target weapon
	if (TObjectPtr<AWeaponBase>* FoundWeapon = SpawnedWeapons.Find(NewWeaponType))
	{
		CurrentWeapon = *FoundWeapon;
		CurrentWeaponType = NewWeaponType;

		if (CurrentWeapon)
		{
			CurrentWeapon->AttachToHand(OwningCharacter.Get());
		}
	}
	else
	{
		CurrentWeapon = nullptr;
		CurrentWeaponType = EWeaponType::Unarmed;
	}

	ClearInputBuffer();
	QueuedWeaponSwitch.Reset();
	OnWeaponSwitched.Broadcast(CurrentWeaponType, CurrentWeapon);
}

void UCombatComponent::SwitchToNextWeapon()
{
	if (WeaponCycleOrder.IsEmpty()) return;

	int32 CurrentIdx = WeaponCycleOrder.IndexOfByKey(CurrentWeaponType);
	int32 NextIdx = (CurrentIdx + 1) % WeaponCycleOrder.Num();

	EquipWeapon(WeaponCycleOrder[NextIdx]);
}

AWeaponBase* UCombatComponent::GetWeapon(EWeaponType Type) const
{
	if (const TObjectPtr<AWeaponBase>* Found = SpawnedWeapons.Find(Type))
	{
		return *Found;
	}
	return nullptr;
}

void UCombatComponent::HandleAttackInput()
{
	if (!OwningCharacter.IsValid() || !CurrentWeapon) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Record input timestamp
	LastAttackInputTimestamp = World->GetTimeSeconds();
	bHasBufferedAttackInput = true;

	// CASE 1: IDLE or RECOVERING -> Start a fresh combo
	if (CurrentCombatState == ECombatState::Idle || CurrentCombatState == ECombatState::Recovering)
	{
		StartNewCombo();
		return;
	}

	// CASE 2: Inside Combo Window -> Attempt transition immediately
	if (bComboWindowOpen)
	{
		TryQueueSectionTransition();
	}
}

bool UCombatComponent::StartNewCombo()
{
	if (!CurrentWeapon) return false;

	UAnimMontage* Montage = CurrentWeapon->GetComboMontage();
	if (!Montage || Montage->CompositeSections.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Weapon has no valid montage or sections!"));
		return false;
	}

	UAnimInstance* AnimInstance = OwningCharacter->GetMesh() ? OwningCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return false;

	TotalSectionsInMontage = Montage->CompositeSections.Num();
	ActiveSectionIndex = 0;
	PendingSectionIndex = INDEX_NONE;
	bComboWindowOpen = false;
	bTransitionCommittedForWindow = false;
	ClearInputBuffer();

	FName FirstSection = Montage->GetSectionName(0);
	ActiveAttackMontage = Montage;

	const float Duration = AnimInstance->Montage_Play(Montage);
	if (Duration <= 0.0f)
	{
		ResetCombo();
		return false;
	}

	AnimInstance->Montage_JumpToSection(FirstSection, Montage);
	AnimInstance->Montage_SetNextSection(FirstSection, NAME_None, Montage);

	SetCombatState(ECombatState::Attacking);
	OnComboSectionChanged.Broadcast(ActiveSectionIndex, FirstSection);
	return true;
}

bool UCombatComponent::TryQueueSectionTransition()
{
	// Guard: At most ONE transition accepted per combo window
	if (bTransitionCommittedForWindow)
	{
		return false;
	}

	if (!ActiveAttackMontage || ActiveSectionIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 NextIndex = ActiveSectionIndex + 1;
	if (NextIndex >= TotalSectionsInMontage)
	{
		return false; // Combo chain completed
	}

	UAnimInstance* AnimInstance = OwningCharacter->GetMesh() ? OwningCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return false;

	const FName CurrentSecName = ActiveAttackMontage->GetSectionName(ActiveSectionIndex);
	const FName NextSecName = ActiveAttackMontage->GetSectionName(NextIndex);

	// Dynamically link montage section
	AnimInstance->Montage_SetNextSection(CurrentSecName, NextSecName, ActiveAttackMontage);
	AnimInstance->Montage_SetNextSection(NextSecName, NAME_None, ActiveAttackMontage);

	PendingSectionIndex = NextIndex;
	bTransitionCommittedForWindow = true;
	ClearInputBuffer();

	// Advance tracked active section index
	ActiveSectionIndex = NextIndex;
	OnComboSectionChanged.Broadcast(ActiveSectionIndex, NextSecName);
	return true;
}

void UCombatComponent::NotifyComboWindowBegin(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage) return;

	bComboWindowOpen = true;
	bTransitionCommittedForWindow = false;
	SetCombatState(ECombatState::ComboWindow);

	// Consume buffered input if valid
	if (bHasBufferedAttackInput && IsInputBufferValid())
	{
		TryQueueSectionTransition();
	}
}

void UCombatComponent::NotifyComboWindowEnd(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage) return;

	bComboWindowOpen = false;

	if (CurrentCombatState == ECombatState::ComboWindow)
	{
		SetCombatState(ECombatState::Attacking);
	}

	// If no transition was committed, ensure the montage will blend out to idle
	if (!bTransitionCommittedForWindow && ActiveAttackMontage && ActiveSectionIndex != INDEX_NONE)
	{
		if (UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance())
		{
			const FName CurrentSecName = ActiveAttackMontage->GetSectionName(ActiveSectionIndex);
			AnimInstance->Montage_SetNextSection(CurrentSecName, NAME_None, ActiveAttackMontage);
		}
	}
}

void UCombatComponent::NotifyMeleeTraceBegin(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || !CurrentWeapon) return;
	CurrentWeapon->BeginTrace();
}

void UCombatComponent::NotifyMeleeTraceTick(const UAnimSequenceBase* Animation, float FrameDeltaTime)
{
	if (Animation != ActiveAttackMontage || !CurrentWeapon) return;
	CurrentWeapon->TickTrace();
}

void UCombatComponent::NotifyMeleeTraceEnd(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || !CurrentWeapon) return;
	CurrentWeapon->EndTrace();
}

void UCombatComponent::NotifySectionEntered(int32 SectionIndex, FName SectionName)
{
	ActiveSectionIndex = SectionIndex;
	PendingSectionIndex = INDEX_NONE;
	bTransitionCommittedForWindow = false;
	OnComboSectionChanged.Broadcast(ActiveSectionIndex, SectionName);
}

void UCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == ActiveAttackMontage)
	{
		SetCombatState(ECombatState::Recovering);
		if (CurrentWeapon)
		{
			CurrentWeapon->EndTrace();
		}
	}
}

void UCombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Guard against callbacks from stale/old montages
	if (Montage != ActiveAttackMontage) return;

	ResetCombo();
	OnAttackEnded.Broadcast();

	// Process queued weapon switch if one was deferred during the attack
	if (QueuedWeaponSwitch.IsSet())
	{
		const EWeaponType TargetType = QueuedWeaponSwitch.GetValue();
		QueuedWeaponSwitch.Reset();
		EquipWeapon(TargetType);
	}
}

void UCombatComponent::ResetCombo()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->CancelTrace();
	}

	ActiveAttackMontage = nullptr;
	ActiveSectionIndex = INDEX_NONE;
	PendingSectionIndex = INDEX_NONE;
	TotalSectionsInMontage = 0;
	bComboWindowOpen = false;
	bTransitionCommittedForWindow = false;
	ClearInputBuffer();
	SetCombatState(ECombatState::Idle);
}

void UCombatComponent::SetCombatState(ECombatState NewState)
{
	if (CurrentCombatState != NewState)
	{
		CurrentCombatState = NewState;
		OnCombatStateChanged.Broadcast(NewState);
	}
}

void UCombatComponent::ClearInputBuffer()
{
	bHasBufferedAttackInput = false;
	LastAttackInputTimestamp = -1.0;
}

bool UCombatComponent::IsInputBufferValid() const
{
	if (!bHasBufferedAttackInput) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;

	return (World->GetTimeSeconds() - LastAttackInputTimestamp) <= InputBufferDuration;
}
