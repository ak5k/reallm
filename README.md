# ReaLlm

## REAPER Low latency monitoring plug-in extension

Cubase/Logic style low latency monitoring mode for REAPER. While enabled, restricts PDC latency ('plugin delay') to one block/buffer size by bypassing plugins from input monitored signalchains. Re-enables plugins once signalchain is no longer input monitored, or ReaLlm is disabled. While ReaLlm is enabled, disabled plugins can be re-enabled manually, and ReaLlm will consider them 'safed' and leave them untouched, unless they're again manually re-disabled. Remembers 'safed' plugins per REAPER project. Leaves already disabled plugins untouched.

Provides following REAPER API functions for ReaScripting:

```
Llm_Do()
Llm_Get()
Llm_Set()
```

Install from [ReaPack](https://reapack.com) or download [latest release](https://github.com/ak5k/reallm/releases/latest) for your computer architecture (e.g. `_x64.dll` for 64-bit Windows PC or `_arm64.dylib` for M1 Mac) and place it in your REAPER `UserPlugins` directory.

Once installed, API documentation available in REAPER Help > ReaScript documentation...

Use `ReaLlm: Low latency monitoring` REAPER action list toggle to enable/disable low latency monitoring. Or map a toolbar, keyboard and/or MIDI shortcut for the action.

[More information](https://forum.cockos.com/showthread.php?t=245445)

![image](https://i.imgur.com/iKHyQXb.gif)
