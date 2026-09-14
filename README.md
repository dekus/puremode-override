# puremode-override

FiveM `sv_pureLevel` client-side override with a modern Win32 GUI.

Scans the running FiveM game process for the `sv_pureLevel` global across every
non-system module, writes the chosen value to every instance and holds them
with a persistent guard that re-writes every 100 ms — so the game never sees a
non-`-1` value regardless of which internal code path checks it.

## Preview

- Custom borderless window (drag-to-move, minimize / close)
- Dark purple palette, gradient title bar, real-time log
- Numeric input with `-` sign support, defaults to `-1`
- Single `APPLY` button; the guard arms automatically on a successful write

## Build

Requirements:

- Windows 10 / 11 x64
- Visual Studio 2022 (v143 toolset)
- Windows 10 SDK

Create an empty **Windows Desktop Application** project targeting **Release |
x64**, drop the source files in, and set the following in project properties:

**C/C++**
- Language / C++ Language Standard → `stdcpplatest`
- Code Generation / Runtime Library → `Multi-threaded (/MT)`
- Code Generation / Security Check → `Disabled (/GS-)`
- Code Generation / Control Flow Guard → `false`
- Code Generation / Spectre Mitigation → `Disabled`
- General / Warning Level → `Turn Off All Warnings (/W0)`
- Preprocessor Definitions: `NDEBUG;_WINDOWS;_MBCS;_CRT_SECURE_NO_WARNINGS;WIN32_LEAN_AND_MEAN`
- General / Debug Information Format → `None`
- General / Additional Include Directories → `$(ProjectDir)`

**Linker**
- System / SubSystem → `Windows`
- Debugging / Generate Debug Info → `false`
- Manifest File / UAC Execution Level → `requireAdministrator`
- Input / Additional Dependencies: `kernel32.lib;user32.lib;gdi32.lib;msimg32.lib;shell32.lib`

**General**
- Character Set → `Multi-Byte`
- Output Directory → `$(ProjectDir)bin\`
- Intermediate Directory → `$(ProjectDir)bin\int\`

Build. Output lands at `bin\pure-mode-override.exe`.

## Usage

1. Launch FiveM and reach the server list / connecting screen.
2. Run `pure-mode-override.exe` (as administrator).
3. The tool waits for the FiveM game window, resolves every target across the
   loaded modules and reports **Ready**.
4. Leave the field at `-1` (or type `0`), press **APPLY**.
5. On success the log shows **Applied** and the persistent guard keeps the
   value in place until you close the tool.

## Layout

```
pure-mode-override/
    main.cpp
    ui/
        ui.hpp
        ui.cpp
    patch/
        patch.hpp
        patch.cpp
```

No external dependencies. Pure Win32 + GDI, C++ latest, single-file per module.

## Credits

Built by **[Deku](https://github.com/dekus)**.

## Disclaimer

Educational and research use only. This tool modifies memory of a live game
process; understand what you are doing before running it. Do not use it to
attack or interfere with services you are not authorized to test. No support
is provided.
