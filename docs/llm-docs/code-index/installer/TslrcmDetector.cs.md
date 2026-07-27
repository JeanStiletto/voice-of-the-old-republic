# TslrcmDetector.cs (62 lines)

Static detector for an existing TSLRCM install, used to gate K2CP / Tweak Pack ordering (they must install after TSLRCM). Scans the uninstall registry key across both hives (`LocalMachine`, `CurrentUser`) and both registry views (32/64-bit) for a `DisplayName` containing "Sith Lords Restored Content" — the entry TSLRCM's Inno Setup installer registers. A miss is treated conservatively as "not installed", which then skips the dependent mods rather than risking wrong install order. Flagged as an UNVERIFIED assumption pending first live test (that TSLRCM 1.8.6's Inno script actually registers this entry); fallback plan noted is a file marker in the game folder.

## Declarations (in source order)

- L19 — `public static class TslrcmDetector`
- L21 — `private const string UninstallKeyPath`
- L24 — `public static bool IsInstalled()`
  note: loops hive x view x subkey; any exception per-key is swallowed so scanning continues
