# Phase 3 scan — localisation subsystem (strings)

Scope: `strings.h` (2003 lines — `Id` enum, ~652 enumerators, plus `Lang` enum
and doc comments), `strings.cpp` (43 lines — the language dispatcher),
`strings_en.cpp` / `strings_de.cpp` / `strings_fr.cpp` / `strings_it.cpp` /
`strings_es.cpp` / `strings_ru.cpp` (844-868 lines each — six parallel
`Get(Id)` switch tables).

Method: targeted extraction and comparison, not a linear read (per the
brief). Concretely:
- Extracted the 652 `Id` enumerators from `strings.h` with a comment-stripped
  positional regex over the enum body (`strings.h:27`-`1961`), verified
  against the `Count_` sentinel.
- Extracted every `case Id::X:` label from all six tables via `grep -oE`,
  cross-checked with a Perl parser that understands C++ string-literal
  syntax (adjacent-literal concatenation, multi-line returns, embedded
  semicolons *inside* string bodies, and genuine case-fallthrough) so the
  id -> text map per language is exact, not a naive semicolon split. (A
  naive first pass mis-split several entries at an in-string semicolon in
  `TutHintCycleTargets`'s text — caught and fixed before trusting any
  downstream diff.)
- Diffed the six id-sets against the enum set (`comm -23` / `-13`).
- Grepped the whole patch tree (287 `.cpp`/`.h` files, flat directory, no
  subfolders) for every enumerator's bare name, `Id::Name`, and `S::Name` —
  `using S = acc::strings::Id;` is a local alias used in 16 files
  (`combat_query.cpp`, `cycle_input.cpp`, `engine_compass.cpp`,
  `examine_view.cpp`, `help.cpp`, `interact_dispatch.cpp`,
  `menus_keybinds.cpp`, `passive_narrate.cpp`, `unified_action_menu.cpp`,
  `view_mode.cpp`) — confirmed `S` is never anything else in this codebase.
  Also checked `using namespace acc::strings;` sites (`menus_pazaakdeck.cpp`,
  `minigame_pazaak.cpp` — these still spell `Id::Name`, so the bare-name
  grep already covers them) and confirmed there is no reflective loop over
  `Id::Count_` that would call `Get()` on every id generically (the only
  `Count_` usages are sentinel/terminator checks, listed and inspected).
- For every `Fmt*` id with a `%` placeholder (194 across the tables),
  extracted the ordered printf-specifier sequence per language and diffed
  pairwise across all six.
- Scanned every id's text per language for leading space, trailing space,
  and internal double-space, and diffed for cross-language inconsistency.
- Sanity-checked the whole approach against a known-live id
  (`CategoryDoor`) before trusting the "zero hits" result on any candidate.

## Section A — general low-level cleanup

### A1 — 37 dead `Id` enumerators (strings.h, six tables)

Every id below has zero references anywhere in the patch tree outside
`strings.h`'s enum declaration and the six tables' `case Id::X:` lines —
verified by bare-identifier grep (not just `Id::`/`S::`, so no alias or
`using namespace` trick hides a caller) across all 287 `.cpp`/`.h` files.
`strings::Id` is never cast to/from `int` and never persisted (no
`static_cast<int>`/`static_cast<Id>` site found), so removing an entry is
compiler-checked — any missed reference fails the build, it can't silently
shift another id's meaning.

Removing an id means deleting its `strings.h` enumerator line plus its
`case Id::X:` line in all six `strings_*.cpp` (37 x 7 = 259 lines total,
minus doc-comment lines that can be trimmed at the same time).

Risk for all of Group A1: **mechanical** (compiler-checked deletion, no
runtime dependency found). Estimated delta: roughly -350 to -450 lines
across the seven files once the associated doc comments are trimmed too.

**Group 1 — combat callouts, superseded by `combat_strings.cpp`.**
Confirmed: `combat_strings.cpp` has its own independent `MsgStrings`
tables (`kEn`, `kFr`, ...) that read the engine's own combat-log anchors
and are the live combat-speech path (per `strings_fr.cpp`'s own header:
"Combat speech is fed by combat_strings.cpp::kFr"). These `Id::*` entries
look like an earlier, superseded design for the same purpose:
- `FmtAttackHit` (strings.h:1105), `FmtAttackMiss` (1106),
  `FmtAttackCrit` (1107), `FmtAttackDeflected` (1108)
- `FmtSavingThrowSucceeded` (1114), `FmtSavingThrowFailed` (1115),
  `SaveTypeFort` (1116), `SaveTypeReflex` (1117), `SaveTypeWill` (1118)

**Group 2 — dialog-reply "unavailable" gating cue, documented but never wired.**
- `DialogReplyUnavailable` (strings.h:1123), `FmtDialogReplyUnavailableRow`
  (1128)
