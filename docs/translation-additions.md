# Translation additions — Polish & Russian

**Status: both UNPARKED. Russian 2026-07-25, Polish 2026-07-31.**
Both distributions are in hand and fully characterised against the two-case
model below. Russian (Allard 1.72) is **Case B**. Polish (the official LEM
edition) is **Case B as well, and the same binary** — see "Polish — measured
(2026-07-31)", which supersedes the speculative "Polish — findings" section
kept below it for the record.

Related: `docs/known-issues.md` ("Integrate a Polish translation" Planned item;
the Polish LanguageID=5 speech-default fix), `docs/upstream-prs.md` (PR C /
`AllowVersionMismatch`), `docs/installer.md` (K1CP locale overlays).

---

## The core model: two cases a translation can fall into

A community translation of KOTOR 1 can affect us in two very different ways.
Which case a given translation is in decides the whole fix.

### Case A — `dialog.tlk` (and textures/audio) only, `swkotor.exe` untouched
- The exe is byte-identical to stock, so its SHA-256 still matches one of our
  three known-good hashes in `patches/Accessibility/manifest.toml`
  `[patch.supported_versions]` (Steam 1.0.3, GoG 1.0.3, GoG CD-repack).
- Our patch installs and loads normally. The **only** symptom is *which language
  we speak*: our `DetectLanguageFromTlk` reads the `dialog.tlk` header language
  ID and, for anything outside 0–4 (En/Fr/De/It/Es), falls back to **English**
  speech (used to be German; fixed in v-note, see known-issues).
- Fix for Case A = purely our side: map the language ID, author our own
  speech strings for it. No exe allow-list change needed.

### Case B — the translation modifies or replaces `swkotor.exe`
- The exe hash no longer matches → KPatchCore's version gate
  (`GameVersionValidator.ValidateAllPatchesSupported`) **hard-fails** and the
  end-user installer refuses (it calls `InstallPatches` **without**
  `AllowVersionMismatch`, so it defaults to `false`).
- To support these installs we'd need to either add the modified exe's hash to
  `supported_versions` **or** expose `AllowVersionMismatch` in the installer —
  but only after verifying the exe is safe to hook (below).

### Why we can't just whitelist a Case B hash blindly
- Our accessibility patch is **essentially all runtime DLL detours at hardcoded
  addresses** (`hooks.toml`: 26 `detour` code hooks + 43 `pointer` / 7 `int`
  address-DB reads that patch nothing). We ship almost no static byte-patches.
