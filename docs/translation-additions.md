# Translation additions — Polish & Russian (parked)

**Status: PARKED (2026-07-21).** Waiting on the Polish and Russian beta testers
to send the direct links to the exact community translations they installed.
Their actual files are the ground truth for whether each translation is
"Case A" (safe) or "Case B" (touches the exe) — see the two-case model below.
Everything found so far is recorded here for when we pick this back up.

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

## Polish — findings

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

**The red flag is NOT the hash gate.**
- The current active KOTOR 1 Russian translation is **by Allard** (for Steam/GOG;
  hosted around BioWare Russian Community `forum.bioware.ru` and `swkotor.ru`).
- It needs custom **Cyrillic bitmap fonts** (Odyssey uses font sheets + TXI, not
  font files).
- Repeatedly described as a **tool you must keep running until you quit the
  game** → smells like a **resident runtime patcher / injector**. If so, the
  Russian tester's "problems" are most likely a **two-injectors-in-one-process
  conflict with our `dinput8.dll` → KotorPatcher injection**, not a version-hash
  rejection. Completely different fix.
- **Russian LanguageID: unknown** — not a standard BioWare ID. Russian tlks
  often reuse an existing slot (e.g. 0 = English, re-encoded) or a custom ID.
  Need the tester's `dialog.tlk` to know. Either way our detector currently
  lands on English speech, not a crash.
- We currently have **no Russian anywhere in our stack**: no `Russian` value in
  the installer `GameLocale` enum, no strings-system language, no installer
  locale JSON. Supporting it is more than a hash edit.

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

### Open design decision — Polish text encoding (NOT mechanical)
- Polish letters (ą ć ę ł ń ó ś ź ż) are **not in Windows-1252**.
- The fr/it/es tables use Windows-1252 byte escapes and rely on Prism's ANSI
  path (`MultiByteToWideChar(CP_ACP, …)`), which only renders because a
  French/etc. user's `CP_ACP` is 1252.
- Polish is **Windows-1250**, and many Polish players run **English Windows**
  (`CP_ACP` = 1252) → 1250 byte escapes would garble for them.
- So Polish likely needs an **explicit-codepage or UTF-16 speak path in Prism**
  rather than piggybacking on `CP_ACP`. Check Prism's speak API before choosing
  the `strings_pl.cpp` encoding. This decision also applies to any future
  Cyrillic (Russian) support.

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

## What unblocks this (to gather from the testers)

1. **Direct links** to the exact Polish and Russian translations they installed.
2. Their `<install>/logs/patch-*.log` from a session (shows whether our hooks
   attached, the detected LanguageID, and any VerifyBytes mismatches).
3. Ideally the modified `swkotor.exe` from each (so we can compute SHA-256 and
   byte-diff our 26 hook sites — the only safe way to decide Case B whitelisting).
4. The Russian tester's precise symptom: **install refused (hash)** vs **crash on
   launch (injector conflict)** vs **ran but spoke English (tlk-only)**.

## Sources
- KOTOR Polish translation (PCGamingWiki): https://community.pcgamingwiki.com/files/file/2516-star-wars-knights-of-the-old-republic-polish-translation/
- KOTOR PL — Biblioteka Ossus: https://ossus.pl/biblioteka/Knights_of_the_Old_Republic
- Star Wars: KOTOR [Polish] — Internet Archive: https://archive.org/details/kotor-pl
- Актуальные русификаторы для Kotor 1-2 — BioWare Russian Community: https://forum.bioware.ru/topic/34566/
- SW: KoTOR — Русификатор, Починка, Моды (Steam guide): https://steamcommunity.com/sharedfiles/filedetails/?id=2087027088
- How to make KOTOR 1 display DBCS languages — Deadly Stream: https://deadlystream.com/topic/11714-how-to-make-kotor-1-display-dbcs-languages/
