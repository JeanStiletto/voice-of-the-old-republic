# Contributing

Thanks for wanting to help make KOTOR 1 accessible. This file covers the dev setup, the inner loop, and the conventions that aren't obvious from reading the code.

If anything in here is wrong, file an issue or PR. The doc is meant to stay short and current.

## Before you start

Read `ARCHITECTURE.md` first. It is shorter than this file and will save you re-reading code you would otherwise have to. Then skim `docs/known-issues.md` to see what's on the backlog — there's often a Planned item that matches what you want to do.

The mod author is blind. Pull requests that introduce mouse-only verification steps, GUI-only tools, or "look at this screenshot" workflows will need a screen-reader-accessible alternative before they can be reviewed.

## Dev setup

You'll need:

- **Windows 10 or 11** (the patch is a 32-bit Windows DLL injected into a 32-bit Windows game).
- **Visual Studio Build Tools 2022** with the C++ desktop workload (`cl.exe` for x86). The IDE is not required.
- **.NET 10 SDK** for `kdev`.
- **.NET 8 SDK** if you'll touch the installer.
- **KOTOR 1 Steam install** at `C:\Program Files (x86)\Steam\steamapps\common\swkotor`. GoG is supported by the framework but Steam is the primary target.
- **Lane Dibello's Kotor-Patch-Manager release** extracted somewhere. Path goes into `kdev.toml` under `[patch_manager].release`.
- **JDK 21 + Ghidra 12.x** if you'll do reverse-engineering. Optional; `kdev` doesn't need it. See `docs/tools.md` for paths.
- **NVDA** (free) or another Tolk-supported screen reader for testing speech output. SAPI works too; install Microsoft's mobile voices for non-English testing.

Check `docs/tools.md` for the canonical install paths and version constraints — `kdev` and the build script assume those paths.

## The inner loop

Edit a `.cpp` / `.h` under `patches/Accessibility/`, then:

```
kdev dev
```

That runs **clean → build → apply → launch**. The game starts with your changes injected. Patch logs land in `<install>/logs/patch-<utc>.log` and `<repo>/logs/`. Tail with:

```
kdev logs --follow
```

Other commands (when the full cycle is overkill):

- `kdev build` — compile only.
- `kdev apply` — install the most recent build into the game.
- `kdev launch` — start the game with the current install. Skip `--monitor`; it has a known crash-after-inject bug (memory entry `project_kdev_launch_monitor_bug`). Read the patch log directly.
- `kdev kill` — force-terminate the running game (use sparingly; prefer Alt+F4 from in-game).
- `kdev status` — sanity-check the project + game install paths.
- `kdev analyze-dump <path-to-wer.dmp>` — extract registers / module list / faulting address from a Windows Error Reporting dump. Useful for crash post-mortems.

## Conventions

### Code style

- Match what's already in `patches/Accessibility/`. Look at neighbouring files before introducing a pattern.
- File-name prefix groups the role: `engine_*` for read-side engine bindings, `menus_*` for per-screen GUI work, `audio_*` for audio, `combat_*`, `guidance_*`, `map_*`, etc. Pick the right prefix when you add a file.
- Headers are kept narrow. The convention is one public namespace (`acc::interact`, `acc::view_mode`, …) per pair, public functions in the header, everything else in the `.cpp`.
- Loader-lock-safe in `DllMain` only. Anything that loads DLLs, opens files, or initialises COM has to be deferred to a "first hook fire" path. See `core_dllmain.cpp` for the canonical lazy-init pattern.
- Strings: never hardcode user-facing literals in handler code. Add an `Id` to `strings.h` and an entry to every locale table (`strings_en.cpp`, `strings_de.cpp`, ...). Handlers call `strings::Get(Id)` and stay language-agnostic. Log lines stay English.

### Comments

- Default to no comments. Only write one when the *why* is non-obvious (a hidden engine invariant, a workaround for a known framework bug, a non-obvious offset).
- Don't restate what the code says. `// downcast to CSWGuiButton` next to `AsButton(ctrl)` is dead weight.
- When a comment references a finding, point to a memory entry (`project_*` / `feedback_*`) or a `docs/*.md` page. If you've shipped a fix, prefer the commit message + the code over a comment.

### Hooks

