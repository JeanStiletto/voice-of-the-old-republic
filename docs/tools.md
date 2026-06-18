# Tools

Inventory of every tool / dependency relevant to this project, with install paths, launch instructions, and current status. Update as new tools are added or paths change.

## Game install (target)

**Star Wars: Knights of the Old Republic 1** (Steam)
- **Install path:** `C:\Program Files (x86)\Steam\steamapps\common\swkotor\`
- **Executable:** `swkotor.exe`
- **SHA-256 of `swkotor.exe`:** `34e6d971c034222a417995d8e1e8fdd9f8781795c9c289bd86c499a439f34c88`
- **Patch level:** v1.0.3 — recognized by Lane's framework as `kotor1_steam_103` (the canonical Steam build)
- **Status:** Supported. The Patch Manager's `KPatchCore/Detectors/GameDetector.cs` knows this exact SHA as `Distribution.Steam`, KotOR 1, v1.0.3. 9 of the 15 example patches already declare it in their `supported_versions`. The `ScriptExtender` example patch's manifest is outdated and lists only GoG/cdcrack — that's a single-patch oversight, not a framework limitation.
- **Steam DRM note:** Steam encrypts the EXE on disk. The Patch Manager's launcher detects this and uses *delayed injection* — launches the game normally, waits for Steam to decrypt and the real game process to appear (validated via PE header DOS MZ signature at `0x400000` to filter out Steam's bootstrap stub), then injects `KotorPatcher.dll`. Implemented in `src/KPatchCore/Launcher/ProcessInjector.cs:LaunchSteamWithInjection`. We get this for free.

## Upstream sources (where to look for updates and reference)

### Lane Dibello (primary upstream)

- **Profile:** `https://github.com/LaneDibello` — browse here when looking for new repos, recent commits, or roadmap activity
- **Discord handle (per release READMEs):** `@lane_d`

KOTOR-relevant repos:

- **`Kotor-Patch-Manager`** — `https://github.com/LaneDibello/Kotor-Patch-Manager` — runtime DLL-injection patching framework. Cloned to `third_party/`.
- **`KotorMessageInjector`** — `https://github.com/LaneDibello/KotorMessageInjector` — .NET library for external-process attach + state read + message inject. Cloned to `third_party/`. Demoted to research/debug tool per architecture decisions.
- **`KotOR_IO`** — `https://github.com/LaneDibello/KotOR_IO` — C# library for KOTOR file formats (2DA, GFF, TLK, ERF/RIM, BIF/KEY). Cloned to `third_party/`.
- **`KeyMouseAccessibilityTest`** — `https://github.com/LaneDibello/KeyMouseAccessibilityTest` — Lane's earlier accessibility experiment (keyboard-driven mouse). No README; source-only. Worth a skim before committing to our own patterns.
- **`DLZ-Tool`** — `https://github.com/LaneDibello/DLZ-Tool` — RE tooling for KOTOR DLZ files. Cloned to `third_party/`. `KotorAdresses.h` + `types.h` are an independent address/struct reference; small repo, high signal.
- **`Kotor-Randomizer`** — `https://github.com/LaneDibello/Kotor-Randomizer` — randomizer mod; useful as a worked example of large-scale KOTOR file editing.
- **`KotOR2RandoConsole`** — `https://github.com/LaneDibello/KotOR2RandoConsole` — KOTOR 2 RE research (early stage).

Lane's RE work also lives in a Ghidra `.gzf` archive on Google Drive (linked from the DeadlyStream thread below) — not in any GitHub repo. Pull into `docs/llm-docs/re/` when we need to read function semantics.

### OldRepublicDevs (Lane contributes here)

