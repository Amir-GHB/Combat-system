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
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[UCombatComponent::BeginPlay] Failed to spawn weapon of type %d"), (int32)WType);
			}
		}
	}

	// Equip starting weapon
	EquipWeapon(DefaultWeaponType);

	// Bind global montage lifecycle delegates on AnimInstance as secondary listener
	if (OwningCharacter->GetMesh() && OwningCharacter->GetMesh()->GetAnimInstance())
	{
		UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance();
		AnimInstance->OnMontageEnded.AddDynamic(this, &UCombatComponent::HandleMontageEnded);
		AnimInstance->OnMontageBlendingOut.AddDynamic(this, &UCombatComponent::HandleMontageBlendingOut);
	}
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Remove delegate bindings cleanly to prevent stale callbacks
	if (OwningCharacter.IsValid() && OwningCharacter->GetMesh())
	{
		if (UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance())
		{
			AnimInstance->OnMontageEnded.RemoveDynamic(this, &UCombatComponent::HandleMontageEnded);
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UCombatComponent::HandleMontageBlendingOut);
		}
	}

	StopActiveAttack(true, true, 0.0f);
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::EquipWeapon(EWeaponType NewWeaponType)
{
	if (!OwningCharacter.IsValid()) return;

	// 1. If currently attacking, handle via queued switch policy
	if (CurrentCombatState == ECombatState::Attacking || CurrentCombatState == ECombatState::ComboWindow)
	{
		if (bQueueWeaponSwitchDuringAttack)
		{
			// Verify target weapon exists before queuing
			if (TObjectPtr<AWeaponBase>* TargetPtr = SpawnedWeapons.Find(NewWeaponType))
			{
				if (IsValid(*TargetPtr))
				{
					QueuedWeaponSwitch = NewWeaponType;
					UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Weapon switch to %d queued until attack finishes."), (int32)NewWeaponType);
					return;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Cannot queue switch: Target weapon %d is invalid!"), (int32)NewWeaponType);
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Weapon switch rejected: Attack active and queuing disabled."));
			return;
		}
	}

	// 2. No-op if already equipped
	if (NewWeaponType == CurrentWeaponType && CurrentWeapon != nullptr)
	{
		return;
	}

	// 3. VALIDATION BEFORE HOLSTERING: Ensure requested weapon exists and is valid
	TObjectPtr<AWeaponBase>* FoundWeapon = SpawnedWeapons.Find(NewWeaponType);
	if (!FoundWeapon || !IsValid(*FoundWeapon))
	{
		UE_LOG(LogTemp, Error, TEXT("[UCombatComponent::EquipWeapon] Requested weapon %d is not found or invalid! Current weapon '%s' preserved."),
			(int32)NewWeaponType, CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("None"));
		return;
	}

	// 4. Safely holster current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->CancelTrace();
		CurrentWeapon->AttachToHolster(OwningCharacter.Get());
	}

	// 5. Equip new validated weapon
	CurrentWeapon = *FoundWeapon;
	CurrentWeaponType = NewWeaponType;
	CurrentWeapon->AttachToHand(OwningCharacter.Get());

	ClearInputBuffer();
	QueuedWeaponSwitch.Reset();
	OnWeaponSwitched.Broadcast(CurrentWeaponType, CurrentWeapon);
}

void UCombatComponent::SwitchToNextWeapon()
{
	// Build cycle list of only currently valid spawned weapons
	TArray<EWeaponType> ValidCycleWeapons;
	for (const EWeaponType Type : WeaponCycleOrder)
	{
		if (TObjectPtr<AWeaponBase>* WPtr = SpawnedWeapons.Find(Type))
		{
			if (IsValid(*WPtr))
			{
				ValidCycleWeapons.Add(Type);
			}
		}
	}

	if (ValidCycleWeapons.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent::SwitchToNextWeapon] No valid spawned weapons available to cycle!"));
		return;
	}

	int32 CurrentIdx = ValidCycleWeapons.IndexOfByKey(CurrentWeaponType);
	int32 NextIdx = (CurrentIdx != INDEX_NONE) ? ((CurrentIdx + 1) % ValidCycleWeapons.Num()) : 0;

	EquipWeapon(ValidCycleWeapons[NextIdx]);
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

	// CASE 1: IDLE or RECOVERING -> Start a fresh combo
	if (CurrentCombatState == ECombatState::Idle || CurrentCombatState == ECombatState::Recovering)
	{
		LastAttackInputTimestamp = World->GetTimeSeconds();
		bHasBufferedAttackInput = true;
		StartNewCombo();
		return;
	}

	// CASE 2: Attack is already active
	if (CurrentCombatState == ECombatState::Attacking || CurrentCombatState == ECombatState::ComboWindow)
	{
		// STRICT BUFFER POLICY: If a transition was already committed for this window,
		// DISCARD extra inputs completely to prevent skipping future sections!
		if (bTransitionCommittedForWindow)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[UCombatComponent] Extra attack input discarded: Transition already committed for current window."));
			return;
		}

		// If combo window is open right now, try queuing transition immediately
		if (bComboWindowOpen)
		{
			LastAttackInputTimestamp = World->GetTimeSeconds();
			bHasBufferedAttackInput = true;
			TryQueueSectionTransition();
		}
		else
		{
			// Early input before window opens: buffer it
			LastAttackInputTimestamp = World->GetTimeSeconds();
			bHasBufferedAttackInput = true;
			UE_LOG(LogTemp, Verbose, TEXT("[UCombatComponent] Early attack input buffered."));
		}
	}
}

