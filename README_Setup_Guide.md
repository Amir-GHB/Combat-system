# Unreal Engine 5.8 C++ Modular Combat System Setup Guide

## 1. Module Setup
Ensure `EnhancedInput` is added to your project's `.Build.cs`:
```csharp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
```

## 2. Dynamic Montage Setup (Scalable to ANY Number of Sections)
1. Open your attack animation montage.
2. In the **Montage Sections** tab, create sections for each combo attack (e.g. `Attack_01`, `Attack_02`, `Attack_03`).
3. **Critical**: In the section manager, click **Clear** on all section links so they do not auto-advance. Our C++ controller dynamically links `Montage_SetNextSection` when an input buffer is consumed.

## 3. Placing Anim Notifies
On the **Notifies** track:
1. **Section Entry**: Add `AnimNotify_SectionEntry` at frame 0 of each section and set `SectionName` to match the montage section name.
2. **Combo Window**: Add `AnimNotifyState_ComboWindow` across the window where subsequent attacks can be buffered (e.g. 40% – 75%).
3. **Melee Trace**: Add `AnimNotifyState_MeleeTrace` across active damage frames.

## 4. Setting Sockets
- **Character Skeleton**: `Hand_R_Socket`, `Holster_Back_Socket`, `Holster_Hip_Socket`, `Hand_R_Fist`, `Hand_L_Fist`.
- **Weapon Meshes**: Add `Trace_Start`, `Trace_Mid`, `Trace_End`.
