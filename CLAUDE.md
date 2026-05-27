# KOTOR Accessibility Mod

## Purpose
Accessibility mod/contribution for **Star Wars: Knights of the Old Republic** (KOTOR 1, BioWare 2003, Steam release) enabling blind and visually impaired players to play using screen readers (NVDA/JAWS/Narrator).

## Developer accessibility
The developer of this mod is blind. Any workflow you propose has to be doable with a screen reader and keyboard alone. Things to avoid unless there is no alternative:
- Mouse-only steps ("click here", "drag this", "hover over X")
- "Check the screen", "look at the colour", "see if the icon shows" — anything that needs visual confirmation
- GUI tools that aren't screen-reader-friendly (most Ghidra panels, raw imhex hex dumps, image diff viewers, etc.)
- Asking the user to read what's on a loading/black screen, or to time something visually
- Sighted-only verification ("does the cutscene look right?", "is the character in the right spot?")

Prefer instead: CLI commands the user can run, log lines we can grep, deterministic test states, screen-reader-announceable outputs, and "speak what you hear" as the user's verification channel. When a sighted check would normally be the fast path, treat it as a last resort and ask the user explicitly before proposing it.

## Accessibility Goals
- Well-structured text output (no tables, no graphics)
- Linear, readable format for screen readers
- Full keyboard navigation support
- Announce context changes, focused elements, and game state

## Claude Response Formatting
- Never use markdown tables (| symbols are read aloud by screen readers)
- Use headings and bullet lists for comparisons
- Present information linearly, one item per line
- Group related info under clear labels
- Don't write raw memory addresses in chat answers. Use the function name, field name, or a short recognisable description instead ("PlayFootstep's field6_0x20 check", "the engine's natural early-out JZ", "the wrapper's TEST EAX,EAX"). Addresses bloat answers without informing the reader. Only include an address when it is itself the point — e.g. when distinguishing two adjacent hook candidates by their exact offset, or when reporting a finding that has to be re-typed into Ghidra/hooks.toml. In code, hooks.toml comments, memory entries, and docs, addresses are still encouraged.

Example - instead of tables, format like this:
**Item Name**
- Property: Value
- Property: Value

## Permissions
- NEVER add broad PowerShell wildcard permissions like `Bash(powershell -Command:*)` or `Bash(powershell:*)` to settings.local.json - always use specific, scoped commands only

## Code Standards
- Modular, maintainable, efficient code
- Avoid redundancy
- Consistent naming
- Verify changes fit existing codebase before implementing - read and understand surrounding code first
- When contributing to an existing project, match the existing style, patterns, and conventions
- When fixing UI interaction bugs (e.g., keyboard event handling, focus management), always test edge cases where the fix might interfere with normal component behavior
- Prefer editing existing files over creating new ones
- Only make changes that are directly requested or clearly necessary - no drive-by refactors
- Before adding a helper, search for an existing one — duplicate utilities are a recurring failure mode across these accessibility projects

## Bash Tool on Windows
- Bash tool runs through bash shell (Git Bash), NOT CMD or PowerShell
- NEVER use `cd /d` — that's CMD syntax, invalid in bash
- NEVER use `cd` — use absolute paths in tool calls
- For git: use `git -C "<absolute project path>"` instead of cd'ing first
- For other tools: pass full paths as arguments
- Windows paths with backslashes work in Git Bash when quoted

## Project Reference

### Game install
- **Steam path:** `C:\Program Files (x86)\Steam\steamapps\common\swkotor`
- **Executable:** `swkotor.exe`
- **Config:** `swkotor.ini` (in install root; `[Alias]` section defines folder mapping)
- **Engine:** BioWare Odyssey Engine (Aurora Engine derivative, also used in NWN)

