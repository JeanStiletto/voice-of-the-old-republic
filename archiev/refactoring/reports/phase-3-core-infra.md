# Phase 3 scan — core lifecycle, settings, update checking, small watchers

Scope (39 files, 5015 lines total): core_dllmain.cpp (340), core_tick.cpp
(452) / .h (13), core_settings.cpp (16) / .h (80), update_checker.cpp (584) /
.h (52), update_checker_http.cpp (445) / .h (53), mod_settings_store.cpp
(153) / .h (38), mod_version.h (15), save_crash_guard.cpp (152) / .h (25),
state_overrides.cpp (172) / .h (25), msg_router.cpp (184) / .h (54),
strfmt.h (45), party_cache.cpp (112) / .h (23), party_leader_announce.cpp
(99) / .h (17), trap_watch.cpp (284) / .h (52), endar_softlock.cpp (274) /
.h (42), same_name_suffix.cpp (227) / .h (69), bringup_announce.cpp (199) /
.h (56), locked_recall.cpp (161) / .h (52), stealth_watch.cpp (145) / .h
(20), intro_skip.cpp (123) / .h (40), spectator_scene.cpp (89) / .h (33).

Method: full `Read` of every file in the batch. Read `core_tick.cpp`
completely first and used its `Dispatch()` body as the ground truth for
"what fires per tick" before judging anything else in the batch as live or
dead. For every file whose namespace does not match its filename
(`party_cache.h` → `acc::combat`, `endar_softlock.*` → `acc::endar`,
`same_name_suffix.*` → `acc::narration`, `spectator_scene.*` →
`acc::spectator`), ran `grep -rn "acc::<real-namespace>::" --include=*.cpp
--include=*.h | grep -v "^<own file>"` to find every external call site
before treating anything in that file as unused — the exact command is
repeated per finding below where relevant. Cross-checked `exports.def` and
`hooks.toml` are not implicated by anything in this batch (no exported
symbols, no hook targets here beside the three detours in core_dllmain.cpp /
save_crash_guard.cpp, which are already wired through `OnRulesInit` /
`DllMain` and were traced to their install sites). Grepped every
`reinterpret_cast` in the batch (6 files, 21 occurrences) and confirmed each
one sits inside `__try`/`__except` or is pure trampoline byte-patching (not
a game-object field read). Grepped `strings::Get(` in every file and
checked the few store-then-compare sites for null-checks. Grepped every
`0x[hex]` literal in every file against `engine_offsets_fields.h` and the
narrower aggregators already in scope at each call site.

## Section A — general low-level cleanup

### A1 — dead includes `<cstdint>` / `<cstdlib>` (update_checker.cpp:40, :42)

Neither header's symbols are used anywhere in the file. Verified by
grepping every fixed-width type (`uint32_t`, `int32_t`, `uint8_t`,
`uint16_t`, `int64_t`, `uint64_t`) and every common `<cstdlib>` symbol
(`atoi`, `strtol`, `rand`, `malloc`, `free`, `exit`, `getenv`, etc.) — zero
hits for either group in `update_checker.cpp`. (The file does use `Vector`,
`DWORD`, `FILE*`, `std::thread`, `std::atomic` — none of those come from
these two headers.)

Proposed change: remove both `#include` lines.
Risk: mechanical.
Estimated delta: -2 lines.

### A2 — dead include `<cstdint>` (update_checker_http.cpp:19)

Same check as A1, same result: no fixed-width-int symbol appears anywhere
in `update_checker_http.cpp`. (`<cstdlib>` on the next line down *is* used —
`strtol` in `ParseVersion` — so that one stays.)

Proposed change: remove the line.
Risk: mechanical.
Estimated delta: -1 line.

### A3 — dead function `ExtractTagName` (update_checker_http.cpp:349-354)

```cpp
bool ExtractTagName(const std::string& json, char* out, size_t outCap) {
    char raw[128] = {};
    if (!ExtractRawTagName(json, raw, sizeof(raw))) return false;
    StripTagToVersion(raw, out, outCap);
    return out[0] != '\0';
}
```

Not declared in `update_checker_http.h` (only `ExtractRawTagName` is), and
`grep -rn "ExtractTagName" --include=*.cpp --include=*.h` finds exactly one
call site — its own definition — plus one comment elsewhere in the same
file that name-checks it. `update_checker.cpp` calls `ExtractRawTagName`
directly and does the tag-stripping itself via `StripTagToVersion` at the
one call site that needs the raw tag preserved (for the direct-download
URL). This is exactly the "post-split residue" the brief flagged as likely
here — candidate 9 (Phase 1) moved this helper out with the rest of the
JSON plumbing, but the caller that used to need the stripped-in-one-step
form apparently didn't survive the split, or never existed after it.