bool UCombatComponent::StartNewCombo()
{
	if (!CurrentWeapon) return false;

	UAnimMontage* Montage = CurrentWeapon->GetComboMontage();
	if (!Montage || Montage->CompositeSections.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Weapon has no valid montage or sections!"));
		ClearInputBuffer();
		return false;
	}

	UAnimInstance* AnimInstance = OwningCharacter->GetMesh() ? OwningCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return false;

	// Safely retire any ongoing attack first, stopping active montage playback and canceling traces
	StopActiveAttack(true, true, 0.1f);

	// Increment and set generation token for this new attack instance
	ActiveAttackToken = ++CurrentAttackToken;

	TotalSectionsInMontage = Montage->CompositeSections.Num();
	ActiveSectionIndex = 0;
	PendingSectionIndex = INDEX_NONE;
	bComboWindowOpen = false;
	bTransitionCommittedForWindow = false;
	ClearInputBuffer();

	FName FirstSection = Montage->GetSectionName(0);
	ActiveAttackMontage = Montage;
	ActiveAttackWeapon = CurrentWeapon;

	const float Duration = AnimInstance->Montage_Play(Montage);
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCombatComponent] Montage_Play failed on %s!"), *Montage->GetName());
		StopActiveAttack(true, false);
		return false;
	}

	// Bind instance-specific callbacks with generation token to guarantee isolation from stale playbacks
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UCombatComponent::OnMontageInstanceEnded, ActiveAttackToken);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

	FOnMontageBlendingOutStarted BlendDelegate;
	BlendDelegate.BindUObject(this, &UCombatComponent::OnMontageInstanceBlendingOut, ActiveAttackToken);
	AnimInstance->Montage_SetBlendingOutDelegate(BlendDelegate, Montage);

	FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(Montage);
	ActiveMontageInstanceId = Instance ? Instance->GetInstanceId() : INDEX_NONE;

	AnimInstance->Montage_JumpToSection(FirstSection, Montage);
	AnimInstance->Montage_SetNextSection(FirstSection, NAME_None, Montage);

	SetCombatState(ECombatState::Attacking);
	OnComboSectionChanged.Broadcast(ActiveSectionIndex, FirstSection);
	return true;
}

bool UCombatComponent::TryQueueSectionTransition()
{
	// Enforce single transition per window
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
		return false; // Reached end of combo chain
	}

	UAnimInstance* AnimInstance = OwningCharacter->GetMesh() ? OwningCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return false;

	const FName CurrentSecName = ActiveAttackMontage->GetSectionName(ActiveSectionIndex);
	const FName NextSecName = ActiveAttackMontage->GetSectionName(NextIndex);

	// Dynamically link montage section
	AnimInstance->Montage_SetNextSection(CurrentSecName, NextSecName, ActiveAttackMontage);
	AnimInstance->Montage_SetNextSection(NextSecName, NAME_None, ActiveAttackMontage);

	// Keep pending and active section state strictly separate!
	// ActiveSectionIndex remains unchanged until the animation actually enters NextIndex.
	PendingSectionIndex = NextIndex;
	bTransitionCommittedForWindow = true;
	ClearInputBuffer();

	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Transition queued: %s -> %s (Pending index: %d)"),
		*CurrentSecName.ToString(), *NextSecName.ToString(), PendingSectionIndex);

	return true;
}