- **Good news (verified in `KotorPatcher/src/patcher.cpp`):** every hook type —
  SIMPLE, REPLACE, **and DETOUR** — calls `Trampoline::VerifyBytes(hookAddress,
  original_bytes)` inside the game process before writing. On a mismatch it
  logs "Original bytes mismatch … wrong game version?" and returns false. So on
  a modified exe our detours **fail safe to inert** — they do NOT patch relocated
  code and crash. The PR-5 risk note ("DLL-only worst case = inert hooks, not a
  crash") is therefore correct for us.
- **The residual risk is silent partial death, not a crash:** with
  `AllowVersionMismatch=true` on a code-modified exe, the installer reports
  success but at launch some/all 26 hooks quietly refuse (only via
  `OutputDebugStringA`, not even our `acclog`). A blind user then has a mod that
  "installed fine" but is partly/wholly dead with no announced reason — worse
  than an honest upfront refusal.

### The design shape worth building (from the PR C discussion)
Don't merge PR C as a blind demote-to-warning. Pair it with honesty:
1. **Loud runtime hook-attach report** — have `accessibility.dll` count how many
   of the 26 detours failed `VerifyBytes` and **speak/log a degraded-mode line**
   ("N of 26 accessibility hooks could not attach — your game version may be
   modified"). Crash-safe + honest. Works for both Steam and GoG. Useful on its
   own, independent of any translation.
2. **Install-time hook-site byte check (GoG only)** — run the same `VerifyBytes`
   logic against the exe *file* at our 26 hook windows during install and give a
   deterministic pre-launch verdict. **Steam caveat:** the Steam exe is
   SteamStub-encrypted on disk (same wall the `borderless_fullscreen` static
   patch hit — see `docs/unified-resolution-patch-compatibility.md`), so on-disk
   bytes read as ciphertext and can't be compared. Steam users can only be
   verified at runtime → option 1 is the only honest path for them.

PR C is deliberately **held** for exactly this reason — the modified-exe scenario
may change how (or whether) it should ship. See `docs/upstream-prs.md` PR C.

---

## Polish — measured (2026-07-31)

The maintainer obtained the actual distribution from a user: a 163 MB 7-Zip
archive, "Star Wars Knights of the Old Republic Spolszczenie". Everything here
is measured against those files.

### What the distribution is

- The **official Polish localisation**, extracted from the retail release:
  KOTOR 1 was localised by **Licomp Empik Multimedia (LEM)** and published
  2004-04-05, subtitles only ("wersja kinowa"), on 4 discs already carrying
  patch 1.03. Not a fan translation — the archive's own `docs/` are the
  LucasArts Polish ReadMe and EULA, and `dialog.tlk` declares BioWare's real
  Polish **LanguageID 5**.
- Payload: `dialog.tlk` + `dialogF.TLK` (49,265 strings each), 19 `Override\`
  entries (12 diacritic font sheets, 6 endgame `.dlg`, `k_pebo_mgheart.ncs`,
  `lbl_iplotxp.tpc`), 7 of the 58 `.bik` cutscenes (the ones with on-screen
  text), `utils/*.ini` from the retail launcher, and `swkotor.exe`.
- The chain the user received it through is a Steam guide by "Manunu"
  (`sharedfiles/filedetails/?id=2136715883`) that credits nobody and mirrors the
  files on ufile/mega/4shared/PCGamingWiki. The upstream rights-holder is
  LucasArts, not a modder — which is why we link rather than bundle.

### `swkotor.exe` — Case B, and the same build as Allard's

- 4,042,752 bytes, SHA-256
  `F96AC62BEE256DAC66B3F7DA3ED62DA4005B4710868E33FD0DB51164015D8137`,
  PE link timestamp `2004-03-05 00:43:51 UTC` — **the same timestamp as
  Allard 1.72**.
- Diffed against the Allard exe directly: `.text` (3,391,488 bytes), `.data`
  and `.rsrc` are **byte-identical**. Exactly **16 bytes of `.rdata`** differ,
  at one hardcoded string — LEM has `"Points Remaining"`, Allard translated it
  to CP1251 Cyrillic. Not code, not a vtable; no entry in
  `engine_rebase_rdata.inc` covers that range (the nearest,
  `kVtableCSWGuiAbilitiesCharGen`, ends well before it).
- Consequence: **the entire existing rebase apparatus applied unchanged.** All
  25 rebased hook signatures match the LEM exe byte-for-byte; all 25
  vanilla-layout ones match none of it. Because `engine_rebase.cpp` identifies
  the build by PE link timestamp rather than by hash, it already returned the
  right mapping for this exe before any edit — only the hash allow-lists needed
  changing.
- This is also a **correction to the Russian section below**, which reasoned
  that Allard patched the exe to make the engine read `dialogF.tlk`. Since
  `.text` is identical to the stock LEM retail binary, that behaviour is the
  2004-03-05 build's own; Allard chose that build, and changed one string.

### Renaming that followed

`allard.hooks.toml` → **`relink2004.hooks.toml`**, and `Build::Allard172` →
`Build::Relink2004` (`ActiveBuildName()` now reports `relink-2004-03-05`). One
file and one identity now serve two distributions, and a third on the same build
would work for free. Discovery is a `*hooks.toml` glob at both ends
(`BuildCommand.cs`, `PatchRepository.cs`), so the rename needed no build change.

### Language detection

- LanguageID 5 → `Lang::Pl` directly; no content probe needed, unlike Russian.
- The CP1251 Cyrillic probe runs *before* the ID switch, so it was checked for a
  false positive: four Polish CP1250 letters (ó ć ę ń) do sit ≥ 0xC0. Measured
  **1.88%** on the LEM `dialog.tlk` against German's 1.29% and the 20% bar.
  Safely on the not-Cyrillic side. Recorded at `TlkLooksCyrillic`.
- Strref space is aligned with the other locales (48218 "Zadania" = "Aufträge",
  48220 "Przedmioty" = "Inventar", 48225 "Karta postaci" = "Charakterblatt"), so
  every strref-keyed lookup — including the whole of `tutorial_hints.cpp`, which
  resolves through `LookupTlk` — carried over with no work at all.

### Combat anchors — the one place Polish is genuinely different

`kdev combat-strings-extract` **crashes** on the Polish tlk, and the crash is
informative rather than a bug to paper over: `ReconstructHitMissPhrase` assumes
`<CUSTOM1>` precedes `<CUSTOM2>` in strref 42042, and Polish orders the
placeholders `<CUSTOM0> <CUSTOM2> <CUSTOM1>`.

Four structural divergences, all read off the tlk:

- **42042** is `Atakujący: <CUSTOM0> Cel: <CUSTOM2> Atak: <CUSTOM1>`. The actor
  sits behind a label rather than opening the line, the target comes *before*
  the verb, and the verb — the only hit/miss signal — is last. No choice of
  separator expresses that, so `MsgStrings` gained `summary_actor_prefix` /
  `summary_target_marker` / `summary_verb_marker` and `combat_log.cpp` gained
  `ParseSummaryLabelled` beside the extracted `ParseSummaryClassic`. All six
  existing locales pass `nullptr` and take the original path unchanged.
  Note 42044 "nie trafia" *contains* 42043 "trafia", so the parser tests miss
  first and matches at the verb position rather than searching the line.
- **42046** is `Użyto atutu: <CUSTOM0>` — the feat label leads the name where DE
  trails it with `" verwendet."`. Hence `feat_marker_leads`.
- **1403** is `<CUSTOM0> trafia. <CUSTOM1> otrzymuje <CUSTOM2> pkt obrażeń` —
  no colon between target and amount, which `RuleDirectDamage` relied on. Hence
  `damage_amount_marker`.
- **42158** is a bare `<CUSTOM0> <CUSTOM1>`, no status-echo copula — the same
  situation Russian is in, so `status_ist_marker` takes the same `\x01`
  sentinel. Save types 1374-1376 likewise share a *leading* phrase, so
  `save_marker` uses 1406's `". "` separator as Russian does.

**`kPl` is UNVERIFIED against a live combat log** — the one open item. Every
value is transcribed from the tlk by the documented per-field rules, and all 55
round-trip correctly through CP1250, but Polish is the first locale where the
line *structure* was inferred rather than just its words. One fight on a Polish
install settles it: compare `MsgBuf: raw:` against `emit-*` in the patch log.
Failure mode is a line read out in full instead of shortened.

### Encoding

Windows-1250, pinned through `CodepageFor(Lang::Pl)` → `prism::SetSpeechCodepage`
before the first utterance. This matters more than it did for Russian: ą ć ę ł ń
ś ź ż are absent from 1252 *entirely*, and a Polish KOTOR is almost always a
Polish tlk dropped onto an English or German install, so `CP_ACP` is the wrong
page more often than not. Escapes were generated from UTF-8 by a scratchpad
`escape-cp1250.ps1` (same maximal-munch handling as the Russian pass), and
verified by round-trip decode plus a `check-table.ps1` that compares Id set,
order and printf specifier sequence against `strings_en.cpp`: 620/620 labels,
order identical, all specifiers matching, zero non-ASCII bytes left in the file.

That checker also turned up a **pre-existing gap unrelated to Polish**:
`strings_fr.cpp`, `strings_it.cpp` and `strings_es.cpp` are missing
`Id::SpectatorBattleDoomed` (619 labels against en/de/ru/pl's 620), so that cue
is silent on those three locales.

---

## Polish — findings (superseded, kept for the record)

The section below was written before the distribution was in hand. Its "two
populations" model turned out to be one population: the archive is the LEM
edition, and it does ship a different exe — but that exe needed no new work.

There are **two distinct Polish populations**, and they are different cases:

### 1. Official LEM edition (likely Case B — unconfirmed)
- KOTOR 1 got a full official Polish release by **Licomp Empirical Media (LEM)**,
  v1.03, 2003 (NOT CD Projekt).
- **Strong lead, not yet confirmed against a primary doc:** the official Polish
  edition reportedly ships a **different `swkotor.exe` that reads `dialogF.tlk`**
  instead of `dialog.tlk`. If true this is Case B (hash mismatch) *and* it breaks
  our language **detection**, which reads `dialog.tlk` next to the exe.
- Confirmation was blocked (PCGamingWiki 403, Steam 429). Re-verify when we pick
  this up — decisive for whether LEM users need the hash/AllowVersionMismatch
  path or are simply undetectable by our tlk read.

### 2. Fan Polish subtitle pack (Case A)
- A separate community translation (PCGamingWiki file 2516 / Internet Archive
  `kotor-pl`) that unpacks into the game folder over a stock English install —
  replaces `dialog.tlk`, exe untouched.

### Decisive in-repo evidence: our Polish tester was Case A
- Our own beta log line `Lang: unknown LanguageID=5; defaulting to German` was
  written by our **injected** `accessibility.dll` (`acclog`).
- For that to run, the installer's hash gate had to **pass** (installer never
  sets `AllowVersionMismatch`) → the tester's `swkotor.exe` was a stock,
  unmodified Steam/GoG hash.
- Therefore that tester ran **stock English exe + Polish `dialog.tlk`
  (LanguageID=5)** = Case A. The "accept Polish index, speak Polish" plan fits
  this population directly. The LEM-different-exe population is separate and
  unconfirmed.
- **Polish LanguageID = 5** is confirmed by that log (BioWare's standard ID for
  Polish).

---

## Russian — findings

**UNPARKED (2026-07-25).** The maintainer obtained the actual distribution
(`KotOR_Rus_Allard_1.72.rar`, from a third-party download host, not the
author's own site). Everything below is measured against those files, not
inferred. The earlier speculation in this section has been replaced.

### What the distribution actually is

- `KotOR_Rus_Allard_1.72.rar` (RAR5, 166,057,192 bytes) contains exactly one
  file: `KotOR_Rus_Allard_1.72.exe`, a **WinRAR SFX archive**
  (SHA-256 `789EE5C419D693C600C40E0032EFB730C5EEBD5CA90875B817A84589452BF907`).
- The SFX comment/script is only `Path=…\steamapps\common\swkotor`, `Title=`
  and a `Text{}` block. There is **no `Setup=`, `Presetup=`, `Silent=`,
  `TempMode=` or `Shortcut=`** — it extracts and runs nothing afterwards. (The
  `SetupCode`/`TempMode`/`SavePath` strings visible in the stub are the WinRAR
  SFX module's own command-name table, present in every WinRAR SFX.)
- Payload = 22 files: `dialog.tlk`, `dialogF.tlk`, `swkotor.exe`, 6 `.bik`
  cutscenes, 12 `Override\` entries (Cyrillic bitmap font sheets `.tga`+`.txi`,
  `logo_sw_02.tga`, `k_pkor_pillar01.ncs`), and `TexturePacks\swpc_tex_gui.erf`.
- **No resident tool.** The "keep it running until you quit" description in the
  old notes was wrong for 1.72 — the installer removed the bundled *KotOR
  Settings Editor* (the SFX text says so explicitly). There is no injector, so
  the feared two-injectors conflict with our `dinput8.dll` does not exist.

### Security review of the download (host-tampering check)

Checked because the file came from a re-hosting site rather than the author.
Everything below came back clean:
- Every payload file's magic matches its extension (`TLK V3.0`, `BIKi`,
  `NCS V1.0`, TGA, `mipmap` text TXI, ERF).
- No embedded `MZ`/`PE` image anywhere in the 21 non-exe files.
- No occurrences of `powershell`, `cmd.exe`, `rundll32`, `regsvr32`, `wscript`,
  `mshta`, `certutil`, `schtasks`, `CreateRemoteThread`, `WriteProcessMemory`,
  `URLDownloadToFile`, `ShellExecute`, `WSAStartup`, `CurrentVersion\Run` … in
  any file, including `swkotor.exe`, in either ASCII or UTF-16.
- `swkotor.exe` imports only OPENGL32, KERNEL32, USER32, GDI32, ole32,
  binkw32, mss32, DINPUT8, GLU32, VERSION, IMM32 — the stock KOTOR 1 set.
  **No networking DLL at all** (no ws2_32 / wininet / urlmon), no shell32.
- No path-traversal entries in either archive layer.
- Unsigned, as expected for a 2024 community release. Verdict: nothing
  suggests the host added anything. (Not a substitute for an AV scan, but the
  structural checks a repacker's dropper would fail are all clean.)

### `dialog.tlk` — Case A detection is impossible, LanguageID collides

- `LanguageID = 0` (the **English** slot), 50,039 strings, text encoded in
  **Windows-1251**. Verified by byte histogram (99.2% of high bytes ≥ 0xC0)
  and by decoding — e.g. `ОШИБКА:` renders correctly only under CP1251.
- So **`DetectLanguageFromTlk` cannot distinguish Russian from English by ID.**
  Detection must be content-based: sample the string blob and test for the
  CP1251 Cyrillic signature. A cheap corroborating signal is that a stock
  English install has **no `dialogF.tlk`**, while this one ships one.
- `dialogF.tlk` also carries `LanguageID = 0` — that is precisely why Allard
  needs a patched exe (see below).

### `swkotor.exe` — Case B, and *why* it is Case B

- 4,042,752 bytes, SHA-256
  `7B961A140667336D22B06686CBDCBE2DDDF8A0EA13C20A27B63BDC57A75D7628`.
  Not one of our three `supported_versions` hashes → the installer's gate
  hard-fails today.
- The SFX text states the exe swap exists to make the engine read `dialogF.tlk`
  for gender-inflected player-character lines. Vanilla only consults
  `dialogF.tlk` for languages the engine knows are gendered; with
  `LanguageID = 0` it never would. **That is the exe's whole job** — it is not
  required for Russian *text* as such.
- **It is not "Steam exe + patches" — it is a different official build.**
  PE timestamp `2004-03-05 00:43:51 UTC`; the Steam exe is
  `2004-02-12 18:15:53 UTC`. The Steam copy additionally carries a `.bind`
  section (SteamStub), which the Russian one does not.

### Hook compatibility — measured, not assumed

Method: map each `hooks.toml` address to a file offset via the PE section
table, compare against the recorded `original_bytes`; when it mismatches,
search all of `.text` for that signature and report the displacement.
Tooling lives in the session scratchpad (`verify-hooks.ps1`, `find-shift.ps1`).

- **All 25 detour signatures are present in the Russian exe, byte-for-byte** —
  but every one is **displaced**, by roughly +96 to +512 bytes. The
  displacement is *not* a single constant and is not monotonic in address, so
  it is a genuine relink, not one inserted block.
- Because the bytes at each hook window are identical, the codegen is the same
  and only layout moved. So this is a **rebasing** problem, not a
  re-reverse-engineering problem.
- **Section layout is identical** to our reference (`.text` @0x401000,
  `.rdata` @0x73D000, `.data` @0x78D000, same `.data` VSize 0xA8498).
- **Data addresses are unchanged.** Proof: our `kIatAilSet3DPlaybackRate`
  (`0x0073D4E8`) resolves in the Russian exe to exactly the
  `mss32.dll!_AIL_set_3D_sample_playback_rate@8` IAT slot. Probes of
  `kAddrGuiManagerPtr`, `kAddrExoInputGlobal`, `kAddrScreenFramePercent` all
  land in `.data` as expected.
- `DINPUT8.dll` is still imported → our `dinput8.dll` proxy loader still
  auto-loads KotorPatcher against this exe.

### Scope of a full exe-support effort

`patches/Accessibility/` references **263 distinct absolute engine addresses**
on non-comment lines (`grep -ohE '0x00[4-7][0-9a-fA-F]{5}'`, deduped):
- **208 in `.text`** (< 0x73D000) — function entries; each needs rebasing.
- **44 in `.rdata`** + **11 in `.data`** — unchanged, no work.

Blocker for producing the rebase table offline: we have no decrypted
byte-reference at those 208 addresses. The Steam exe is SteamStub-encrypted on
disk, and Lane's GoG Ghidra DB is not pulled locally. The practical source is a
one-shot `.text` dump from the *running* game (decrypted in memory), after
which the same signature search that located the 25 hook sites resolves the
rest mechanically.

### Status of the Russian language work (2026-07-25)

Done, building clean, **not yet confirmed in game**:
- `prism::SetSpeechCodepage` / `GetSpeechCodepage` — the ANSI overload's
  hardcoded `CP_ACP` is gone; all narrow→wide conversion funnels through one
  settable codepage. Applies to engine `CExoString` text too, not just our
  tables.
- `Lang::Ru`, `lang_ru`, `acc::strings::CodepageFor(Lang)` (1251 for Ru, 1252
  for the rest), dispatcher wired, pinned from `core_dllmain` before Prism
  starts so the first utterance is already correct.
- Content-based detection: `TlkLooksCyrillic` samples 64 KB of the tlk string
  blob and returns Russian when ≥20% of bytes are ≥0xC0. It runs *before* the
  LanguageID switch and overrides it, so a future Russian repack on a
  different ID works for free. Measured: Allard `dialog.tlk` 77.76%,
  `dialogF.tlk` 77.77%, German `dialog.tlk` 1.29%. Only German was available
  locally to test the false-positive side; French/Spanish carry more accented
  characters but would need to be ~6× more accented than German to trip it.
- `combat_strings.cpp::kRu` — all 52 fields. Engine anchors extracted with
  `kdev combat-strings-extract --tlk <allard>/dialog.tlk`; the four
  deflection fields were hand-derived from 42417 because the extractor
  predates them. Two Russian quirks documented at the table: the hit/miss
  distinction rides an adverb (42043/42044) rather than the verb, and there
  is no status-echo copula, so `status_ist_marker` is a `\x01` sentinel.
- `strings_ru.cpp` — full table, all 647 case labels, matching en/de exactly.
  Machine-translated draft flagged for a native-speaker pass.

Verified mechanically: zero non-ASCII bytes left in the source, no `Id` missing
versus the English table, and identical printf placeholder sequences for all
647 ids (a divergence there would be a crash, not a cosmetic bug).

Tooling used lives in the session scratchpad: `escape-cp1251.ps1` (UTF-8
source → CP1251 escapes, handling the hex maximal-munch trap), `tlkdump.ps1`
(strref lookup + C-literal output), `decode-literals.ps1` (round-trip
proof-reading), `check-specifiers.ps1`. Worth promoting into `tools/kdev` if
Polish follows.

Installer side, also done and building clean:
- `GameLocale.Russian` — deliberately `= 100`, outside the 0..4 tlk language-ID
  range, because Russian is a *detection result* and not a header value.
  `GameLocaleDetector` runs the same CP1251 content probe as the DLL, with the
  constants kept in lockstep and cross-referenced in both files; if the two
  ever disagreed the user would get an installer in one language and speech in
  another. Verified by compiling the shipped `GameLocale.cs` into a scratch
  harness and running it against both real tlks: Allard → Russian (77.8%),
  installed German → German (1.3%).
- `ru.json` (all 145 keys) and `ru` added to `LanguageDetector`'s three tables.
- Guidance instead of bundling: a Russian install gets two extra paragraphs on
  the optional-mods footnote (translation found; K1CP untested per the
  translation's author). Choosing Russian for the installer while the game is
  *not* translated shows a one-off dialog pointing at the author's own channels.
  It never blocks the install.

**Distribution decision (2026-07-25): we do not bundle or download the Russian
translation.** The K1CP precedent does not transfer — K1CP is a GitHub-hosted
open project and what we overlay there is K1CP's own `translation_*` append.tlk
files. Allard 1.72 is © 2024 with no licence grant, derives from SerGEAnt's
Zone of Games localisation (so there are likely two rights-holders), is 166 MB,
and its only stated homes are `vk.com/allardchannel` and `rpgnuke.ru` — neither
of which offers a stable, versioned, hash-verifiable URL an installer could
pin. Users install it themselves; we detect it.

Still open: which of K1CP's two Russian overlays (olegkuz1997 / JayDominus) to
use — currently unwired, so a Russian install gets the English append.tlk like
Italian and Spanish.

### Stage 2 — address-resolver toolchain (built 2026-07-25)

Two new kdev commands, both building clean; full design in `docs/kdev-design.md`.

- **`kdev dump-text`** — captures the running game's decrypted image from
  process memory. Needed because the Steam exe is SteamStub-encrypted on disk,
  so there is no offline byte-reference at our addresses. One launch, no DLL
  required. Includes a decryption sanity check so a dump taken too early fails
  loudly instead of producing a useless reference.
- **`kdev sigscan`** — harvests all 280 address references (264 distinct, 216
  in `.text`) from `hooks.toml` and the C++ sources, builds relocation-tolerant
  signatures with Iced (wildcarding relative-branch and code-pointer operands,
  keeping `.data` displacements concrete), and resolves them against a target
  exe as `unique` / `ambiguous` / `not-found`. It never guesses a best
  candidate — most of these are called as function pointers, so a wrong address
  crashes rather than degrading.

Validated so far by a self-consistency run (same exe as reference and target):
**197/197 signatures resolved uniquely at delta 0**, exercising harvesting,
decoding, wildcarding, uniqueness growth and matching end-to-end.

### Stage 2 results against the Allard exe (2026-07-25)

Reference captured from a **vanilla** process — the `dinput8.dll` loader was
renamed aside for the dump and restored afterwards, because our own detour
trampolines overwrite bytes at exactly the 25 hook sites the cross-check
verifies. Any future dump must do the same.

**`hooks.toml` cross-check: 25/25.** The reference is sound, so the rest of the
numbers mean something.

Of 216 `.text` addresses: **212 resolved uniquely, 1 ambiguous, 3 could not
produce a unique signature.** Displacement ranges from -320 to +640 with a
median of +288, and is locally consistent — neighbouring functions move
together — which is what a relink looks like.

The first pass scored 205/216. The gap was one wrong assumption: that only
`.text` references need wildcarding. Four constructors failed because they
contain `mov [esi], offset vtable` into **`.rdata`**, and `.rdata` is *not*
stable between these builds — its VirtualSize differs (0x4FBFE vs 0x4FC1E), so
everything past the edit shifted. Treating `.rdata` as relocation-sensitive
(while keeping `.data` concrete, which is byte-stable) plus raising the pattern
cap to 256 bytes took it to 212.

An ordinal fallback then settled two more. When a signature matches the *same
number* of sites in both binaries, the sets correspond in address order, so our
address's rank gives the target site. Results are labelled `unique-ordinal` and
never folded into `unique` — it is an inference. It fails safe: if the counts
differ, or our address is not among the reference hits, it declines rather than
guessing. Current standing: **212 unique + 2 by ordinal = 214 of 216.**

Two addresses could not be resolved by content, and neither was safe to guess
(both are called as function pointers):
- `kAddrCSWCMapPinCtor` (0x00692540) — unique in the reference but matched two
  sites in the target, so the ordinal rule did not apply.
- `kAddrCExoInputSetActive` (0x005DF540) — a degenerate signature, matching a
  run of eight 16-byte thunks at 0x004AE7A0..0x004AE810. Growing the pattern
  did not separate them.

**Both settled by cross-reference, 2026-07-25.** The technique: find every
`call rel32` targeting the address in the reference, locate each *call site* in
the target by its surrounding bytes (with all rel32 displacements masked), and
decode the displacement there. Call sites are ordinary, unique code even when
the callee is not, so the duplicate-code problem disappears.

- `kAddrCExoInputSetActive` → **0x005DF710** (+464). 10 of the 12 reference call
  sites resolved and all 10 agreed; the other two sat in windows that could not
  be placed uniquely, and neither dissented. The bytes at the answer are the
  same forwarding thunk (`mov ecx,[ecx+4]; jmp internal`). Note this is nowhere
  near the 0x004AE7xx candidates the signature scan had offered — content
  matching had it wrong, not merely ambiguous.
- `kAddrCSWCMapPinCtor` → **0x00692430** (-272), 3 of 3 call sites agreeing,
  which confirms the delta-plausibility inference the earlier pass had made. The
  bytes match the reference constructor apart from absolute data pointers; the
  rival candidate (0x005FCCA0) diverges 30 bytes in, which is what rules it out.

Both live in a hand-maintained `kXrefTable` in `engine_rebase.cpp`, searched
after the generated table misses — **not** in `engine_rebase_table.inc`, which
`kdev sigscan` overwrites. Standing: 214 generated + 2 by cross-reference = all
216.

Before that, `acc::addr::R` returned **0** for these on a rebased build rather
than the stale reference value: a call through null faults at address 0 and is
obvious in a dump, whereas a stale address lands inside an unrelated function
and corrupts state first. All five call sites are also inside
`__try`/`__except (EXCEPTION_EXECUTE_HANDLER)` blocks, so the degradation was
soft (log and return false) — that safety net stays, it just no longer fires.

### Stage 3 — C++ rebasing (done 2026-07-25, builds clean)

- `engine_rebase.{h,cpp}` — `acc::addr::R(referenceVa)` maps a reference-build
  address to the running build; identity on the reference. The build is
  identified by reading the PE link timestamp out of our own mapped image
  (reference 0x402BC2D9, Allard 0x4047CD47) — no file I/O and no allocation, so
  it is safe from the static initialisers that call it, and the detection is
  cached in a function-local static to avoid any initialisation-order
  dependency.
- `engine_rebase_table.inc` — 214 entries, generated by `kdev sigscan`
  (which now emits it, plus a per-version hooks file, rather than anyone
  transcribing addresses by hand).
- **195 `.text` constants across 32 files** now route through `R()`;
  the 51 `.data` constants were deliberately left as `constexpr`.
- **44 `.rdata` constants** route through `R()` as well — see the Stage 4 note
  below. The original claim here, that `.rdata` needed no attention because we
  held no bare constants in it, was wrong and cost a release candidate.

Verification: 105 TUs recompiled with zero errors (so nothing depended on these
being compile-time constants), and a cross-check confirms every address routed
through `R()` is present in the table except the two known-unresolved.

One bug caught by that cross-check and worth remembering: the first transform
pass used only an upper bound on the address range, so it also rewrote small
struct offsets that happen to be declared `uintptr_t` (0x100, 0xab0, 0xc74).
Harmless on the reference build, where `R()` is the identity — but they would
have returned 0 on Allard. `.text` needs **both** bounds
(0x00401000..0x0073D000).

### Stage 3 — hooks files and the version gate (done 2026-07-25, builds clean)

- `hooks.toml` now carries `[metadata] target_versions` with the three vanilla
  hashes, so it no longer applies to every version the framework sees.
- **`allard.hooks.toml`** — all 25 hooks at rebased addresses. Generated from
  the base file rather than transcribed, because `kdev sigscan` emits only
  `address` + `original_bytes`: `[[hooks.parameters]]`, `skip_original_bytes`,
  `exclude_from_restore` and `consumed_exit_address` all had to be carried
  across, and a missed parameter block hands a handler the wrong arguments
  silently.
- **The 5 `consumed_exit_address` values** are the part sigscan cannot help
  with. Each was rebased by its own hook's delta and then verified: the opcode
  bytes at the rebased address match the reference. For the two shared
  epilogues (`OnHandleInputEvent`, `OnClientHandleInputEvent`) the jump table
  immediately after corroborates it — every entry shifts by the same +240 /
  +400. A wrong exit address here would jump into the middle of an unrelated
  function with a half-unwound stack, so this deserved the extra check.
- Only then: the Allard hash added to `[patch.supported_versions]` in
  `manifest.toml`.

Verified after building: all 25 sites' `original_bytes` match the Allard exe,
all 25 base sites still match the reference image, and the packaged `.kpatch`
carries both hooks files with disjoint `target_versions` (so exactly one is
selected per install).

**Confirmed in game on the Allard exe (2026-07-25).** A live session (patch log
`patch-20260725-204636.log`) showed: hooks installed and firing, the build
detected and `acc::addr::R` remapping correctly (the save-crash guard landed at
the rebased `ImageScale` address), and speech working across the title menu,
in-world navigation, combat and the action menu. Consumed-exit hooks are proven
by the `CONSUMED` / `PAIR-CONSUMED` input lines — those could only be verified
statically before.

Both cross-referenced addresses were exercised: `CExoInput::SetActive` dispatched
four times via `ForceReacquireInput`, and `CSWCMapPin::CSWCMapPin` created a new
user map pin (`UserMarker: drop ok`). Those were the two least-proven values in
the whole table, and both were resolved against candidates that content matching
had either mis-ranked or got outright wrong.

One caveat on the bring-up itself: it only worked after clearing KPatchCore's
cached game identity, which had been reporting the exe as vanilla Steam and
loading the vanilla hooks file. Fixed since — see PR-8 in `docs/upstream-prs.md`.

Build note: adding the Iced package forced a fresh NuGet restore, which
surfaced **CVE-2025-6965** in `SQLitePCLRaw.lib.e_sqlite3` — transitive via
`Microsoft.Data.Sqlite 8.0.0` in vendored KPatchCore, and pre-existing rather
than introduced. There is no patched version (every release up to 2.1.11 is
affected, "None" listed as fixed). Suppressed in `kdev.csproj` with a targeted
`NuGetAuditSuppress` and a written justification. **The same dependency ships
in the end-user installer**, which does not treat warnings as errors and so
never flagged it — worth a separate look.

### Stage 4 — the `.rdata` vtables (2026-07-25)

Stage 2 excluded `.rdata` on the reasoning that we hold no bare `.rdata`
constants, "only vtable references inside functions, which move with their
function". Wrong: **44** of them, and on a rebased build every vtable-identity
check silently failed. Symptom, found by playing rather than by any check we had:
character creation read its class buttons as "control 11" instead of "Männlich:
Soldat". Half the menu system identifies controls by vtable, so options
sub-screens, key mapping, the store, the journal and the level-up panels were all
degraded the same way.

**Why the first Allard session looked clean.** It exercised navigation, combat,
the action menu and save lists — none of which use vtable identity. The evidence
was in that same log all along: 365 `SpecRead: miss` lines against 6 in the Steam
baseline. Nobody was counting them.

**Resolution method.** A vtable cannot be signature-scanned — there is no code at
the address. Two independent methods, required to agree:

- *Operand cross-reference.* The address appears in `.text` as an `imm32` operand
  in the constructor that stores it. Locate that code in the target with all
  operands masked, read the new value.
- *Vtable content verification.* Walk a candidate's first eight entries and
  compare the **functions** they point at byte-for-byte, with operands masked and
  the window truncated at the inter-function padding. That truncation matters:
  many slots hold an empty virtual compiled to a bare `ret 8`, and a fixed-width
  window spills into whatever the linker placed next, which is exactly what
  differs between builds. Identity comes from contents, not position.

44 of 44 resolved, no disagreements: 23 by both methods, 19 by content alone, 2
by operand alone (an import-table entry and the upgrade-slot table — neither has
function pointers to compare).

Layout, for reference: below ~`0x751000` everything maps to itself; above,
everything shifts by +24. Each was resolved individually anyway — "the
neighbours moved by 24" is an inference, and this is not a file where inference
is cheap.

The map lives in `engine_rebase_rdata.inc`, separate from the generated `.text`
table because `kdev sigscan` cannot produce it yet. **Teaching sigscan this pass
is open work** — until then the two scan methods above are the reproducible
record, and the report's `data … .rdata` rows are the checklist.

One implementation trap: `menus_extract.cpp`'s override table must stay
`constexpr` and map at the comparison instead. A runtime initialiser on that
function-local static makes the function need object unwinding, which its `__try`
blocks forbid (C2712).

**Regression signal, for next time:** `grep -c "SpecRead: miss"` on the session
log. 6 on Steam, 365 on Allard before the fix, 53 after (all the benign
"image-only button has no inline text" path, which is what the Steam baseline's 6
are too).

### Speech encoding — the parked blocker is already solved

The old note assumed Cyrillic would need new Prism plumbing. It does not:
`prism::Speak(const wchar_t*, bool)` already exists (`prism.h`), and the ANSI
overload's only limitation is that it hardcodes `CP_ACP`. The minimal, in-style
fix is a per-language speech codepage (CP1251 for Russian, CP1250 for Polish)
used by the ANSI overload, so `strings_ru.cpp` can use CP1251 byte escapes
exactly the way `strings_de.cpp` uses CP1252 ones. That fixes Polish too.

Remaining Russian-strings work is the same shape as fr/it/es: a ~840-line
`strings_ru.cpp` over the 652 `Id` values, a `kRu` table in
`combat_strings.cpp`, `Lang::Ru` + dispatcher, installer `GameLocale` +
`Locales/ru.json`.

---

## Implementation plan — Polish as language 6 (Case A path)

Verified against the code. `fr/it/es` are **full machine-translated tables**
(a big `switch` over every `Id`), so Polish mirrors that exactly.

Code seams:
- `strings.h` — add `Pl` to `enum class Lang`; declare
  `namespace lang_pl { const char* Get(Id); }`.
- `strings.cpp` — dispatcher: `case Lang::Pl: return lang_pl::Get(id);`
  (note current default is `Lang::De`, overridden by `SetLanguage`).
- **new `strings_pl.cpp`** — full Polish table (hundreds of `Id` cases; the big
  artifact; machine-translated draft matching the fr/it/es quality bar, flagged
  for later human review like fr/it/es).
- `combat_strings.cpp` — a Polish combat table (`kPl`), like `kFr`.
- `core_dllmain.cpp` — `DetectLanguageFromTlk`: `case 5: detected = L::Pl;`.
- Installer — add Polish to `GameLocale`, add `Locales/pl.json`, wire the
  `dialog.tlk` langID→locale detection (5 → Polish).

### Open design decision — Polish text encoding (RESOLVED 2026-07-25)
- Polish letters (ą ć ę ł ń ó ś ź ż) are **not in Windows-1252**.
- The fr/it/es tables use Windows-1252 byte escapes and rely on Prism's ANSI
  path (`MultiByteToWideChar(CP_ACP, …)`), which only renders because a
  French/etc. user's `CP_ACP` is 1252.
- Polish is **Windows-1250**, and many Polish players run **English Windows**
  (`CP_ACP` = 1252) → 1250 byte escapes would garble for them.
- **Resolution:** give the ANSI overload a per-language speech codepage instead
  of hardcoded `CP_ACP` (CP1250 for Polish, CP1251 for Russian, CP1252 for the
  existing tables — a no-op for them on their native installs, and a *fix* on
  non-native ones). `prism::Speak(const wchar_t*, bool)` already exists as the
  fallback if a wide table is ever preferred. See the Russian section.

---

## Installer question — what "install the Polish translation" means

Two very different scopes to resolve with the maintainer:
- **(a) Support a Polish install** — recognize a Polish `dialog.tlk` (langID 5),
  set our locale to Polish, install *our* Polish accessibility strings.
  Self-contained; no third-party redistribution. Doable immediately.
- **(b) Also bundle/fetch the community Polish *game* translation** (the
  `dialog.tlk`/override/fonts) for users who don't already have it — analogous
  to how the K1CP installer overlays `translation_{german,french,russian}`
  append.tlk (see `installer/.../ModInstallers/K1cpInstaller.cs` +
  `docs/installer.md`). Raises sourcing + **licensing** questions (which pack,
  can we redistribute, or download at install time).

Maintainer's note (2026-07-21): idea that the installer could let users **add
language packs**, or **auto-download the fitting one to layer onto K1CP**. That
lines up with scope (b) and the existing K1CP per-locale overlay mechanism —
worth designing as a general "language pack" step once we have the sources and
their licences. Russian would slot into the same mechanism (plus Cyrillic fonts
and the resident-tool conflict caveat above).

---

## Decision — the two Russian paths

Both paths need the same language work (detection + `strings_ru.cpp` +
per-language codepage + installer locale). They differ only in what happens to
`swkotor.exe`.

### Path A — Russian text on the stock exe (small, shippable)
Install everything from the Allard payload **except `swkotor.exe`**: both
`.tlk`s, the Cyrillic `Override\` fonts, `TexturePacks\swpc_tex_gui.erf`, the
subtitled `.bik` cutscenes.
- Exe hash stays one of our three known-good values → **no gate change, no
  rebasing, all 25 detours attach normally**.
- Cost to the player: player-character dialogue lines are not gender-inflected
  (the single thing Allard's exe buys). Everything else is fully Russian.
- Risk: low and *self-announcing* — if it were wrong, it would be wrong in
  text, not in a dead mod.

### Path B — support Allard's exe as a fourth version (large)
- Add the hash to `[patch.supported_versions]`, **and** ship a rebased address
  set: 25 hook addresses (already located, see above) plus the 208 `.text`
  constants. The 55 `.rdata`/`.data` constants stay as-is.
- Prerequisite: a decrypted `.text` byte-reference at our 208 addresses, taken
  as a one-shot dump from the running game, to drive the signature search.
- **Do not add the hash without the rebase.** With `AllowVersionMismatch`, the
  detours fail `VerifyBytes` and go inert one by one; the installer reports
  success and the player gets a silently half-dead mod. That is the exact
  failure mode this document was written to avoid.

**Recommendation: ship Path A now, keep Path B as a follow-up.** Path A gets
Russian players a fully working mod immediately; Path B trades a large,
verification-heavy rebase for gendered PC lines only.

## Still to gather (Polish only)

All three original items are settled — the distribution was supplied directly,
and its `swkotor.exe` went through the same characterisation as the Russian one
(see "Polish — measured"). What remains:

1. **One Polish combat capture** to confirm `kPl`. See the combat-anchors note
   above for the exact grep.

## Sources
- KOTOR Polish translation (PCGamingWiki): https://community.pcgamingwiki.com/files/file/2516-star-wars-knights-of-the-old-republic-polish-translation/
- KOTOR PL — Biblioteka Ossus: https://ossus.pl/biblioteka/Knights_of_the_Old_Republic
- Star Wars: KOTOR [Polish] — Internet Archive: https://archive.org/details/kotor-pl
- Актуальные русификаторы для Kotor 1-2 — BioWare Russian Community: https://forum.bioware.ru/topic/34566/
- SW: KoTOR — Русификатор, Починка, Моды (Steam guide): https://steamcommunity.com/sharedfiles/filedetails/?id=2087027088
- How to make KOTOR 1 display DBCS languages — Deadly Stream: https://deadlystream.com/topic/11714-how-to-make-kotor-1-display-dbcs-languages/