Proposed change: delete the function (and the two comment references to it
at the top of the file and inside `ParseVersion`, which only exist to
explain its behaviour).
Risk: mechanical (unreferenced, not declared in the header, compiler would
flag it as an unused static were the file's functions all internal-linkage
— they're not, so nothing catches this automatically; that's exactly why
it survived the split).
Estimated delta: -6 lines (function) + 2 comment-line edits.

### A4 — raw hex literal where a named constant is already in scope (state_overrides.cpp:152)

```cpp
int usable = *reinterpret_cast<int*>(
    reinterpret_cast<unsigned char*>(gameObject) + 0x328);
```

`engine_area.h:459` already declares
`constexpr size_t kPlaceableUsableOffset = 0x328;  // "Useable" GFF flag`,
and `state_overrides.cpp:8` already includes `engine_area.h` (for
`kObjectTagOffset`), so `kPlaceableUsableOffset` is in scope at this call
site today — the file just doesn't use it. (The companion literal on the
next line, `0xd4` for the animation field, has no existing named constant
anywhere in `engine_offsets*.h` or `engine_area.h` — that one is not a
duplicate-of-existing-constant case, just an unnamed offset local to this
diagnostic log line, and I'm not proposing a change there.)

Proposed change: replace `0x328` with `acc::engine::kPlaceableUsableOffset`
(or the bare `kPlaceableUsableOffset` if it's file-scope, matching how
`kObjectTagOffset` is already used elsewhere in this file).
Risk: mechanical — same value, named.
Estimated delta: 0 net lines.

### A5 — hardcoded startup greeting bypasses `strings::Get(Id)` (core_dllmain.cpp:161-164)

```cpp
char greeting[128];
snprintf(greeting, sizeof(greeting),
         "Voice of the Old Republic loaded, version %s", acc::kModVersion);
prism::Speak(greeting, /*interrupt=*/true);
```

This is the exact shape the brief calls out: a literal built via `snprintf`
into a buffer that is then spoken, bypassing the centralised strings table.
`DetectLanguageFromTlk()` + `acc::strings::SetLanguage(lang)` run
immediately before this call (line ~281-284, same function chain in
`OnRulesInit`), so the language IS already known at speak time — the
string table is just never consulted. Every player, regardless of detected
locale (German, French, Italian, Spanish, Russian), hears this exact
English sentence on every launch. `grep -ni "loaded, version"
strings.h strings_*.cpp` confirms there is no existing `Id` for it.

Proposed change: add an `Id` (e.g. `Id::FmtModLoaded`, `"%s"`-style like
`FmtUpdateAvailable`) with per-locale translations, and format through
`acc::strings::Get()` the same way `update_checker.cpp` does for its
version-carrying announcements. Open question for the user: whether "Voice
of the Old Republic" (the mod's proper name) should stay untranslated
inside each locale's template, or be part of the localized sentence too —
that's a translation-content decision, not a code one.
Risk: low (touches every launch's first utterance; trivial to verify by ear
in any configured locale).
Estimated delta: +1 `Id` entry x 6 locale tables, ~3 line changes in
core_dllmain.cpp.

### A6 — small duplication: session-open boilerplate (update_checker_http.cpp:37-59, :206-220)

`OpenSession()` and `HttpDownloadUrlToFile()` both do
`WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)` followed by
`WinHttpConnect(session, <host>, <port>, 0)` with matching failure-log-and-
cleanup on each step — ~14 lines duplicated near-verbatim. The one real
difference is the port: `OpenSession` hardcodes
`INTERNET_DEFAULT_HTTPS_PORT`, while `HttpDownloadUrlToFile` needs whatever
port `WinHttpCrackUrl` resolved from the target URL (`uc.nPort`), which is
why it doesn't just call `OpenSession` today.

