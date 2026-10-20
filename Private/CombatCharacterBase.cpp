// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "CombatCharacterBase.h"
#include "CombatComponent.h"
#include "DamageHandlerComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

ACombatCharacterBase::ACombatCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	CombatComponent = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	DamageHandlerComponent = CreateDefaultSubobject<UDamageHandlerComponent>(TEXT("DamageHandlerComponent"));
}

void ACombatCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void ACombatCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (AttackAction)
		{
			EnhancedInput->BindAction(AttackAction, ETriggerEvent::Started, this, &ACombatCharacterBase::OnAttackTriggered);
		}
		if (NextWeaponAction)
		{
			EnhancedInput->BindAction(NextWeaponAction, ETriggerEvent::Started, this, &ACombatCharacterBase::OnNextWeaponTriggered);
		}
	}
}

void ACombatCharacterBase::OnAttackTriggered(const FInputActionValue& Value)
{
	if (CombatComponent)
	{
		CombatComponent->HandleAttackInput();
	}
}

void ACombatCharacterBase::OnNextWeaponTriggered(const FInputActionValue& Value)
{
	if (CombatComponent)
	{
		CombatComponent->SwitchToNextWeapon();
	}
}

float ACombatCharacterBase::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (DamageHandlerComponent)
	{
		FDamageInfo DamageInfo;
		DamageInfo.BaseDamage = ActualDamage;
		DamageInfo.DamageCauser = DamageCauser;
		FHitResult EmptyHit;
		DamageHandlerComponent->ApplyDamage(DamageCauser, this, DamageInfo, EmptyHit);
	}

	return ActualDamage;
}