### Project workspace
- **Project root:** `C:\Users\fabia\Dev\kotor`
- **`tools/kdev/`** — internal dev CLI (.NET 10), drives the full build → install → launch loop for our patch
- **`patches/Accessibility/`** — our patch source (hooks, menus, narration, audio, guidance, and engine-interface modules)
- **`installer/KotorAccessibilityInstaller/`** — end-user installer (.NET 8 WinForms, self-contained single-file EXE). References KPatchCore directly; modelled on the arena installer (`AccessibleArenaInstaller/IMPLEMENTATION.md` in the sibling arena project) — read that first if touching this code.
- **`installer/release.ps1`** — local release pipeline (kdev build → publish installer → tag → gh release)
- **`third_party/`** — vendored dependencies: `Kotor-Patch-Manager`, `KotorMessageInjector`, `KotOR_IO`, `prism`/`prism-dist`, `dsoal`, `tolk`, and others. See the directory for the current set.
- **`docs/`** — see Documentation below
- **`build/`, `logs/`** — generated; gitignored
- **`kdev.toml`** — project-level config consumed by `kdev`

### Documentation
Always read these before diving into work — they capture decisions and current state:
- **`docs/tools.md`** — installs and paths (game, JDK, Ghidra, VS Build Tools, patch manager, kdev). Update when new tools are installed.
- **`docs/kdev-design.md`** — kdev's design decisions, project layout, subcommand contracts, exit codes, completion status.
- **`docs/controls-and-input.md`** — canonical KOTOR 1 keyboard/mouse control survey + accessibility-gap backlog.
- **`docs/installer.md`** — end-user installer design, bundled-mods plan, beta-prep notes.
- **`docs/upstream-prs.md`** — tracking of fixes/features we plan to send back to upstream (mostly KPatchManager).
- **`docs/known-issues.md`** — five-bucket status tracker (Bugs / Planned / Monitor / Polish / Beta Preparations).
- **`docs/llm-docs/`** — LLM-targeted reference material. See `docs/llm-docs/CLAUDE.md` for the index. Start with `game-flow.md` for lifecycle context, `accessibility-map.md` for pillar-by-pillar hook candidates, and `sarif-cookbook.md` for querying Lane's RE database.
- **`archiev/`** — historical investigation, design, and progress docs (session-by-session retrospectives). Search here if a code comment references a `docs/*-investigation.md` or `docs/navsystem-*.md` that no longer exists in `docs/`.