Proposed change: add an optional `port` parameter to `OpenSession`
(defaulting to `INTERNET_DEFAULT_HTTPS_PORT` so the two existing call sites
in `update_checker.cpp` don't change), and have `HttpDownloadUrlToFile`
call it with `uc.nPort`.
Risk: mechanical (same WinHTTP calls, same error handling, same log tags —
just one shared call site instead of two).
Estimated delta: -10 lines net.

### A7 — three of `core_settings.h`'s four pillar structs have zero consumers

`grep -rn "\.pillar2\.\|\.pillar3\.\|\.pillar4\.\|\.cross\." --include=*.cpp`
(run across the whole `patches/Accessibility/` tree, not just this batch)
returns nothing — `Pillar2Settings`, `Pillar3Settings`, `Pillar4Settings`
and `CrossPillarSettings` (core_settings.h:31-68, ~38 lines) are never read
anywhere. Only `Pillar1Settings` has consumers (`audio_cue_player.cpp`,
`spatial_change_detector.cpp`), and even there `pillar1.voiceBudgetMax`
(core_settings.h:24) is unread while its siblings are (`pillar1.cueLandmark`
is also unread, but the file already documents that at
`audio_cue_player.cpp:59`: "cueLandmark stays in settings as inert" — so
that one is a deliberate no-op, not an oversight).

This is NOT a dead-code deletion candidate — the header's own comment says
"Phase 7 (deferred) replaces this with config-file-backed mutable state,"
so the struct is a forward-declared settings surface, not leftover
scaffolding from something already built. Flagging for the user's
awareness only: the pillar2/3/4/cross behaviour these fields describe
(room/area announcement toggles, guidance toggles, cycle category toggles,
combat-verbosity/cutscene-cues toggles) is currently hardcoded in consumer
code rather than reading these flags, so today they're documentation of
intent, not live settings. `voiceBudgetMax` is the one loose thread inside
the otherwise-consumed `Pillar1Settings` — worth a one-line note to
whoever picks up Phase 7's config UI that it has no reader yet.

Proposed change: none from me — recording for awareness per the brief's
"flag, don't fix" rule on anything that isn't a clean mechanical call.
Risk: n/a (no change proposed).

### A8 — `InvalidatePartyCache()` has zero callers (party_cache.cpp:108-110, party_cache.h:21)

`grep -rn "InvalidatePartyCache" --include=*.cpp --include=*.h` finds only
the declaration and the definition — no call site anywhere in the tree.
The header's own doc comment already calls this out as designed-in slack:
"Force refresh on next call. **Optional** — time-based backstop catches
roster changes anyway." So this reads as an intentionally-unused escape
hatch for a future recruit/dismiss hook, not orphaned code from a removed
caller.

Proposed change: none — flagging per the same "recorded, not fixed"
treatment as A7, since the header text makes the design intent explicit.
Risk: n/a (no change proposed).

## Section B — AI-pattern findings

### B1 — dead null-checks on `strings::Get()`, one with an unreachable hardcoded fallback (same_name_suffix.cpp:75, trap_watch.cpp:134, trap_watch.cpp:232)

`acc::strings::Get(Id)` (strings.cpp:15-25) always returns a valid
`const char*` — worst case `""` on an unhandled `Lang` — never `nullptr`.
Three call sites in this batch still null-guard the result:

```cpp
// same_name_suffix.cpp:73-75
const char* word =
    acc::strings::Get(acc::strings::Id::ContainerEmptySuffix);
if (!word || word[0] == '\0') return;
```

```cpp
// trap_watch.cpp:132-134
const char* fmt =
    acc::strings::Get(acc::strings::Id::FmtAnnounceWithClock);
if (fmt && fmt[0]) return acc::strfmt::Format(fmt, label, clock, m);
```

```cpp
// trap_watch.cpp:229-232
const char* fmt =
    acc::strings::Get(acc::strings::Id::FmtTrapDetected);
std::string label = acc::strfmt::Format(
    (fmt && fmt[0]) ? fmt : "Falle entdeckt: %s", name);
```

The third one compounds the pattern: I checked all six locale tables
(`grep -n "FmtTrapDetected" strings_*.cpp`) and every one of them returns a
non-empty string, so the `"Falle entdeckt: %s"` fallback is not just a
null-check on something that can't be null — it's a hardcoded German
literal (bypassing centralisation, same shape as A5) sitting behind a
branch condition that can never actually be taken. Same check for
`FmtAnnounceWithClock` and `ContainerEmptySuffix` — both non-empty in all
six tables too, so all three `[0]` empty-string checks are equally
unreachable today, not just the `nullptr` half of each condition.

Proposed change: drop the `fmt &&` / `!word ||`-style null half of each
check (Get() truly cannot return null). Whether to keep the `[0]`
empty-string half as future-proofing against an accidentally-blanked
translation table entry, or drop that too since it's unreachable today, is
a judgement call I'm leaving to the user. If trap_watch.cpp's fallback
stays at all, it should route through `strings::Get` with a real `Id`
rather than a bare German literal, consistent with A5.
Risk: mechanical for the null-check removal; low for deciding whether to
keep/replace the fallback string.
Estimated delta: -3 to -6 lines depending on how much of the guard is kept.

## Findings (possible bugs — user decides)

None found in this batch. I specifically audited for the cross-batch
missing-SEH-guard pattern the brief flags as the sweep's most important
finding: every `reinterpret_cast` in these 39 files (`state_overrides.cpp`
x6, `save_crash_guard.cpp` x7, `core_dllmain.cpp` x4, `msg_router.cpp` x1,
`trap_watch.cpp` x2, `stealth_watch.cpp` x1 — `grep -c reinterpret_cast`
per file) is either (a) inside `__try`/`__except`
(`state_overrides.cpp::AppendStateLabel`, `msg_router.cpp::
OnAppendToMsgBuffer`, `trap_watch.cpp::TriggerIsTrap`,
`stealth_watch.cpp::ReadLeaderStealth`) or (b) pure executable-trampoline
byte-patching that never dereferences a live game-object pointer
(`core_dllmain.cpp::InstallMouseGuard`, `save_crash_guard.cpp::
InstallSaveScreenshotGuard` — these write machine code into a
`VirtualAlloc`'d block and patch a 5-byte JMP at a known function entry;
there is no `CSWSObject`/`CSWSCreature`/etc. field read to guard). Every
other engine-state read in the batch goes through the wrapped accessor
functions in `engine_area.h` / `engine_player.h` / `engine_reads.h`
(`GetObjectPosition`, `GetObjectTag`, `GetPartyMembers`, `ReadGlobalNumber`,
`LookupTlk`, etc.) rather than a raw pointer cast in this batch's own code,
so this batch's own files have nothing further to guard — whatever
guarantees those accessor functions carry live in files outside this
batch's scope.

## Candidate 28 — narrow-header include opportunities

- `update_checker.cpp:48` includes `engine_offsets.h` for `Vector` only
  (one use, `PollF5`'s `Vector pos;`) — `engine_offsets_types.h` would do.
- `msg_router.cpp:7` includes `engine_offsets.h` for `CExoString` + `Vector`
  (comment already says so) — both live in `engine_offsets_types.h`.
- `party_leader_announce.cpp:3` includes `engine_offsets.h` for `Vector`
  only (`Vector unused;` in `Tick()`) — `engine_offsets_types.h` would do.
- `trap_watch.h:31` includes `engine_offsets.h` for `Vector` only (used in
  the `PeekFreshMine` signature and `TrackedTrap::pos`) —
  `engine_offsets_types.h` would do.

`core_tick.cpp`, `state_overrides.cpp`, `party_cache.cpp`,
`trap_watch.cpp`, `endar_softlock.cpp`, `same_name_suffix.cpp`,
`stealth_watch.cpp` and `spectator_scene.cpp` all include `engine_area.h`
and/or `engine_player.h` directly (not the raw `engine_offsets.h`), which
is already the correct one-level-down include per the brief's own
aggregator list — no further narrowing available at this level without
also splitting those two aggregators, which is out of this batch's scope.

## Files scanned with nothing to report

- core_tick.cpp, core_tick.h — the dispatcher; used as the ground truth
  for every liveness check in this report rather than a scan target itself,
  and nothing in it looked stale, unreachable, or duplicated on its own
  terms (its slow-tick watchdog and dispatch order are both explicitly
  do-not-touch per the brief).
- core_settings.cpp
- update_checker.h, update_checker_http.h
- mod_settings_store.cpp, mod_settings_store.h
- mod_version.h
- save_crash_guard.cpp, save_crash_guard.h
- state_overrides.h
- msg_router.h
- strfmt.h
- party_leader_announce.h
- endar_softlock.cpp, endar_softlock.h
- same_name_suffix.h
- bringup_announce.cpp, bringup_announce.h
- locked_recall.cpp, locked_recall.h
- stealth_watch.cpp, stealth_watch.h
- intro_skip.cpp, intro_skip.h
- spectator_scene.cpp, spectator_scene.h
