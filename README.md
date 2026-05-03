# ReaLlm

## REAPER Low latency monitoring plug-in extension

Cubase/Logic style low latency monitoring mode for REAPER. While enabled, restricts PDC latency ('plugin delay') to one block/buffer size by bypassing plugins from input monitored signalchains. Re-enables plugins once signalchain is no longer input monitored, or ReaLlm is disabled. While ReaLlm is enabled, disabled plugins can be re-enabled manually, and ReaLlm will consider them 'safed' and leave them untouched, unless they're again manually re-disabled. Remembers 'safed' plugins per REAPER project. Leaves already disabled plugins untouched. Track with names containing "Llm" are treated as monitored inputs.

Use `ReaLlm: Low latency monitoring` REAPER action list toggle to enable/disable low latency monitoring. Or map a toolbar, keyboard and/or MIDI shortcut for the action.

Also provides REAPER API functions for ReaScripting.
API documentation available in REAPER Help > ReaScript documentation...

### CMake presets (Windows)

The shared `windows-msvc` configure preset intentionally does not pin a Visual Studio generator version.
Use either Visual Studio or Ninja:

Visual Studio (choose installed version at configure time):

```powershell
cmake --preset windows-msvc -G "Visual Studio 17 2022"
cmake --build --preset windows-msvc-release
```

Ninja:

```powershell
cmake --preset windows-msvc-ninja-release
cmake --build --preset windows-msvc-ninja-release
```

### Installation

Install from [ReaPack](https://reapack.com) or download [latest release](https://github.com/ak5k/reallm/releases/latest) for your computer architecture (e.g. `x64.dll` for 64-bit Windows PC or `arm64.dylib` for M1 Mac) and place it in your REAPER `UserPlugins` directory.

![image](https://i.imgur.com/iKHyQXb.gif)