The comment at strings.h:1121 says this is "a suffix appended via
enrichRow when a reply is gated (active=0)". `menus_listbox.cpp`'s dialog
reply spec sets `enrichRow = nullptr` (no dialog-reply-specific enrichRow
exists), and the actual live path — `menus_monitors.cpp`'s
`DialogReplyState` mechanism — announces each row via the generic
`FmtContainerItemAt` (menus_monitors.cpp:887), with no "unavailable" word
at all. **See the accessibility-gap item under "Findings" below — this
one is more than dead code.**

**Group 3 — superseded by the `DamageLevel` classification.**
- `TargetIsDead` (strings.h:943). `examine_view.cpp:171` and
  `combat_query.cpp:332` both speak `S::DamageLevel5Dead` for the "dead"
  word today (a 6-level 0-5 damage classification); `TargetIsDead` reads
  like an earlier boolean-flag design that predates it.

**Group 4 — pre-unified-action-menu design.**
- `FmtActionBarOpened` (strings.h:303), `FmtActionBarFired` (306),
  `ActionBarColumnEmpty` (305)
Their sibling `FmtActionBarColumnEmpty` (*with* a column number) is very
much alive — used in `interact_dispatch.cpp` (3 call sites) and
`unified_action_menu.cpp` (2 call sites). These three read as leftovers
from the action-bar-column design that the unified action menu replaced
(memory: "Unified action menu — one menu replaced radial+target+personal").

**Group 5 — minigame cues with no caller in their own file.**
- `SwoopRaceObstacleNear` (strings.h:1421) — `minigame_swoop_race.cpp`
  speaks `FmtSwoopRaceGear`, `SwoopRaceStarted`, `SwoopRaceControls`,
  `FmtSwoopRaceTime`, `SwoopRaceEnded` (all adjacent to this one in both
  the enum and the source file) but never this one.
- `TurretTargetLost` (strings.h:1466) — `minigame_turret.cpp` speaks
  `TurretGameStarted`, `TurretGameControls`, `TurretGameEnded`,
  `FmtTurretTarget`, `FmtTurretDestroyed`, `TurretNoTargets` but never
  this one.

**Group 6 — map/pin edge-case guidance, no caller anywhere.**
- `MapPinShiftDashHint` (strings.h:64), `MapCursorOffPath` (1167),
  `MapCursorTransitionDoor` (1200), `FmtMapCursorCorridor` (1168)
Sibling `FmtMapCursorCorridorDir` (the "Korridor"-noun-dropped terser
variant, per its own inline comment) is live; `FmtMapCursorCorridor` is
not — reads like the terser variant fully replaced it and the older one
was never removed.

**Group 7 — store cue words, superseded by the credit-amount variants.**
- `StoreSold` (strings.h:1250), `StoreBought` (1251)
`menus_store.cpp:419-420` only calls `FmtStoreBoughtFor` /
`FmtStoreSoldFor` (the versions with the credit amount). The bare,
amount-less versions are unused.

