# LaunchCommand.cs (115 lines)

`kdev launch [--monitor]` — starts `swkotor.exe` directly via `Process.Start` (`UseShellExecute=true`); the `dinput8.dll` proxy dropped by `kdev apply` auto-loads KotorPatcher, so no separate launcher process is needed. Refuses to launch (exit 6) if the proxy DLL isn't present at the game install root, since that would silently produce an unmodded session. Pins the process to CPU core 0 twice (immediately and after a 5s settling delay, since some library resets affinity during init) to stop KOTOR's `Sleep(0)`-spinning main loop from pegging a core. `--monitor` blocks on `WaitForExit` and returns the game's real exit code; `DevCommand` always calls with `monitor:true`.

## Declarations (in source order)

- L7 — `static class LaunchCommand`
- L9 — `Command Build()` — `--monitor` option
- L25 — `int Run(bool monitor)` — checks `dinput8.dll` proxy exists, starts the process, pins affinity (initial + after-5s), optionally waits and returns the real exit code
- L96 — `void TryPin(int pid, IntPtr mask, string label)` — sets `Process.ProcessorAffinity`, tolerant of the process having already exited
