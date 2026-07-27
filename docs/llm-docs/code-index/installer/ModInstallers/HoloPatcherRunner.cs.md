# HoloPatcherRunner.cs (164 lines)

Static helper shared by `K1cpInstaller`, `K2cpInstaller`, and `TweakPackInstaller` that drives a single headless HoloPatcher install (`--game-dir --tslpatchdata --install`, no `--console`) as a child process, forwarding throttled stdout lines plus a heartbeat to a `statusUpdate` callback. Caller supplies locale-format keys (`progressFormatKey`, `heartbeatFormatKey`) resolved via `InstallerLocale.Format` so each mod's status strings stay mod-prefixed. 10-minute timeout kills the process tree and reports failure.

## Declarations (in source order)

- L16 — `internal static class HoloPatcherRunner`
- L21 — `private const int ForwardThrottleMs = 2500` — max stdout-forward rate to avoid screen-reader interruption spam
- L26 — `private const int HeartbeatMs = 5000` — heartbeat tick when HoloPatcher goes quiet
- L28 — `public static async Task<(bool Success, string Error)> RunAsync(string holoPatcherExe, string gameDir, string tslpatchdataDir, Action<string> statusUpdate, string progressFormatKey, string heartbeatFormatKey)`
  note: `lastForwardTicks` (Interlocked, monotonic TickCount64) coordinates stdout-forward vs. heartbeat so they never both fire in the same quiet window
  note: after `WaitForExitAsync` returns, calls synchronous `proc.WaitForExit()` per MS docs so in-flight Output/ErrorDataReceived events flush before buffers are read
  note: 10-minute `CancellationTokenSource` kills the process tree (`entireProcessTree: true`) on timeout