void UCombatComponent::CommitSectionTransition(int32 NewSectionIndex, FName NewSectionName)
{
	ActiveSectionIndex = NewSectionIndex;
	PendingSectionIndex = INDEX_NONE;
	bTransitionCommittedForWindow = false;

	OnComboSectionChanged.Broadcast(ActiveSectionIndex, NewSectionName);
	UE_LOG(LogTemp, Log, TEXT("[UCombatComponent] Section committed: %s (Active index: %d)"),
		*NewSectionName.ToString(), ActiveSectionIndex);
}

void UCombatComponent::NotifySectionEntered(FName SectionName, const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || !ActiveAttackMontage || ActiveAttackToken == 0) return;

	const int32 FoundIndex = ActiveAttackMontage->GetSectionIndex(SectionName);
	if (FoundIndex == INDEX_NONE) return;

	// Commit section progression when confirmed by notify
	if (FoundIndex == PendingSectionIndex || (PendingSectionIndex == INDEX_NONE && FoundIndex == ActiveSectionIndex + 1))
	{
		CommitSectionTransition(FoundIndex, SectionName);
	}
}

void UCombatComponent::NotifyComboWindowBegin(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || ActiveAttackToken == 0) return;

	// Reliable progression confirmation check:
	// Verify that the AnimInstance has genuinely entered the pending section before advancing state
	if (PendingSectionIndex != INDEX_NONE && ActiveAttackMontage && OwningCharacter.IsValid() && OwningCharacter->GetMesh())
	{
		if (UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance())
		{
			const FName CurrentPlayingSection = AnimInstance->Montage_GetCurrentSection(ActiveAttackMontage);
			const int32 CurrentPlayingIndex = ActiveAttackMontage->GetSectionIndex(CurrentPlayingSection);

			// Advance only if the playback is confirmed to have reached the pending section!
			if (CurrentPlayingIndex == PendingSectionIndex)
			{
				CommitSectionTransition(CurrentPlayingIndex, CurrentPlayingSection);
			}
			else
			{
				UE_LOG(LogTemp, Verbose, TEXT("[UCombatComponent] Section check: Montage currently at '%s' (index %d), pending index is %d. Not advancing."),
					*CurrentPlayingSection.ToString(), CurrentPlayingIndex, PendingSectionIndex);
			}
		}
	}

	bComboWindowOpen = true;
	bTransitionCommittedForWindow = false;
	SetCombatState(ECombatState::ComboWindow);

	// Consume buffered input if within expiration window
	if (bHasBufferedAttackInput && IsInputBufferValid())
	{
		TryQueueSectionTransition();
	}
	else
	{
		ClearInputBuffer();
	}
}

void UCombatComponent::NotifyComboWindowEnd(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || ActiveAttackToken == 0) return;

	bComboWindowOpen = false;

	if (CurrentCombatState == ECombatState::ComboWindow)
	{
		SetCombatState(ECombatState::Attacking);
	}

	// Discard any unconsumed buffered input when window closes
	ClearInputBuffer();

	// If no transition was committed, ensure the montage will blend out to idle
	if (!bTransitionCommittedForWindow && ActiveAttackMontage && ActiveSectionIndex != INDEX_NONE)
	{
		if (OwningCharacter.IsValid() && OwningCharacter->GetMesh())
		{
			if (UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance())
			{
				const FName CurrentSecName = ActiveAttackMontage->GetSectionName(ActiveSectionIndex);
				AnimInstance->Montage_SetNextSection(CurrentSecName, NAME_None, ActiveAttackMontage);
			}
		}
	}
}

