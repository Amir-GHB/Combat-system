// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatCharacterBase.generated.h"

class UCombatComponent;
class UDamageHandlerComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class COMBATSYSTEM_API ACombatCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ACombatCharacterBase();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDamageHandlerComponent> DamageHandlerComponent;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> NextWeaponAction;

	void OnAttackTriggered(const FInputActionValue& Value);
	void OnNextWeaponTriggered(const FInputActionValue& Value);

public:
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
};
