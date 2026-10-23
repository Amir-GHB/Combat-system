// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "Weapons/Weapon_Bow.h"

AWeapon_Bow::AWeapon_Bow()
{
	WeaponConfig.WeaponType = EWeaponType::Bow;
	WeaponConfig.WeaponDisplayName = FText::FromString(TEXT("Bow (Placeholder)"));
	WeaponConfig.BaseDamage = 0.0f;
	WeaponConfig.PoiseDamage = 0.0f;
	WeaponConfig.KnockbackImpulse = 0.0f;
	WeaponConfig.DamageCategory = EDamageCategory::Piercing;
	WeaponConfig.HandSocketName = FName("Bow_Hand_Socket");
	WeaponConfig.HolsterSocketName = FName("Bow_Holster_Socket");
	WeaponConfig.TraceSocketNames.Empty();
}