### Game folders that matter for modding
- **`Override\`** — drop-in folder; any file here overrides the matching resource in the BIFs. Primary entry point for content mods.
- **`modules\`** — per-area `.rim` / `_s.rim` (script) bundles, one pair per game module (e.g. `M12ab.rim`).
- **`rims\`** — global rims (`global.rim`, `chargen.rim`, etc.).
- **`data\`** — `chitin.key`-indexed `.bif` archives (`2da.bif`, `dialog.bif`, `scripts.bif`, `templates.bif`, ...). Read-only base assets.
- **`lips\`** — lipsync data per VO line.
- **`streamwaves\` / `streammusic\` / `streamsounds\`** — audio (potential source for VO subtitles / cues).
- **`dialog.tlk`** — master string table, all in-game text keyed by StrRef.
- **`patch.erf`** — Steam patch overlay.

### Key file formats
- **KEY/BIF** — chitin.key indexes BIF archives (base assets).
- **ERF/RIM/MOD** — encapsulated resource files (modules, patches).
- **2DA** — 2-dimensional array tables (game data, e.g. `feat.2da`, `spells.2da`).
- **GFF** — Generic File Format, structured binary used by `.utc` (creatures), `.uti` (items), `.dlg` (dialogs), `.are` (areas), `.git`, `.ifo`, `.utm`, etc.
- **NSS / NCS** — NWScript source / compiled bytecode; the engine's scripting language.
- **TLK** — talk table; all UI/dialog strings.
- **MDL/MDX, TPC, TGA** — models and textures.

### Modding entry points (in priority order for accessibility work)
1. **Override drop-ins** — replace 2DA / GFF / NCS without touching base archives.
2. **NWScript hooks** — every dialog node, area transition, combat round, and trigger fires scripts; modded `.ncs` can emit state we read externally.
3. **External companion process** — DLL injection or memory reader to surface focused UI element / tooltip text to a screen reader (the engine has no native a11y APIs).
4. **TLK / dialog.tlk edits** — for changing or augmenting text content.

### Established community tooling (to be installed locally as needed)
- **KotOR Tool** (Fred Tetra) — GUI extractor/editor for BIF/ERF/2DA/GFF.
- **TSLPatcher / HoloPatcher** — standard installer format for distributing mods that patch 2DAs without conflicts.
- **xoreos / xoreos-tools** — open-source reimplementation; `xoreos-tools` CLI is excellent for headless extraction (`unkeybif`, `unerf`, `gff2xml`, `tlk2xml`, `ncsdis`).
- **KOTORBlender** — Blender add-on for MDL.
- **nwnnsscomp** — NWScript compiler/decompiler (KOTOR variant).
- **DeadlyStream** — primary mod hosting site / community knowledge base.

### Build/run
- **Inner dev loop:** use `kdev` from the project root. Common: `kdev status`, `kdev dev` (clean → build → apply → launch), `kdev kill`, `kdev logs --follow`.
- **`kdev` build:** `dotnet build tools/kdev/kdev.csproj` from the project root (output at `tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe`).
- **Mod build pipeline:** `kdev build` stages `patches/Accessibility/` next to the upstream `Common/`+`lib/` layout `create-patch.bat` expects, runs the bat under MSVC, drops the `.kpatch` in `build/`.
- **Mod install:** `kdev apply` calls `KPatchCore.PatchApplicator.InstallPatches` against the configured Steam install.
- **Mod launch:** `kdev launch [--monitor]` spawns the prebuilt 32-bit `KPatchLauncher.exe` (calling KPatchCore directly from the x64 kdev process would silently fail injection). `--monitor` is currently broken on Steam (delayed-injection breaks `Process.ExitCode`); prefer `kdev launch` and read patch logs directly.

### Logs
- **Project logs:** `logs/` at project root (gitignored). `kdev` writes `kdev-<utc>.log`, `build-<utc>.log`, `dev-<utc>.log`. Tail with `kdev logs [--follow]`.
- **Game logs:** `<install>/logs/` — written by the game itself (largely silent).
- **Patch DLL logs:** convention is `logs/patch.log` from inside our injected DLL (to be established when we write the first hook).

### Key entry points
- **`patches/Accessibility/`** — our patch source. Will contain `manifest.toml`, `hooks.toml`, `exports.def`, `*.cpp`.
- **`tools/kdev/Program.cs`** — dev CLI entry; subcommands in `tools/kdev/Commands/`.
- **`third_party/Kotor-Patch-Manager/Patches/`** — Lane's example patches; canonical reference for how a `.kpatch` is structured. `ScriptExtender/` is the closest analog to what we'll build.
- **`third_party/Kotor-Patch-Manager/docs/`** — upstream's `MULTI_VERSION_ARCHITECTURE.md`, `AddressDatabaseSystem.md`, `Roadmap2026.md`. Read when designing hooks.
- **Override\\, dialog.tlk, GUI .gui files** — game-side artifacts; relevant if/when we move beyond detour hooks into data-side mods.

### External references
- **Lane Dibello's GitHub** (primary upstream — Patch Manager, MessageInjector, KotOR_IO, etc.): `https://github.com/LaneDibello`. Full repo inventory + URLs in `docs/tools.md` under "Upstream sources".
- **OldRepublicDevs/PyKotor** (HoloPatcher, file-format Python lib; future distribution path): `https://github.com/OldRepublicDevs/PyKotor`
- **DeadlyStream** thread for Lane's RE work: `https://deadlystream.com/topic/11948-kotor-1-gog-reverse-engineering/`
- **xoreos** (open-source engine reimplementation, useful cross-reference): `https://github.com/xoreos/xoreos`
- BioWare Aurora/Odyssey docs are out of print; community references and xoreos source are the practical reference.
- NWScript reference: `nwscript.nss` (extract from `scripts.bif`).
- Lane's KOTOR 1 Ghidra database (`k1_win_gog_swkotor.exe.gzf`) lives on Lane's Google Drive (linked from the DeadlyStream thread above) — not pulled locally yet; download into `docs/llm-docs/re/` when we need to read function semantics.