- **`PyKotor`** — `https://github.com/OldRepublicDevs/PyKotor` — Python library for KOTOR file formats; ships **HoloPatcher** (TSLPatcher's modern replacement). Lane's roadmap targets HoloPatcher as the distribution path for `.kpatch` files.

### Forums and other references

- **DeadlyStream** primary thread for Lane's RE work: `https://deadlystream.com/topic/11948-kotor-1-gog-reverse-engineering/` — Lane posts releases, RE updates, and modding context here. First post links the Google Drive Ghidra archive.
- **xoreos** open-source reimplementation: `https://github.com/xoreos/xoreos` — useful for cross-referencing engine internals and file formats.

## Java Development Kit

**Adoptium Temurin OpenJDK 21.0.11 LTS**
- **Install path:** `C:\Tools\jdk-21.0.11+10\`
- **Java executable:** `C:\Tools\jdk-21.0.11+10\bin\java.exe`
- **Environment variable:** `JAVA_HOME` set at user level to `C:\Tools\jdk-21.0.11+10` (visible in newly-spawned shells, not in any shell that was open before this was set)
- **Verification command:** `"C:/Tools/jdk-21.0.11+10/bin/java" -version` should report `Temurin-21.0.11+10`
- **Why:** Ghidra 12.x requires JDK 17+. The pre-existing system Java was 1.8 — too old. JDK 21 is current LTS.

## Visual Studio Build Tools (C++ compiler)

**Visual Studio Build Tools 2022** (no IDE — compiler + Windows SDK only)
- **Install path:** `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\`
- **`vswhere.exe`:** `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` (standard location, used by `create-patch.bat`)
- **`vcvars32.bat`:** `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat` (sets up 32-bit x86 build environment)
- **MSVC compiler:** `cl.exe` 19.44.35226 for x86, under `VC\Tools\MSVC\14.44.35207\bin\`
- **Workload installed:** `Microsoft.VisualStudio.Workload.VCTools` with `--includeRecommended` (Desktop development with C++; pulls in Windows 10 SDK)
- **Verified:** `cmd /c "vcvars32.bat && cl"` reports the compiler banner end-to-end
- **Why:** required by `create-patch.bat` to compile detour-type patches' C++ source into `windows_x86.dll`

## Ghidra (reverse engineering)

**Ghidra 12.0.4 PUBLIC** (build 20260303)
- **Install path:** `C:\Tools\ghidra_12.0.4_PUBLIC\`
- **Launch GUI:** double-click `C:\Tools\ghidra_12.0.4_PUBLIC\ghidraRun.bat`
- **Headless analyzer:** `C:\Tools\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat`
- **Docs entry point:** `C:\Tools\ghidra_12.0.4_PUBLIC\GettingStarted.html`
- **GUI smoke test:** not yet performed — launch once and confirm a project window opens
- **Why:** required for decompiled pseudocode, cross-references, raw instruction bytes, and any RE work past what's already exported. The function-name / address / struct-offset slice of Lane's RE is already queryable via `AddressDatabases/kotor1_0_3.db` (see "SQLite" below) — Ghidra is for the rest.

### Ghidra project (Lane's KOTOR 1 RE database, imported)

- **Project location:** `C:\Tools\ghidra-projects\kotor1\` (outside the repo so it survives `kdev clean`; regeneratable from the `.gzf` in ~30 sec)
- **Imported program name:** `k1_win_gog_swkotor.exe`
- **Source archive:** `docs/llm-docs/re/k1_win_gog_swkotor.exe.gzf` (see "Reverse-engineering reference data" below)
- **Steam-vs-GoG note:** the `.gzf` was built against the GoG binary, but bytes at common code addresses are identical between GoG and Steam (verified by cross-checking `0x00552c9a`). So bytes pulled from this Ghidra project can be used directly in our Steam-targeted `hooks.toml` without translation.

### Ghidra scripts and headless workflow

- **Our scripts:** `tools/ghidra-scripts/` (in repo, shareable). Currently:
  - `DumpBytes.java` — print N bytes at given addresses; used to discover detour `original_bytes` for new hooks.
  - `Decompile.java` — print decompiled C for one or more functions by entry-point address. If the FIRST arg is a path it writes **clean** C to that file (no Ghidra log prefixes / doubled lines / banner); otherwise it `println`s (noisy — the headless logger mirrors+prefixes each line, so output comes out doubled). Prefer the wrapper below.
- **Decompile a function — clean one-liner:** `bash tools/ghidra-scripts/decomp.sh 0x<addr> [0x<addr> ...]`. Picks a temp out-file, runs headless, prints pure C. ~30-60s (JVM + 28 MB project load — inherent to Ghidra headless, not a hang; the run auto-backgrounds via the Bash tool, so read the task output on completion). Only one headless run at a time against the `kotor1` project (it takes a lock).
- **Standard headless invocation pattern** (read-only against the imported project):
  ```
  "C:/Tools/ghidra_12.0.4_PUBLIC/support/analyzeHeadless.bat" \
      "C:/Tools/ghidra-projects" kotor1 \
      -process k1_win_gog_swkotor.exe \
      -readOnly \
      -scriptPath "C:/Users/fabia/Dev/kotor/tools/ghidra-scripts" \
      -postScript DumpBytes.java <addr_hex>:<count> [<addr_hex>:<count> ...]
  ```
  Output lines start with `BYTES ` so the caller can grep them out of Ghidra's startup banner.

## Reverse-engineering reference data (downloaded)

Lane's KOTOR 1 RE bundle from the Google Drive folder linked from his DeadlyStream thread. Cloned to `docs/llm-docs/re/` (which is gitignored if the patches workflow needs to keep the repo small — confirm before commit). Total ~600 MB across 5 files.

- **`k1_win_gog_swkotor.exe.gzf`** (28 MB) — Lane's full Ghidra database, Java-serialized. Imported into the Ghidra project above; otherwise opaque without Ghidra.
- **`k1_win_gog_swkotor.exe.xml`** (31 MB) — Ghidra Program XML export. Symbols, function metadata, code-block ranges, comments, datatypes — all parseable as text. Notably *does not* include raw instruction bytes.
- **`k1_win_gog_swkotor.exe.sarif`** (490 MB) — Ghidra SARIF 2.1.0 analysis export. JSON. Largest single artifact; query with `jq`. Not yet exercised; defer until a question lands that the XML/DB can't answer.
- **`swkotor.exe.h`** (745 KB) — C header with Ghidra-style typedefs and (likely) function declarations. Candidate for direct `#include` in patch source.
- **`KotOR_1_System_Layout-2.pdf`** (28 MB) — Lane's architecture / system layout document. Human-readable reference.

## Pending downloads / installs

## Sysinternals DebugView (debug-output capture)

**DebugView** — captures `OutputDebugStringA` output from any running process. Used to see KotorPatcher's full load trace (LoadLibrary results, GetProcAddress results, byte-verification mismatches) and our own patch DLL's debug output, all of which is otherwise invisible.
- **Install path:** `C:\Tools\DebugView\`
- **Launch (64-bit Windows):** `C:\Tools\DebugView\dbgview64.exe`
- **First-run setup:** menu *Capture* → enable both `Capture Win32` and `Capture Global Win32` (the latter requires running as admin to see output from elevated processes; the game runs as user, so user-elevation is fine).
- **Filtering:** *Edit* → *Filter/Highlight* → set Include to `[KotorPatcher];[accessibility];[GameVersion]` to cut noise.
- **Why:** KotorPatcher's entire diagnostic trail goes to `OutputDebugStringA`, with no fallback log file. Without DebugView, every load failure is silent.

## Lane Dibello's Patch Manager (prebuilt release)

**Latest release Beta 0.4.2**, used for end-to-end testing without compiling from source.
- **Install path:** `C:\Tools\KotorPatchManager-v0.4.2\`
- **Launcher:** `C:\Tools\KotorPatchManager-v0.4.2\bin\KPatchLauncher.exe` (~122 MB; includes self-contained .NET runtime)
- **Injected DLL:** `C:\Tools\KotorPatchManager-v0.4.2\bin\KotorPatcher.dll`
- **Address databases:** `C:\Tools\KotorPatchManager-v0.4.2\bin\AddressDatabases\` (`kotor1_0_3.db` ≈ 2.1 MB, `kotor2_gog_aspyr.db` ≈ 60 KB)
- **Bundled patches:** `C:\Tools\KotorPatchManager-v0.4.2\patches\` — 15 `.kpatch` files. Steam-confirmed candidates for smoke-testing: `Widescreen.kpatch` (visible effect), `RepeatingBlaster.kpatch`, `SemiTransparentLetterbox.kpatch`.
- **Tools:** `C:\Tools\KotorPatchManager-v0.4.2\tools\` (`create-patch.bat`)

## Lane Dibello's Patch Manager (cloned source)

**Repo:** `LaneDibello/Kotor-Patch-Manager` — runtime DLL-injection patching framework for KotOR 1 & 2
- **Local clone:** `C:\Users\fabia\Dev\kotor\third_party\Kotor-Patch-Manager\`
- **Default branch:** `master`
- **Latest release at clone time:** Beta 0.4.2 (2026-04-12)
- **Build prerequisite:** Visual Studio 2022 with C++ desktop workload + .NET workload (not yet installed)
- **Solution file:** `KotorPatchManager.sln` at clone root
- **Build configuration constraint:** `KotorPatcher` project must build as `Win32` (KotOR is 32-bit); other projects can be `Any CPU`
- **Key subdirectories:**
  - `AddressDatabases/` — SQLite DBs of reverse-engineered function/data addresses per game version (`kotor1_0_3.db`, `kotor2_gog_aspyr.db`)
  - `Patches/` — example patches, including `ScriptExtender/` (adds new NWScript commands)
  - `src/` — source for `KPatchCore` (C#), `KPatchLauncher` (C#), `KotorPatcher` (C++ DLL)
  - `lib/sqlite3.*` — bundled SQLite native libs
  - `docs/` — upstream docs (`MULTI_VERSION_ARCHITECTURE.md`, `AddressDatabaseSystem.md`, `Roadmap2026.md`, etc.)
- **License:** none declared in repo metadata as of clone; treat as "all rights reserved" until clarified
- **Status:** Beta, 45 open issues at time of clone

## Lane Dibello's KotorMessageInjector (cloned source)

**Repo:** `LaneDibello/KotorMessageInjector` — .NET library for attaching to a running `swkotor.exe`, reading game state, injecting `CSWMessage` events. **Primary basis for the MVP companion app.**
- **Local clone:** `C:\Users\fabia\Dev\kotor\third_party\KotorMessageInjector\`
- **Solution file:** `KotorMessageInjector.sln`
- **Top-level layout:** `KotorMessageInjector/` (the library), `testApp/` (working example), `README.md`
- **Key API surface (per upstream README):** `ProcessAPI` (Win32 wrappers), `KotorHelpers` (`getGameVersion(out isSteam)`, `getPlayerServerID/ClientID`, `getLookingAtServerID/ClientID`, `KOTOR_OFFSET_*` / `KOTOR_1_*` / `KOTOR_2_*` constants), `Message` (CSWMessage builder), `Injector` (orchestrates remote alloc + asm shim + thread spawn)
- **Steam recognition:** `getGameVersion` already distinguishes Steam from other versions. Whether all KOTOR-1 offsets are correct on the Steam binary is unverified — see investigation doc step 4.

## Lane Dibello's DLZ-Tool (cloned source)

**Repo:** `LaneDibello/DLZ-Tool` — RE-backed C++ tooling for analyzing KOTOR DLZ files. Tiny repo, high signal — independent confirmation of addresses also present in the Patch Manager's address DB.
- **Local clone:** `C:\Users\fabia\Dev\kotor\third_party\DLZ-Tool\`
- **Key files:**
  - `KotorAdresses.h` — hardcoded addresses for KOTOR 1 + KOTOR 2 (`ADDRESS_APP_MANAGER = 0x007a39fc` for KOTOR 1, matches the Patch Manager DB's `APP_MANAGER_PTR`), plus a `KotorAddresses` class with version switching
  - `types.h` — `GameObjectEntry` struct, `GAME_OBJECT_TYPES` enum (CREATURE=5, DOOR=10, etc.), helpers like `getGOAIndexFromServerID`
  - `KotorManager.h`, `ProcessReader.h`, `main.cpp` — external-process reader pattern (predecessor to MessageInjector)
- **Why:** second source of truth for object-structure offsets, useful when probing DB results for sanity

## Lane Dibello's KotOR_IO (cloned source)

**Repo:** `LaneDibello/KotOR_IO` — C# library for reading and writing KOTOR file formats. Relevant when we move past native-binary hooks into 2DA/GFF/TLK editing.
- **Local clone:** `C:\Users\fabia\Dev\kotor\third_party\KotOR_IO\`
- **Solution file:** `KotOR_IO.sln`
- **Layout:** `KotOR_IO/` (the library), `KIOTest/`, `test8/`
- **Formats supported:** 2DA, GFF, TLK, ERF/RIM, BIF/KEY (per upstream description)

## jq (SARIF query)

**jq 1.8.1** — installed at `C:\Users\fabia\bin\jq.exe` (on PATH). Used to query the 490 MB SARIF export, which holds the slice of Lane's RE that the SQLite DB doesn't (full signatures with parameter types, vtable layouts, ~369k cross-references, Lane's textual comments, complete symbol set).
- **Source:** `https://github.com/jqlang/jq/releases/latest/download/jq-windows-amd64.exe` (1.0 MB standalone).
- **Performance:** 5–10 seconds per query against the 490 MB SARIF (whole file is parsed each time). Acceptable for occasional lookups; build the SARIF→SQLite ingester if we hit a workflow where we re-query repeatedly.
- **Recipes:** see `docs/llm-docs/sarif-cookbook.md` — covers find-by-name, list-class, callers/callees, comments-in-range, struct layout, bookmarks.

## SQLite (RE database query)

**SQLite 3.46.1** — already on PATH at `C:\Users\fabia\bin\sqlite3.exe`. Used to query the Patch Manager's address database, which is the queryable form of Lane's Ghidra-exported RE.
- **DB location:** `C:\Users\fabia\Dev\kotor\third_party\Kotor-Patch-Manager\AddressDatabases\kotor1_0_3.db`
- **Tables:** `functions` (9710 rows: class_name, function_name, address, calling_convention, param_size_bytes, notes), `global_pointers` (16 rows), `offsets` (4255 rows: class member offsets), `game_version` (SHA-256 binding)
- **Pipeline upstream:** Lane works in Ghidra → exports CSV → `tools/SqliteTools import-ghidra` writes into the DB. Schema and the import command are documented in `third_party/Kotor-Patch-Manager/docs/AddressDatabaseSystem.md`.
- **Implication:** for "what is the address of `CServerExoApp::MainLoop`?" or "what is the offset of `m_bGamePaused`?" the DB answers directly — Ghidra is only needed for decompilation, comments, and unnamed code. See `docs/llm-docs/accessibility-map.md` for current hook candidates.

## xoreos-tools (resource extraction)

**xoreos-tools 0.0.6 "Elanee"** — CLI suite for unpacking BioWare Aurora-engine archives (KEY/BIF, ERF, RIM, etc.). Used to dump `data/sounds.bif` to loose WAVs for browsing during nav-cue curation; broader use cases: GFF inspection, TLK extraction, NCS disassembly.
- **Install path:** `C:\Tools\xoreos-tools-0.0.6\`
- **Source:** `https://github.com/xoreos/xoreos-tools/releases/download/v0.0.6/xoreos-tools-0.0.6-win64.zip` (48 MB zip, expands to ~120 MB)
- **Key binaries:** `unkeybif.exe` (KEY/BIF extractor), `unerf.exe` (ERF/MOD), `unrim.exe` (RIM), `gff2xml.exe` (GFF → XML), `tlk2xml.exe` (TLK → XML), `ncsdis.exe` (NWScript bytecode disassembler), `convert2da.exe` (2DA binary ↔ ASCII)
- **Verification:** `"C:/Tools/xoreos-tools-0.0.6/unkeybif.exe" --version` reports `xoreos-tools 0.0.6+0.g87946ab`
- **Sounds.bif extract recipe:**
  ```bash
  cd <output_dir>
  "C:/Tools/xoreos-tools-0.0.6/unkeybif.exe" e \
      "C:/Program Files (x86)/Steam/steamapps/common/swkotor/data/sounds.bif" \
      "C:/Program Files (x86)/Steam/steamapps/common/swkotor/chitin.key"
  ```
  Yields 1928 named WAVs in cwd. Categorical prefixes: `gui_*` (UI clicks/beeps), `dr_*` (door open/close), `fs_*` (footsteps), `as_*`, `cb_*`, `cs_*`, `mgs_*`, `bf_*`, etc.
- **Output location for our use:** `build/sounds-extracted/` (gitignored; regenerate from BIF on demand).

## Other Lane repos (not yet cloned, noted for reference)

These are upstream — fetch via `gh repo clone <name>` if/when needed.

- **`LaneDibello/KeyMouseAccessibilityTest`** — Lane's earlier accessibility experiment (keyboard-driven mouse). No README. Source-only; small.
- **`OldRepublicDevs/PyKotor`** — Python library for KOTOR file formats; ships **HoloPatcher** (TSLPatcher's modern replacement). Lane's roadmap targets HoloPatcher integration as the distribution path for `.kpatch` files. ~348 MB repo.

## Downloads cache

- **`C:\Tools\downloads\jdk21.zip`** — 196 MB, source for the JDK install above
- **`C:\Tools\downloads\ghidra.zip`** — 486 MB, source for the Ghidra install above
- **Status:** safe to delete once Ghidra GUI smoke test passes

## Pending downloads / installs

- *(Lane's KOTOR 1 Ghidra bundle — downloaded 2026-04-29; see "Reverse-engineering reference data" above.)*
- **Visual Studio 2022 Community** with "Desktop development with C++" + ".NET desktop development" workloads. Required to build the Patch Manager from source (or to build a `.kpatch`). Not required for the MessageInjector MVP if we use `dotnet` SDK + a CLI workflow instead. Decide before phase 2.
- **NVDA** screen reader + **Tolk** library (`https://github.com/dkager/tolk`). Required for the MVP to actually speak to a screen reader. Tolk wraps NVDA / JAWS / SAPI / Narrator behind one C API and has C# bindings.

## Conventions

- All third-party source clones live under `C:\Users\fabia\Dev\kotor\third_party\<repo-name>\`.
- All standalone tools (JDK, Ghidra, future installs) live under `C:\Tools\<tool>-<version>\`.
- Reference / research material (RE databases, decompiled scripts, vendor docs we want to mirror locally) lives under `C:\Users\fabia\Dev\kotor\docs\llm-docs\`.
