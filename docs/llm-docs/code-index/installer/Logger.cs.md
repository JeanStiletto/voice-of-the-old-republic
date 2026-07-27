# Logger.cs (113 lines)

Simple static logger that buffers all installer progress in memory and writes to `KotorAccessibility_Install.log` on the user's Desktop only when explicitly flushed (on error, or if the user opts in). `AskAndSave` prompts via MessageBox unless `alwaysAsk` — errors always prompt, otherwise silent. Every other class in the installer (`InstallationManager`, `GitHubClient`, forms, etc.) logs through this.

## Declarations (in source order)

- L11 — `public static class Logger`
- L17 — `static Logger()` — LogPath = Desktop\KotorAccessibility_Install.log
- L23 — `static string GetLogPath()`
- L24 — `static bool HasErrors`
- L26 — `static void Info(string message)`
- L27 — `static void Warning(string message)`
- L29 — `static void Error(string message)` — sets HasErrors
- L35 — `static void Error(string message, Exception ex)` — also logs stack trace
- L42 — `static void Log(string level, string message)` — timestamped, appended to in-memory buffer + Console.WriteLine
- L50 — `static void Flush()` — writes the full buffered log to disk
- L74 — `static bool AskAndSave(bool alwaysAsk = false)` — MessageBox Yes/No; only prompts if HasErrors or alwaysAsk
- L96 — `static void OpenLogFile()` — shell-opens the log file
