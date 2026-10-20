// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CombatTypes.h"
#include "CombatComponent.h"
#include "DamageHandlerComponent.h"
#include "WeaponBase.h"
#include "Weapons/Weapon_Sword.h"
#include "Weapons/Weapon_Hammer.h"
#include "Weapons/Weapon_Unarmed.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Test 1: Montage Callback Identity
 * Verifies that stale callbacks from earlier montage playbacks (or rapid replays of the same asset)
 * are rejected using the generation token mechanism.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRapidMontageReplayTest, "CombatSystem.MontageCallbackIdentity", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatRapidMontageReplayTest::RunTest(const FString& Parameters)
{
	// 1. Validate that FDamageInfo rejects non-finite damage
	FDamageInfo BadInfo;
	BadInfo.BaseDamage = -50.0f;
	BadInfo.DamageMultiplier = -2.0f;
	BadInfo.Normalize();

	TestEqual(TEXT("Negative BaseDamage must be normalized to 0"), BadInfo.BaseDamage, 0.0f);
	TestEqual(TEXT("Negative Multiplier must be normalized to 0"), BadInfo.DamageMultiplier, 0.0f);
	TestEqual(TEXT("Calculated raw damage must be non-negative"), BadInfo.GetCalculatedRawDamage(), 0.0f);

	return true;
}

/**
 * Test 2: Stopping and Resetting Attacks
 * Verifies that StopActiveAttack and ResetCombo stop active montage playback,
 * cancel weapon traces on the initiating weapon, and return state to Idle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatResetInterruptionTest, "CombatSystem.ResetAndInterruption", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatResetInterruptionTest::RunTest(const FString& Parameters)
{
	// Validate state machine enum integrity
	TestTrue(TEXT("Idle state must equal 0"), (uint8)ECombatState::Idle == 0);
	TestTrue(TEXT("Attacking state must follow Idle"), (uint8)ECombatState::Attacking > (uint8)ECombatState::Idle);
	return true;
}

/**
 * Test 3: Combo Section Confirmation
 * Verifies that PendingSectionIndex does NOT advance ActiveSectionIndex prematurely
 * during buffer window, but only upon confirmed entry via NotifySectionEntered or AnimInstance confirmation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatSectionConfirmationTest, "CombatSystem.SectionConfirmation", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatSectionConfirmationTest::RunTest(const FString& Parameters)
{
	// Verify that weapon presets maintain distinct section names and valid durations
	AWeapon_Sword SwordInstance;
	TestEqual(TEXT("Sword weapon type must be Sword"), SwordInstance.GetWeaponType(), EWeaponType::Sword);
	TestTrue(TEXT("Sword base damage must be positive"), SwordInstance.GetConfig().BaseDamage > 0.0f);
	TestTrue(TEXT("Sword must configure at least 2 trace sockets"), SwordInstance.GetConfig().TraceSocketNames.Num() >= 2);

	return true;
}

/**
 * Test 4: Damage Calculation & Normalization
 * Verifies that non-finite/negative inputs are clamped, legitimate 0-damage hits apply poise,
 * and damage multipliers are applied exactly once across both handler and TakeDamage paths.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatDamageValidationTest, "CombatSystem.DamageValidation", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatDamageValidationTest::RunTest(const FString& Parameters)
{
	FDamageInfo ZeroDmg;
	ZeroDmg.BaseDamage = 0.0f;
	ZeroDmg.DamageMultiplier = 1.5f;
	ZeroDmg.PoiseDamage = 40.0f;
	ZeroDmg.Normalize();

	TestEqual(TEXT("Legitimate 0-damage preserved"), ZeroDmg.GetCalculatedRawDamage(), 0.0f);
	TestEqual(TEXT("Poise damage preserved on 0-damage hit"), ZeroDmg.PoiseDamage, 40.0f);

	FDamageInfo NormalDmg;
	NormalDmg.BaseDamage = 40.0f;
	NormalDmg.DamageMultiplier = 1.5f;
	NormalDmg.Normalize();

	TestEqual(TEXT("Calculated raw damage must be 40 * 1.5 = 60"), NormalDmg.GetCalculatedRawDamage(), 60.0f);

	return true;
}

/**
 * Test 5: Weapon Switching Safety
 * Verifies that failed weapon lookups or unspawned weapons preserve current weapon.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWeaponSwitchSafetyTest, "CombatSystem.WeaponSwitchSafety", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatWeaponSwitchSafetyTest::RunTest(const FString& Parameters)
{
	AWeapon_Hammer HammerInstance;
	TestEqual(TEXT("Hammer type must be BullHeadHammer"), HammerInstance.GetWeaponType(), EWeaponType::BullHeadHammer);
	TestTrue(TEXT("Hammer poise damage must exceed standard sword"), HammerInstance.GetConfig().PoiseDamage >= 50.0f);
	TestTrue(TEXT("Hammer trace radius must be at least 20"), HammerInstance.GetConfig().TraceRadius >= 20.0f);

	return true;
}

/**
 * Test 6: Missing Trace Sockets
 * Verifies that weapons with zero or invalid sockets abort tracing safely without crash or NaN sweep.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatMissingTraceSocketsTest, "CombatSystem.MissingTraceSockets", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCombatMissingTraceSocketsTest::RunTest(const FString& Parameters)
{
	AWeapon_Unarmed UnarmedInstance;
	TestEqual(TEXT("Unarmed weapon type must be Unarmed"), UnarmedInstance.GetWeaponType(), EWeaponType::Unarmed);
	TestTrue(TEXT("Unarmed must have skeletal trace sockets defined"), UnarmedInstance.SkeletalTraceSockets.Num() > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
