# Development

## Source layout

| Files in `src/` | Responsibility |
| --- | --- |
| `dinput8_proxy.cpp`, `dinput8.def` | Forward the six system DirectInput exports and initialise at the verified game call site. |
| `xbox_controls.cpp/.hpp` | Validate/install hooks; exploration movement, facing, camera collision and menu navigation. |
| `xinput_backend.cpp/.hpp` | Controller sampling, device ownership, focus/reconnect handling and rumble lifecycle. |
| `controller_bindings.cpp/.hpp` | First-use XInput defaults, INI persistence and preservation of user remaps. |
| `combat_stick.cpp/.hpp` | Convert deliberate horizontal stick flicks into native target actions. |
| `controller_prompts.cpp/.hpp` | Resolve action bindings and replace controller text through the native text hooks. |
| `prompt_templates.cpp/.hpp` | Maintained English instruction templates keyed by TLK entry ID. |
| `controller_glyphs.cpp/.hpp` | Load artwork and upload glyphs into the native font cache. |
| `glyph_assets.cpp/.hpp` | Read KEY/BIF/override textures, decode DXT1/DXT5 and resample glyphs. |
| `control_math.cpp/.hpp` | Camera-relative direction calculations, independent of game memory. |
| `mod_options.cpp/.hpp` | Load and validate the mod INI once at startup. |
| `game_memory.hpp` | Templates for the verified game's byte offsets and virtual-method slots. |

Headers contain declarations, types, templates and small inline constants.
Ordinary function bodies and module state belong in `.cpp` files. Headers include
their own dependencies and use `#pragma once`; each implementation includes its
matching header first when it has one. The export-only proxy uses `dinput8.def`.
These boundaries follow the [C++ Core Guidelines on source files](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-source).

The code targets MSVC x86: naked trampolines, inline assembly and calling
conventions deliberately match the game's ABI. Do not replace these with portable
wrappers without checking the caller's registers, stack layout and return address.
Comments beside hooks document those constraints and the relevant native addresses.

Use the checked-in `.clang-format` (clang-format 19) and `.editorconfig` for layout.
Comments should explain contracts, invariants or non-obvious game behavior.
Development chronology belongs in version control rather than source comments.

## Tests

`tools/build.ps1` builds and runs four standalone tests:

- `tests/control_math.cpp`: movement directions, speed and invalid inputs.
- `tests/controller_bindings.cpp`: first-use defaults, persisted remaps, profile isolation, invalid entries and failed writes using a temporary INI.
- `tests/glyph_assets.cpp`: synthetic texture decoding and archive-path validation.
- `tests/proxy_forwarding.cpp`: forwarding to the system DirectInput library.

These checks use generated inputs and the Windows DirectInput library.
The script also checks each runtime header in isolation before compiling the DLL.

XInput setup runs once when the backend claims the game's primary controller.
It uses the game's native INI path and controller section, writing only the twenty
controller bindings and `ControllerFixBindingsInitialized=1`. The marker is written
after the defaults succeed; subsequent launches load saved bindings and repair
only missing or invalid entries. Native binding-menu saves retain the marker.
An XInput connection without a native device name uses the `XInput Controller`
section, including for later native menu saves. No per-frame INI I/O is performed.

## Runtime constraints

`DllMain` performs no work. Installation occurs synchronously at the game's
DirectInput creation call, after unpacking and before input polling. Every patch
signature is checked before any write; page protections are restored in reverse
order because multiple hooks may share a page. The installed DLL remains pinned
until process exit.

The DLL exports only the six DirectInput functions listed in `src/dinput8.def`.
Hook installation is an internal function called once by the proxy.

Prompt IDs and `{n}` placeholders refer to native text entries and controller
actions. Keep placeholders binding-aware. Leave unrelated dialogue and unsupported
bindings on the native text path, and preserve voice metadata and dynamic save names.
