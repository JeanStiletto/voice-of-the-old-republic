# IntroMovieDisabler.cs (243 lines)

Renames the three launch-time intro `.bik` files (biologo, leclogo, legal) to `.bik.disabled` so the engine's `PlayMoviesAsync` queue fails to open them and falls through to the main menu — cuts 10-20s off cold start and eliminates an engine bug where alt-tabbing during the intro restarts the entire intro sequence (30-60s of perceived-stuck time for a blind user). In-game scripted cutscenes go through a different engine path (`PlayMovie`/`ExecuteCommandPlayMovie`) and are unaffected. Chosen over the `[Game Options] Disable Movies=1` ini flag because that flag kills story cutscenes too. Both `DisableIntros` and `RestoreIntros` are idempotent and self-healing (clean up a stray file left in the other state from a prior partial run). The runtime mod-settings toggle in menus_modsettings.cpp reuses this same filesystem state as its persistence layer.

## Declarations (in source order)

- L33 — `public static class IntroMovieDisabler`
- L42 — `IntroFiles` — biologo.bik, leclogo.bik, legal.bik
  note: verified against swkotor.exe's string table; the three literals sit together in .data
- L49 — `public sealed class Result { Success, Error, Renamed, AlreadyDone, Missing }`
- L63 — `static Result DisableIntros(string gameDir)` — renames each to `.disabled`; already-disabled or missing counted gracefully, not as errors
- L156 — `static Result RestoreIntros(string gameDir)` — reverse of DisableIntros; used by the uninstaller
