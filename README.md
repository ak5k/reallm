# ReaLlm
## REAPER Low latency monitoring plug-in extension

Cubase/Logic style low latency monitoring mode for REAPER. While enabled, restricts PDC latency ('plugin delay') to one block/buffer size by bypassing plugins from input monitored signalchains. Re-enables plugins once signalchain is no longer input monitored, or ReaLlm is disabled. While ReaLlm is enabled, disabled plugins can be re-enabled manually, and ReaLlm will consider them 'safed' and leave them untouched, unless they're again manually re-disabled. Remembers 'safed' plugins per REAPER project. Leaves already disabled plugins untouched.

Registers `ReaLlm: Low latency monitoring` REAPER action list toggle for enabling/disabling low latency monitoring.

Registers following REAPER API functions for scripting:
```
Llm_Do
Llm_Get
```
Once installed, documentation available in REAPER Help > ReaScript documentation...

[More information](https://forum.cockos.com/showthread.php?t=245445)

![image](https://i.imgur.com/iKHyQXb.gif)
