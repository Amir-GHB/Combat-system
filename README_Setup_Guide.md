# Unreal Engine 5.8 C++ Modular Combat System Setup Guide

## 1. Module and character setup

Ensure the game module depends on EnhancedInput:

    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

ACombatCharacterBase creates a spring-arm follow camera and camera component, and implements camera-relative movement, look, jump, attack, and next-weapon input. In your character Blueprint, assign these assets in the **Input** category:

- **Default Mapping Context**
- **Move Action**: Vector2D (X = right/left, Y = forward/back)
- **Look Action**: Vector2D (X = yaw, Y = pitch)
- **Jump Action**: Digital
- **Attack Action**: Digital
- **Next Weapon Action**: Digital

Map these actions to your preferred keys/gamepad controls in the mapping context. Make sure the Player Controller possesses the character and uses Enhanced Input.

## 2. Weapon inventory

In the character's Combat Component defaults:
1. Add entries to **Weapon Classes**, keyed by EWeaponType.
2. Set **Weapon Cycle Order** to the order desired for next-weapon input.
3. Set **Default Weapon Type** to the starting weapon.
4. Configure each weapon Blueprint's WeaponConfig, including hand/holster sockets, damage values, and trace sockets.

The component spawns the configured weapons, initially attaches them to their holsters, then equips the default weapon. Missing equip/unequip montages fall back to immediate socket attachment.

## 3. Choose an attack montage mode per weapon

Each weapon uses exactly one of these modes. Keep the unused configuration empty to avoid confusion.

### Single Montage With Sections

1. Set **Attack Montage Mode** to **Single Montage With Sections**.
2. Assign **Attack Combo Montage**.
3. Add one montage section for each combo step (for example, Attack_01, Attack_02, Attack_03). The number of sections is discovered dynamically.
4. Clear automatic section links so the component controls transitions.
5. Place AnimNotify_SectionEntry at the start of each section and set its SectionName to the exact section name.

### One Montage Per Combo Step

1. Set **Attack Montage Mode** to **One Montage Per Combo Step**.
2. Populate **Individual Attack Montages** in combo order. The array index is the combo step, so no section names or hardcoded combo count are required.
3. Place AnimNotifyState_ComboWindow in each montage where the next attack may be buffered.
4. Place AnimNotifyState_MeleeTrace across the active damage frames of each montage.

For both modes, the combo window accepts one transition per window. Early attack input is buffered for Input Buffer Duration seconds. Individual-montage mode starts the next montage after the current one ends naturally; interrupted attacks do not continue the combo.

## 4. Animation-driven weapon switching

Each weapon can have a unique **Unequip Montage** and **Equip Montage** in its WeaponConfig.

- The outgoing weapon's **Unequip Montage** should include AnimNotify_WeaponSwapPoint set to **Holster Current Weapon** at the frame the weapon should move to its holster socket.
- The incoming weapon's **Equip Montage** should include AnimNotify_WeaponSwapPoint set to **Draw Target Weapon** at the frame it should move to its hand socket.
- The component validates the notify against the montage and current switch phase.
- If a montage or notify is missing, the component attaches the weapon at montage completion as a safety fallback, avoiding a permanently stuck SwappingWeapon state.

Switch requests made during attacks are queued when **Queue Weapon Switch During Attack** is enabled. The switch transaction uses generation tokens so callbacks from older montages cannot complete a newer switch.

## 5. Melee trace sockets

- **Character Skeleton**: configure socket names such as Hand_R_Socket and Holster_Back_Socket to match your skeleton.
- **Weapon Mesh**: add trace sockets such as Trace_Start, Trace_Mid, and Trace_End.
- Configure **Trace Socket Names**, **Trace Radius**, **Trace Collision Channel**, and optionally **Draw Debug Traces** per weapon.

The trace state is controlled by AnimNotifyState_MeleeTrace. Ensure the selected collision channel overlaps the target actors' collision responses.

## 6. Montage callback lifecycle fix

The component intentionally does **not** bind global UAnimInstance::OnMontageEnded or OnMontageBlendingOut listeners. It uses delegates bound to each montage playback and validates callbacks with an attack generation token. It does not use FAnimMontageInstance::GetInstanceId() or an ActiveMontageInstanceId field, which avoids the UE 5.8 compile issue and prevents stale global callbacks from resetting a newer attack.