void UCombatComponent::NotifyMeleeTraceBegin(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || ActiveAttackToken == 0) return;

	// Always target the weapon that initiated the attack
	if (ActiveAttackWeapon.IsValid())
	{
		ActiveAttackWeapon->BeginTrace();
	}
}

void UCombatComponent::NotifyMeleeTraceTick(const UAnimSequenceBase* Animation, float FrameDeltaTime)
{
	if (Animation != ActiveAttackMontage || ActiveAttackToken == 0) return;

	if (ActiveAttackWeapon.IsValid())
	{
		ActiveAttackWeapon->TickTrace();
	}
}

void UCombatComponent::NotifyMeleeTraceEnd(const UAnimSequenceBase* Animation)
{
	if (Animation != ActiveAttackMontage || ActiveAttackToken == 0) return;

	if (ActiveAttackWeapon.IsValid())
	{
		ActiveAttackWeapon->EndTrace();
	}
}

void UCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// Guard against callbacks during internal StopActiveAttack or from stale attack tokens
	if (bIsStoppingAttack || Montage != ActiveAttackMontage || ActiveAttackToken == 0) return;

	SetCombatState(ECombatState::Recovering);

	// Always terminate trace on blending out
	if (ActiveAttackWeapon.IsValid())
	{
		ActiveAttackWeapon->EndTrace();
	}
}

void UCombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Guard against callbacks during internal StopActiveAttack or from stale attack tokens
	if (bIsStoppingAttack || Montage != ActiveAttackMontage || ActiveAttackToken == 0) return;

	// Montage is already ended on the AnimInstance, so avoid redundant Montage_Stop call
	StopActiveAttack(bInterrupted, /*bStopMontage=*/false);
	OnAttackEnded.Broadcast();

	// Process queued weapon switch if one was deferred during the attack
	if (QueuedWeaponSwitch.IsSet())
	{
		const EWeaponType TargetType = QueuedWeaponSwitch.GetValue();
		QueuedWeaponSwitch.Reset();
		EquipWeapon(TargetType);
	}
}

void UCombatComponent::OnMontageInstanceBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 AttackToken)
{
	if (AttackToken != ActiveAttackToken || AttackToken == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[UCombatComponent] Stale blending out callback rejected for token %llu (active token: %llu)"), AttackToken, ActiveAttackToken);
		return;
	}
	HandleMontageBlendingOut(Montage, bInterrupted);
}

void UCombatComponent::OnMontageInstanceEnded(UAnimMontage* Montage, bool bInterrupted, uint64 AttackToken)
{
	if (AttackToken != ActiveAttackToken || AttackToken == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[UCombatComponent] Stale montage ended callback rejected for token %llu (active token: %llu)"), AttackToken, ActiveAttackToken);
		return;
	}
	HandleMontageEnded(Montage, bInterrupted);
}

void UCombatComponent::ResetCombo()
{
	StopActiveAttack(true, true, 0.2f);
}

void UCombatComponent::StopActiveAttack(bool bInterrupted, bool bStopMontage, float BlendOutTime)
{
	if (bIsStoppingAttack)
	{
		return;
	}

	TGuardValue<bool> StoppingGuard(bIsStoppingAttack, true);

	// Invalidate the attack token FIRST so any synchronous callbacks from Montage_Stop are treated as stale
	const uint64 RetiredToken = ActiveAttackToken;
	ActiveAttackToken = 0;
	ActiveMontageInstanceId = INDEX_NONE;

	// Cancel trace on the specific weapon that initiated the attack
	if (ActiveAttackWeapon.IsValid())
	{
		ActiveAttackWeapon->CancelTrace();
	}

	UAnimMontage* MontageToStop = ActiveAttackMontage;

	// Stop montage playback if requested and still actively playing
	if (bStopMontage && MontageToStop && OwningCharacter.IsValid() && OwningCharacter->GetMesh())
	{
		if (UAnimInstance* AnimInstance = OwningCharacter->GetMesh()->GetAnimInstance())
		{
			if (AnimInstance->Montage_IsPlaying(MontageToStop))
			{
				AnimInstance->Montage_Stop(BlendOutTime, MontageToStop);
			}
		}
	}

	ActiveAttackWeapon = nullptr;
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