- Mid-function with register-source args. The framework's `source = "esp+X"` path has a known LEA-vs-MOV bug; use `source = "ebx"` / `"edi"` / etc. instead.
- Find the cut point with `tools/ghidra-scripts/DumpBytes.java` (headless Ghidra).
- Single addresses go in `hooks.toml`. Per-version variants split into per-version files later if Steam diverges from GoG.
- After landing a hook, log the trigger fires unconditionally for the first few sessions. Don't rate-limit diagnostic logs (memory entry `feedback_log_no_rate_limits`). Full fidelity beats file size.

### Speech rules

- Default: `prism::Speak(text)`, queued. Per-channel dedup via `SpeakIfChanged` for high-frequency listbox / focus events.
- Urgent (interrupts and survives NVDA typed-char cancel): `prism::SpeakUrgent(text)` — routes through SAPI.
- Never silence a fallback. Generic "control N" / "row N" placeholders bypass dedup and throttle by design (memory: `feedback_never_silence_fallback_announcement`). If you don't have a name, queue the placeholder rather than drop.

### Engine reads

- Verify offsets against the live binary before trusting Lane's headers. The headers are mostly right but occasionally lag the binary; SARIF / decompile is the next checkpoint.
- Down-cast via vtable for typed reads. `vtable[20] = AsLabel`, `vtable[22] = AsButton`, etc. See `engine_panels.cpp` for the canonical helpers.

## Testing

There is no automated UI test harness. The build does a clean compile check, and that's the only mechanical gate.

Real testing is **in-game with a screen reader**, listening:

1. Make the change.
2. `kdev dev`.
3. Reproduce the scenario you targeted (load the matching save / area).
4. Listen.
5. Check `<install>/logs/patch-*.log` for unexpected fire patterns.

Code that compiles cleanly but produces no audible result is *not done*. Don't commit untested code (memory: `feedback_no_untested_commits`). If the change cannot be tested in-game (refactor, dead-code drop, build-system change), say so in the commit message.

Per-area test saves are not in the repo; the author maintains a local set. If you need one for an area you can't reach, ask in the PR.

## Reverse-engineering workflow

If you need to add a hook on a function the project doesn't already use:

1. Look in `docs/llm-docs/re/` first — Lane Dibello's GoG-derived Ghidra database, headers, SARIF, and PDF live here. The GoG bytes match Steam (memory entry `project_ghidra_gog_steam_bytes_match`), so addresses are usable as-is.
2. If the function isn't named or the layout is incomplete, run `tools/ghidra-scripts/Decompile.java` via `analyzeHeadless.bat` to get a C-like decompile. Recipe in memory entry `reference_ghidra_headless_decompile`.
3. Verify bytes at the address with `tools/ghidra-scripts/DumpBytes.java` before writing the hook. Paste the bytes into the `hooks.toml` cut field; the framework matches on them at load time.
4. Land the hook with a logging-only handler first. Run it in-game, confirm it fires when you expect, *then* add behaviour.

## Sending a PR

1. Branch off `main`.
2. One topic per PR; small + reviewable beats large + comprehensive. A bug fix and an unrelated refactor go in two PRs.
3. Commit messages: short subject (what + why), body if there's context the diff doesn't carry. Don't write planning docs in commit messages.
4. Test in-game. Note in the PR body which areas / scenarios you verified, plus any you couldn't.
5. If your change touches an engine offset, vtable index, or hook address, link to the source you verified it against (decompile, SARIF query, memory entry, or session log line).
6. If your change adds a user-facing string, add the `Id` and the English entry. Other locales can land in the same PR if you can write them, or in a follow-up — translations are cheap, the architectural rule is that handler code stays language-agnostic via `strings::Get(Id)`.

## Where to find things

- **What an engine function does:** `docs/llm-docs/re/swkotor.exe.h` (Lane's headers), or run `Decompile.java` for an address.
- **Tool inventory + install paths:** `docs/tools.md`.
- **Active upstream PR backlog:** `docs/upstream-prs.md`.
- **Why a past decision was made:** check the memory index (`~/.claude/projects/<this repo>/memory/MEMORY.md`) and the relevant `project_*` / `feedback_*` entries. Most non-obvious code has a memory entry.
- **Historical investigation docs:** `archiev/archived docs <date>/`. Don't expect them to match current code; they're frozen snapshots.

## Getting help

The mod's author is reachable at `fabian@nordwiesen30.de`. Issues on the repo are the preferred channel for technical questions so other contributors can see the answers.
