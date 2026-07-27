# LogsCommand.cs (89 lines)

`kdev logs [--follow]` — tails the newest `*.log` file under `config.LogsDir` (`logs/` at project root, kdev's own build/apply/launch logs — NOT the game's or the patch DLL's logs). Opens with `FileShare.ReadWrite` so a concurrent writer isn't blocked. `--follow` polls every 250ms for new lines until Ctrl+C.

## Declarations (in source order)

- L6 — `static class LogsCommand`
- L8 — `Command Build()` — `--follow` option
- L24 — `int Run(bool follow)` — finds newest log by `LastWriteTimeUtc`, prints existing content, then optionally follows
