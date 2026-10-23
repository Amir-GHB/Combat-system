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
5. Add **Weapon_Spear** under **Spear** for a melee spear starting point. **Weapon_Bow** is a rigged-bow ranged weapon with hold-to-draw input, projectile firing, and configurable distance falloff. Add either class to **Weapon Classes** to make it available, then adjust **Weapon Cycle Order** as desired.

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
3. Place AnimNotifyState_ComboWindow over the frames where the next attack input is accepted.
4. Place the single AnimNotify **Combo Transition Point** at the exact frame where the next montage should take over. Put it after the combo window and before any idle/recovery tail that should be skipped when chaining.
5. Place AnimNotifyState_MeleeTrace across the active damage frames of each montage.

For both modes, the combo window accepts one transition per window. Early attack input is buffered for Input Buffer Duration seconds. In individual-montage mode, a valid input queues the next array entry, and **Combo Transition Point** starts it at the authored frame, blending/interruption of the outgoing montage instead of waiting through its idle tail. If the transition notify is absent, the queued step falls back to starting when the current montage ends naturally. Interrupted attacks do not automatically continue the combo.

## 4. Animation-driven weapon switching

Each weapon can have a unique **Unequip Montage** and **Equip Montage** in its WeaponConfig. In the weapon Blueprint defaults, **bPlayUnequipMontageOnSwitch** controls whether that weapon's outgoing unequip animation plays; disabling it skips directly to the holster step and still plays the incoming weapon's equip montage. **bNoHolster** detaches and hides the weapon while it is unequipped, instead of attaching it to a holster socket.

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

## 6. Rigged bow and projectile setup

1. Add **Weapon_Bow** to the Combat Component's **Weapon Classes** map under **Bow**.
2. Assign the bow's rigged skeletal mesh to **Skeletal Bow Mesh**. Its skeletal mesh can use its own AnimBP/Control Rig for string stretch and limb bending.
3. Add an **Arrow_Spawn_Socket** to the bow skeleton at the arrow's release point, or set **Arrow Spawn Socket Name** to match your socket. If no socket is found, the system safely falls back to a character-view-based spawn position.
4. Optionally assign **Draw Montage** and **Release Montage**. Both are optional; missing montages do not disable drawing or projectile firing.
5. The attack input's **Started** event begins drawing, **Completed** releases a shot, and **Canceled** cancels without firing. A weapon switch during a draw also cancels the draw.
6. Use **Full Draw Time** to define charge duration. **Allow Quick Shot** permits a tap to fire at low charge; otherwise, releasing before **Minimum Draw Time** cancels the shot.
7. In the bow skeletal mesh's AnimBP/Control Rig, read the owning **Weapon_Bow** actor's **DrawAlpha** (0..1) to drive the rigged string and bow limbs. **bIsDrawing** indicates whether the player is currently holding the draw.
8. Configure **Projectile Class** and **Projectile Config**. Assign an arrow mesh to the projectile Blueprint's optional **Projectile Visual** component. Projectile settings include speed, gravity, radius, lifetime, damage, and falloff mode:
   - **No Falloff** keeps damage constant over distance.
   - **Linear Falloff** interpolates from full damage at **Falloff Start Distance** to **Minimum Damage Multiplier** at **Falloff End Distance**.
   - **Curve Falloff** samples **Falloff Curve** using normalized distance from 0 at falloff start to 1 at falloff end.
9. Charge strength scales projectile damage and speed using **Min/Max Draw Damage Multiplier** and **Min/Max Draw Speed Multiplier**. Bow damage values come from its **WeaponConfig**; projectile movement and falloff come from **Projectile Config**.

The projectile routes hits through **DamageHandlerComponent** when available and falls back to Unreal's TakeDamage path otherwise. The projectile is destroyed on impact or when its configured lifetime expires. No ammo/inventory system is included in this first pass.

## 7. Montage callback lifecycle fix

The component intentionally does **not** bind global UAnimInstance::OnMontageEnded or OnMontageBlendingOut listeners. It uses delegates bound to each montage playback and validates callbacks with an attack generation token. It does not use FAnimMontageInstance::GetInstanceId() or an ActiveMontageInstanceId field, which avoids the UE 5.8 compile issue and prevents stale global callbacks from resetting a newer attack.