**Group 8 — portrait single-arrow fallback; comment claims a consumer grep can't find.**
- `PortraitArrowPrev` (strings.h:666), `PortraitArrowNext` (667)
strings.h:662 says these are "kept for the directional-fallback path
(single-arrow chains where the anchor consolidation isn't possible)".
No such fallback call site exists anywhere in the tree (checked
`menus_extract.cpp`, which does use the sibling `FmtPortraitArrowId`).
Flagging the exact contradiction rather than asserting dead: either the
fallback path was never implemented, or it existed once and was removed
without updating the comment.

**Group 9 — combat-queue verb-byte words with no matching case.**
- `QueueVerbMove` (strings.h:1090), `QueueVerbHeal` (1091),
  `QueueVerbCutscene` (1093)
`combat_queue.cpp` cases `QueueVerbAttack` / `CastForce` / `ItemCast` /
`Equip` / `Unequip` / `UseTalent` / `Unknown` from the same themed group
(strings.h:1085-1094, byte-coded action-queue verb) but not these three.

**Group 10 (ungrouped, 9 ids) — no superseding mechanism identified.**
Confirmed zero references by the same bare-name grep; root cause not
traced (would need per-feature investigation outside this batch's remit):
- `EditboxEnd` (strings.h:898)
- `ExamineOpened` (985)
- `ExamineRowStatusInvisible` (1055)
- `FmtBriefEffectsCount` (965)
- `FmtBriefFeatsCount` (966)
- `FmtCompassDegrees` (543)
- `FmtTransitionRoom` (477)
- `MessagesTitleDialogLog` (1134)
- `PazaakSelectCardFirst` (1292)

### A2 — stale "lang_fr aliases lang_en" comment contradicts the code (strings.h:1993)

What's there: strings.h's doc block (lines 1991-1995) says "lang_fr
currently aliases lang_en for the Id::* speech path (full FR translation
pass is deferred)".

What the code actually does: `strings_fr.cpp` never references `lang_en`
anywhere (grepped the whole file — zero hits on the token). Its `Get()` is
a complete, independent 651-case switch of French text — `"Porte"`,
`"PNJ"`, `"Conteneur"`, `"Objet"`, `"Lieu"`,
`"Aucune porte \xE0 port\xE9e"`, `"Choisissez d'abord une carte."`, and so
on. Comparing every id shared between EN and FR: only 39 of 651 have
identical text, and every one of those 39 is a short, generic string
(`"%s, %s"`, plain digits, etc.) that coincides by chance, not by aliasing.
For comparison, the same "identical text vs English" count for the other
four languages — which nobody claims alias English — is statistically the
same: DE 38 same / 614 different, ES 29/622, IT 28/623, RU 26/626.
French's overlap rate with English (39 same / 612 different) is
indistinguishable from German's (38), Spanish's (29), Italian's (28),
Russian's (26) — all five are independently, fully translated tables, not
"four translated plus one aliased."

Also telling: `strings_fr.cpp`'s own file header (lines 1-11) makes no
aliasing claim — it says exactly what `strings_it.cpp` and
`strings_es.cpp` say ("this table covers the Id::* speech path"), and
those two are never described as aliasing anything either.

This surfaced directly because this scan's own binding brief repeated the
strings.h claim as settled context ("lang_fr deliberately aliases
lang_en ... This is documented and is NOT a finding"). Flagging it
precisely *because* the documented premise does not match the code as it
stands today — worth the user's attention regardless of how the brief
characterised it going in.

Why it matters: a future maintainer (or another agent working from
strings.h alone) will believe French `Id::*` speech is unfinished/English
and either skip real work that's already done, or duplicate it.

Proposed change: correct strings.h's comment to state `lang_fr` is a
complete, independent table (matching its own file header) — **not** a
proposal to do translation work; translation already appears complete to
the same standard as DE/IT/ES/RU.

Risk: mechanical (comment-only edit). Estimated delta: ~3 lines changed.

### A3 — `SpectatorBattleDoomed` missing from FR/IT/ES (content gap, confirmed-safe fallback)

`strings.h:178` declares `Id::SpectatorBattleDoomed` (the Endar Spire
scene-soldier "the way is blocked" narration, single caller
`spectator_scene.cpp:60`). `strings_en.cpp:59`, `strings_de.cpp`, and
`strings_ru.cpp` all case it; `strings_fr.cpp`, `strings_it.cpp`, and
`strings_es.cpp` do not. It is the **only** enumerator missing from any
table — every other id has a case in all six switch tables (EN/DE/RU sit
at 652/652 case labels; FR/IT/ES at 651/652).

Runtime behavior, traced end-to-end: none of the six `Get()` switches has
a `default:` label. All six end with `case Id::Count_: return "";`
followed by a trailing `return "";` after the closing brace of the
switch. A switch with no matching case — this one included — simply falls
out of the switch body to that trailing `return ""`, identically in every
language. So on FR/IT/ES this specific narration line silently becomes an
empty string spoken by Prism: **not a crash, not the wrong phrase, not
undefined behaviour** — just silence at that one moment, on three of six
languages.

This is a content gap (missing translations), not a logic bug — flagging
under cleanup rather than "possible bugs" because the fallback path is
confirmed safe. Unlike the tutorial-hint FR/IT/ES gap (documented,
deliberate, out of scope per the brief), nothing found in the surrounding
code or comments marks this one as an intentional, known omission — worth
the user deciding whether it's an oversight or an accepted gap.

Risk: low (content-only; needs three short translations, no code change).
Estimated delta: +3 lines (one case per missing table).

## Section B — AI-pattern findings

None found. Reasoning: `strings.cpp` (43 lines) is a tight, non-redundant
dispatcher with no defensive over-checking. The six `Get(Id)` tables are
inherently repetitive by nature — they are literal lookup data, not logic —
so their line-by-line similarity is the correct shape for the problem, not
an AI-generated-code smell. `strings.h`'s per-id doc comments (sampled
across ~300 lines: 200-300, 800-900, plus targeted reads around every
dead-id finding above) consistently explain *why* — call sites, argument
order and meaning, phrasing rationale, cross-references to sibling ids,
even design history ("Korridor noun dropped 2026-05-22") — rather than
restating the enumerator name in prose. No `TODO`/`FIXME`/`XXX`/`HACK`
markers and no commented-out `case` blocks were found in any of the eight
files.

## Findings (possible bugs — user decides)

### F1 — gated/unavailable dialog replies are never announced as unavailable

Traced while investigating dead id `DialogReplyUnavailable` (A1, Group 2).
`strings.h:1120-1128` documents a designed cue: when a dialog reply is
gated (`active=0`), a `" (unavailable)"`-style suffix is meant to be
appended via an `enrichRow` callback. That callback was never wired for
the dialog-reply listbox spec (`enrichRow = nullptr` in
`menus_listbox.cpp`), and the mechanism that actually does announce dialog
reply rows today — `menus_monitors.cpp`'s `DialogReplyState`/monitor path
— reads each row with the generic `FmtContainerItemAt` format
(`menus_monitors.cpp:887`), which carries no gated/ungated distinction at
all.

Net effect: a blind player navigating dialog replies with a keyboard has
no way to hear that a given reply is currently greyed out (e.g. a
skill-check-gated or plot-gated line) before selecting it — sighted
players see the greyed-out state, screen-reader users don't get the
equivalent cue. This sits squarely in the mod's accessibility mission, so
flagging it here rather than folding it into the A1 dead-code list.

Not proposing a fix — this needs the user to decide whether to wire
`DialogReplyUnavailable`/`FmtDialogReplyUnavailableRow` into
`DialogReplyState`'s row-announce path, confirm gating is rare enough not
to matter, or address it a different way. Exact in-game action to
reproduce/verify: open a dialog with a skill-check-gated reply option
(several early Taris conversations have these) and arrow onto the greyed
row — confirm whether anything currently marks it as unavailable before
Enter is pressed.

## Format-specifier check (Fmt* ids) — clean, no mismatches

Extracted the ordered printf-specifier sequence (flags/width/precision +
conversion character, e.g. `%s`, `%02d`, `%.0f`) for all 194 `Fmt*` ids
that carry a `%` placeholder, across all six tables, and diffed pairwise.
**Zero mismatches** — every `Fmt*` id has the identical specifier count,
type, and order in every language that cases it. (The only id missing
from any table, `SpectatorBattleDoomed`, carries no format specifiers, so
this check has no interaction with A3.) This was the check the brief
called out as the real crash/garbage risk — clean result, nothing to
report.

## Whitespace anomaly check — clean, no anomalies

Scanned every id's text in every language for internal double-spaces:
**zero occurrences in any of the six tables.** Scanned leading/trailing
single spaces (used deliberately for phrase-concatenation patterns, e.g.
`FmtBriefCondition`'s `" %s."` or `FmtCharSheetClass`'s `"%s. "` — both
documented in nearby comments as opener/joiner fragments meant to be
concatenated by the caller) and diffed for cross-language inconsistency:
**zero mismatches** — every id that has a leading/trailing space has it in
every language that cases it, and every id without one is without one
everywhere. No repeat of the earlier English-only double-space bug
(`combat_strings.cpp`, already fixed, separate file) in this batch.

## Duplicate check — no duplicate case labels; no actionable duplicate enumerators

No id is cased twice within any single table (unique case-label count
equals total case-label count in all six files). One genuine
`case`-fallthrough exists, consistently in all six tables:
`TutTraskCamera` falls through to share `TutTraskFootlocker`'s text (both
describe the same camera-control tutorial line) — intentional, not a bug.

Checked for "two enumerators with identical text in all six tables" as
requested. Found several short/generic strings shared by 2-4 different
ids *within* each language (e.g. `"Door"` used by both `CategoryDoor` and
`MapCursorDoorNoun`; `"dead"` by both `TargetIsDead` and
`DamageLevel5Dead`; `"empty"` by four different UI contexts;
`"%s, %s"`/`"%s %s"` shared by several `Fmt*` ids as a generic two-part
row template). Traced each pair's call sites: every one is a genuinely
distinct concept (map-cursor hit-test result vs. a category prefix;
damage-classification word vs. an unrelated flag; a reusable two-field
row layout) that happens to render the same short word or shape, not a
redundant enum entry. The same pairs recur consistently across all six
languages (confirmed by identical per-language duplicate counts), which
is what you'd expect from coincidental short-word/format reuse, not from
a translation-specific bug. Not raising these as findings — no actionable
enum consolidation identified.

## Candidate 28 — narrow-header include opportunities

Nothing to report. `strings.h` includes nothing (self-contained enum +
declarations). All seven `.cpp` files include only `strings.h` itself —
none of them touch the `engine_offsets.h` aggregator family at all.

## Files scanned with nothing to report

- `strings.cpp` — clean; tight 43-line dispatcher, no findings of any kind.
