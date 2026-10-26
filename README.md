# Jade Empire Controller Fix

Controller improvements for the Windows version of **Jade Empire: Special Edition**.
The mod restores independent camera orbit and camera-relative exploration movement,
and adds XInput support, controller glyphs, menu navigation and rumble fixes.

## Features

- Independent right-stick camera orbit
- Analog camera-relative movement and smooth character facing during exploration
- D-pad menu navigation and separate left/right trigger actions
- Xbox glyphs for English controller instructions
- Vibration support
- Optional right-stick target switching while locked on in combat

- Native keyboard/mouse movement and camera behavior when `ControllerOnly` is enabled


## Compatibility

- Tested for both the Steam and GOG versions of the game
- Only tested for XInput controllers
- Not tested with other mods

## Installation

1. Download the latest version from releases
2. Place `dinput8.dll` and `JadeEmpireControllerFix.ini` beside `JadeEmpire.exe`
   (e.g., C:\GoG Games\Jade Empire)
3. if playing on Linux, add WINEDLLOVERRIDES="dinput8=n,b" %command% to your Steam launch options (untested)

On first launch, the JadeEmpire.ini will add the below bindings for your controller:
```ini
Movement L/R=Left StickX
Movement U/D=Left StickY
Look L/R=Right StickX
Look U/D=Right StickY
Menu=Button 8
Combat Pause=Button 7
Fight Style 1=Hat 1
Fight Style 2=Hat 1
Fight Style 3=Hat 1
Fight Style 4=Hat 1
Attack=Button 1
Block=Button 2
Strong Attack=Button 3
Focus=Button 4
Chi Strike=Button 6
Chi Heal=Button 5
Target Left=Right StickZ
Target Right=Right StickZ
No Target=-
Free Look=Button 10
```

This puts Menu on Start, Combat Pause on Back, Chi Strike on RB and Chi Heal on LB.
Both target bindings using `Right StickZ` is intentional: the mod supplies their
independent left/right trigger values. `No Target=-` leaves that separate binding
unassigned; both triggers together provide the game's  regular Free Target action.

Camera sensitivity can be adjusted in the game's menu. 

To uninstall, remove `dinput8.dll` from your game directory.

## Configuration

Edit `JadeEmpireControllerFix.ini` beside the DLL and restart the game. Note that it is 
separate from the game's `JadeEmpire.ini`.

| Section | Option | Default | `1` | `0` |
| --- | --- | --- | --- | --- |
| Controls | `ControllerOnly` | `1` | Limit exploration movement/camera changes to controller input. | Apply them to keyboard/mouse as well (not extensively tested). |
| Controls | `RightStickTargetSwitching` | `1` | Horizontal right-stick flicks switch locked combat targets (similar to modern souls likes). | Keep native right-stick camera input. |
| Controls | `InvertLookX` | `0` | Invert right-stick horizontal camera look. | Normal horizontal look. |
| Controls | `InvertLookY` | `0` | Invert right-stick vertical camera look. | Normal vertical look. |
| Interface | `ControllerPrompts` | `1` | Show controller glyphs and English instructions. | Keep original text and numbered button labels. |
| Feedback | `Rumble` | `1` | Enable XInput vibration. | Disable mod XInput vibration. |

The `ControllerOnly` option affects exploration movement and camera, not menu input
or target switching. 

Target flicks require returning the stick to center before the next switch. Physical trigger targeting 
remains available with either option value.

Keyboard/mouse activity takes priority during simultaneous use.

With `ControllerPrompts=1`, prompts use the game's controller glyphs. Matching
`override/*.txb` textures take precedence over the base artwork. With
`ControllerPrompts=0`, prompts and glyphs retain vanilla game behavior.

## Building

Requirements: Windows, Visual Studio 2022 C++ Build Tools, and a Windows SDK.
The script locates the tools through `vswhere` and builds **32-bit C++17** code
with warnings treated as errors and the C/C++ runtime linked statically.

```powershell
.\tools\build.ps1
```

Output is written to `build/`. Use `-OutputDirectory` for another directory.
Every runtime header is checked in isolation and each `.cpp` is compiled
independently. The build runs movement math, controller INI setup, synthetic
texture-decoding and system DirectInput forwarding tests. It does not require game files.

## Known limitations

- English prompt replacements only; other languages have not been verified.
- Existing menu layouts and labels outside the shared text hooks remain native.
- Switching from an already-selected XInput device to DirectInput requires a restart.

## Licence

The mod source uses the Mozilla Public License 2.0. See `LICENSE` for the terms.
Jade Empire and its game assets remain the property of their respective owners.
