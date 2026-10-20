// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "DamageHandlerComponent.h"
#include "GameFramework/Actor.h"

UDamageHandlerComponent::UDamageHandlerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDamageHandlerComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;
	CurrentPoise = MaxPoise;
	bIsDead = false;
}

float UDamageHandlerComponent::ApplyDamage_Implementation(AActor* InstigatorActor, AActor* TargetActor, const FDamageInfo& DamageInfo, const FHitResult& HitResult)
{
	if (bIsDead) return 0.0f;

	// 1. Calculate raw damage using canonical formula
	const float RawDamage = DamageInfo.GetCalculatedRawDamage();

	// If raw damage is 0 or negative (e.g. non-damaging utility or invulnerable hit), return 0.
	// Do not artificially clamp to 1.0f.
	if (RawDamage <= 0.0f)
	{
		return 0.0f;
	}

	// 2. Armor mitigation formula: FinalDamage = RawDamage * (100 / (100 + Armor))
	const float MitigationFactor = 100.0f / (100.0f + FMath::Max(0.0f, Armor));
	const float FinalDamage = FMath::Max(0.0f, RawDamage * MitigationFactor);

	// 3. Poise reduction
	CurrentPoise = FMath::Clamp(CurrentPoise - DamageInfo.PoiseDamage, 0.0f, MaxPoise);

	// 4. Health deduction
	CurrentHealth = FMath::Clamp(CurrentHealth - FinalDamage, 0.0f, MaxHealth);

	// 5. Broadcast delegates
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnDamageTaken.Broadcast(FinalDamage, InstigatorActor, DamageInfo, HitResult);

	// 6. Death detection
	if (CurrentHealth <= 0.0f && !bIsDead)
	{
		bIsDead = true;
		OnDeath.Broadcast(InstigatorActor);
	}

	return FinalDamage;
}

void UDamageHandlerComponent::Heal(float Amount)
{
	if (bIsDead || Amount <= 0.0f) return;

	CurrentHealth = FMath::Clamp(CurrentHealth + Amount, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}
