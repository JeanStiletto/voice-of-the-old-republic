# Phase 3 scan — menu chain + character generation

Scope (7 files, ~3300 lines combined):
- `menus_chain.cpp` (1102), `menus_chain.h` (211)
- `menus_chain_input.cpp` (784)
- `menus_chargen_attr.cpp` (423), `menus_chargen_attr.h` (125)
- `menus_chargen_feats.cpp` (483), `menus_chargen_feats.h` (54)
- `menus_chargen_skills.cpp` (276), `menus_chargen_skills.h` (105)
- `menus_chargen_layout.cpp` (59), `menus_chargen_layout.h` (33)
- `menus_skillflow_nav.cpp` (23), `menus_skillflow_nav.h` (27)

Method: full read of every file above (no sampling). Then targeted greps
per-header-symbol to prove every `#include` is actually referenced in the
including file (ran one grep per candidate-unused header, listed inline
below each finding — not just a file-name search, per the brief's trap #1).
Also grepped `engine_offsets_{fields,values,addresses}.h` to place each
used constant in its post-C8 sub-header, for the candidate-28 section.
One cross-batch check: `menus_powers_levelup.cpp` (469 lines, out of my
batch) turned up sharing `menus_skillflow_nav.h` with `menus_chargen_feats.cpp`
(483 lines) — flagged as a pointer for whoever's batch owns it, not analysed
in depth here.

## Section A — general low-level cleanup

### A1 — Broken/stale top-of-file comment (`menus_chain.cpp:1-5`)

What's there now:
```
// chain-navigation state and helpers.
//
// here and what stays in menus.cpp. Function bodies are unchanged from
// the original menus.cpp inline definitions; only the namespacing /
// linkage changes.
```
Line 3 starts mid-sentence ("here and what stays in menus.cpp") — this is
what's left after an editor deleted the "What lives here: ..." lead-in
during a later pass, leaving a grammatically broken fragment. Compare
`menus_chain.h:1-15`, which still carries the full two-paragraph version
("What lives here: ... What stays in menus.cpp: ..."). Neither file's
top comment mentions `menus_chain_input.cpp` at all, even though five of
the functions this very comment is describing now live in that third
file (moved there by Phase-1 candidate 2). This is exactly the "stale
comment from a prior split" the brief called out as a known trap.

Proposed change: rewrite `menus_chain.cpp:1-5` to state plainly what
this file holds (chain build/validate/state) vs. `menus_chain_input.cpp`
(input consumption) vs. `menus.cpp` (speech/focus). `menus_chain.h`'s
existing header comment can gain one line noting that the four
`Handle*`/`WalkChildren` declarations are implemented in
`menus_chain_input.cpp`, not this pair.

Risk: mechanical (comment-only). Estimated delta: ~6 lines changed, 0
added net.

### A2 — Eight includes in `menus_chain.cpp` are unused post-split (candidate-2 residue)

`menus_chain.cpp` still includes eight headers that only the functions
which moved to `menus_chain_input.cpp` needed. Verified by grepping the
qualified symbol each comment claims to need, not just the header name:

- `engine_input.h` — no `kInput*` constant is used in this file (the
  `using namespace acc::engine;` comment at line 42 lists `kInput*` but
  none actually appear; every `kInput*` use is in `menus_chain_input.cpp`).
- `engine_manager.h` — none of `FindOwningPanel` / `IsPanelInManager` /
  `GetForegroundPanel` / `LogManagerStack` appear; `IsPanelLive` (this
  file's own manager walk) reads `kAddrGuiManagerPtr` directly instead.
- `menus_journal.h` (line 30, comment claims `IsJournalEntry,
  SpeakDescription`) — neither is called in this file; both are only
  called from `menus_chain_input.cpp:162,270`.
- `menus_listbox.h` (line 31, comment claims
  `IsWorkbenchUpgradePickerArmed, ArmEquipPicker,
  ArmWorkbenchUpgradePicker`) — none called here; all three are only
  called from `menus_chain_input.cpp:305,320,675`.
- `menus_monitors.h` (line 33, comment claims `AnnounceControl`) — not
  called; `AnnounceControl` calls are all in `menus_chain_input.cpp`
  (lines 282, 485).
- `menus_pending.h` (line 35, comment lists `QueueActivate, IsPending,
  QueueMoveCursor, Queue{ClickAt,EquipSelect,WorkbenchSlotSelect,
  StoreItemActivate}`) — none of the `acc::menus::pending::*` calls the
  comment lists appear in this file.
- `minigame_pazaak.h` (line 37, comment claims "DispatchWagerInput
  codes") — no `acc::pazaak::` symbol is referenced.
- `peek_description.h` (line 38, comment claims
  `SpeakItemRowDescription`) — not called; the one call is
  `menus_chain_input.cpp:273`.

Every one of these headers' comments correctly describes what the code
*used to* call from this file before candidate 2 moved
`HandleEnterActivation` / `HandleNavStep` / `HandleLeftRight` /
`HandleEsc` / `WalkChildren` out — the includes (and their explanatory
comments) were simply never pruned from the file they were split out of.

Proposed change: delete these 8 `#include` lines from `menus_chain.cpp`.
Risk: mechanical (compiler-checked — a stray remaining use would fail
to build). Estimated delta: -8 lines.

### A3 — Seven includes in `menus_chain_input.cpp` are unused (same residue, other direction)

Mirror of A2: the file that gained the five handlers also gained
headers it doesn't need, because the original include block was copied
wholesale rather than trimmed to the new split. Verified the same way
(grepped the qualified symbol, not the header name):

- `engine_manager.h` — same four symbols checked, none present.
- `engine_player.h` (comment: "`PartyTableIsNPCAvailable` / `*Selectable`
  / `kPartyRosterSlotCount`") — that logic is the `PartySelection`
  portrait-decorative filter, which stayed in `RebindChain` in
  `menus_chain.cpp`; neither symbol appears in `menus_chain_input.cpp`.
- `menus_pazaakdeck.h` — `IsChainDecorative` is only called from
  `menus_chain.cpp:484`; not referenced here.
- `menus_credits.h` — `ForEachCreditsRowAnchor` / `ExtractCreditsRow`
  are only called from `menus_chain.cpp`'s virtual-row registration
  block; not referenced here.
- `menus_equipstats.h` — same shape, `ForEachEquipStatRowAnchor` /
  `ExtractEquipStatRow` only called from `menus_chain.cpp`.
- `menus_charsheet.h` — same shape, `ForEachStatRowAnchor` /
  `ExtractStatRow` only called from `menus_chain.cpp`.
- `<cstring>` — no `strcmp`/`strncmp`/`memcpy`/`memset`/`strcpy`/`strlen`
  call anywhere in this file (all the text compares that need `cstring`
  — `FindCloseButton`/`FindCancelButton` — stayed in `menus_chain.cpp`).

Proposed change: delete these 7 lines from `menus_chain_input.cpp`.
Risk: mechanical. Estimated delta: -7 lines.

(A2 + A3 together are 15 dead include lines across the two files — a
direct, quantifiable instance of the "now-unneeded includes" residue
the brief asked this batch to check for.)

### A4 — Hardcoded English fallback strings spoken to the user (`menus_chargen_feats.cpp:307-309, 326`)

Two fallback paths in `AnnounceFocused` bypass `strings.h`'s `Get(Id)`
and speak a literal instead:

```cpp
// line 307-309 — button focus, when ReadButtonText fails/returns empty
if (!got) {
    snprintf(btnText, sizeof(btnText), "%s",
             br.logTag ? br.logTag : "?");   // "BTN_RECOMMENDED" / "BTN_ACCEPT" / "BTN_BACK"
}
prism::Speak(btnText, /*interrupt=*/false);   // line 311
```
```cpp
// line 326 — chart-cell focus, when ReadNameLabel fails/returns empty
snprintf(name, sizeof(name), "Talent %u", (unsigned)featId);
```
```cpp
char head[256];
snprintf(head, sizeof(head),
         acc::strings::Get(acc::strings::Id::FmtChargenFeatChartCell),
         name, sw);
prism::Speak(head, /*interrupt=*/false);   // line 336 — speaks "name" as-is
```

Both are genuine fallback-only paths (the engine normally does populate
the button/label text, so this rarely fires), but when it does fire the
user hears a raw English developer string — `"BTN_BACK"` or `"Talent
14"` — regardless of the mod's configured language. This is the
centralisation rule the brief calls out as a real finding when violated.

Proposed change: add two `strings::Id` entries (e.g.
`ChargenFeatUnnamedButtonFallback`, `FmtChargenFeatUnnamedFeat`) with
localised text for all six languages, route both `snprintf` calls
through `Get(Id)` instead of the literal/`logTag`.

Risk: low (fallback-only path; needs the description-listbox-empty or
label-read-failure condition to trigger, which is hard to force
in-game — testing this specifically would mean finding a feat/button
whose text the engine hasn't populated yet, which the comments suggest
doesn't normally happen). Estimated delta: +2 string IDs × 6 locale
files, ~4 lines changed in `menus_chargen_feats.cpp`.

### A5 — `FindCloseButton` / `FindCancelButton` duplicate their scan loop (`menus_chain.cpp:347-388`)

```cpp
void* FindCloseButton(void* panel) {              // lines 347-367, 21 lines
    ...
    auto* list = reinterpret_cast<CExoArrayList*>(...);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!IsChainNavigable(c)) continue;
        char text[256];
        if (!acc::menus::extract::FromControl(c, text, sizeof(text), panel)) continue;
        if (strncmp(text, "Schliess", 8) == 0 || strncmp(text, "Close", 5) == 0 ||
            strncmp(text, "OK", 2) == 0 || strncmp(text, "Weiter", 6) == 0 ||
            strncmp(text, "Continue", 8) == 0) {
            return c;
        }
    }
    return nullptr;
}

void* FindCancelButton(void* panel) {             // lines 369-388, 20 lines — identical shape
    ... same walk, same IsChainNavigable/FromControl gate ...
    if (strncmp(text, "Abbrechen", 9) == 0 || strncmp(text, "Cancel", 6) == 0 ||
        strncmp(text, "Nein", 4) == 0 || strncmp(text, "No", 2) == 0) {
        return c;
    }
}
```

The two functions are the same 15-line scan (walk `panel.controls`, cap
at 256, skip non-navigable, extract text, prefix-match) with only the
prefix table swapped. (Note: these prefixes are engine-rendered button
captions being pattern-matched for internal routing, not text the mod
speaks — this is not a strings.h/centralisation violation, just
duplicated matching logic.) No existing helper in the codebase does
this scan generically (grepped for `FindButton`/`PrefixMatch`/
`MatchPrefix` codebase-wide — zero hits outside these two functions).

Proposed shared helper:
```cpp
struct PrefixMatch { const char* prefix; size_t len; };
void* FindButtonMatchingAnyPrefix(void* panel, const PrefixMatch* prefixes, int count);
```
`FindCloseButton`/`FindCancelButton` become ~6-line wrappers each that
pass their own `constexpr` prefix table.

Risk: mechanical (pure refactor of identical logic; no behavior
change). Estimated delta: -41 lines (two functions) → ~30 (shared) + 12
(two wrappers) ≈ net -0 to -10 lines, but collapses two copies of the
scan/cap/gate logic into one.

## Section B — AI-pattern findings

### B1 — `RebindChain` is a ~680-line function doing at least eight separable jobs (`menus_chain.cpp:419-1100`)

The brief explicitly puts function-level decomposition in scope for
Phase 3 (the `ClassifyCluster`/`BuildForArea` precedent from
`room_topology.cpp`). `RebindChain` is the same shape, larger: one
function that (1) walks `panel.controls` filtering decorative controls,
(2) recurses into listbox children with per-panel special cases, (3)
registers up to five kinds of "virtual" text-only chain rows, (4)
insertion-sorts the whole chain by y, (5) squashes cycle-arrow
flankers, (6) computes the equip-slot click-offset, (7) computes the
class-icon click-offset, (8) anchors the cursor on the engine's active
control and syncs two chargen sub-modules, (9) dumps the whole result
to the log. Each phase is already visually banner-commented and mostly
self-contained (reads/writes only `g_chain`/`g_chainCount` plus its own
locals) — this is a much more separable case than the `room_topology`
one that got reverted, because none of these phases share private
state with each other beyond the chain array itself.

Two sub-findings worth calling out on their own:

**B1a — `isDecorative` lambda is 157 lines by itself
(`menus_chain.cpp:478-634`).** It's a `[&]`-capturing lambda defined
inline inside `RebindChain`, with one `if (pk == PanelKind::X)` branch
per panel kind (Pazaak deck, Pazaak wager, InGameCharacter,
InGameEquip ×2, InGameLevelUp, PartySelection, WorkbenchUpgrade, plus
the universal close-button-caption filter). Each branch is already a
well-isolated, well-commented unit; nothing here reads chain state, so
it doesn't need to be a capturing lambda — it captures `closeCaption`/
`haveCloseCaption` (computed just above it) and nothing else. Could
become a standalone function `IsControlDecorative(void* panel, void* c,
const char* closeCaption, bool haveCloseCaption)`.

**B1b — Four virtual-row registration blocks are near-identical
(`menus_chain.cpp:696-722`, `812-841`, `874-894`, `903-927`, ~110 lines
total).** Credits row, InGameCharacter stat rows, Pazaak wager row, and
Equip stat rows each follow the exact same shape:
```cpp
auto onXAnchor = [](void* labelControl, int sortCy, void* userData) -> bool {
    void* p = userData;
    if (g_chainCount >= kMaxChainEntries) return false;
    int cx, cy;
    if (!GetControlCenter(labelControl, cx, cy)) cx = 0;
    char probe[8];
    if (!ExtractX(p, labelControl, probe, sizeof(probe))) return true;  // not ready yet
    g_chain[g_chainCount++] = { labelControl, cx, sortCy, /*textOnly=*/true };
    return true;
};
ForEachXRowAnchor(panel, onXAnchor, panel);
```
Only the extractor function differs (`ExtractCreditsRow` /
`ExtractStatRow` / `extract::FromControl` / `ExtractEquipStatRow`) —
and one of the four (the wager row, line 884) even has its probe
arguments in a different order (`FromControl(labelControl, probe,
sizeof(probe), p)` vs. the other three's `Extract*(p, labelControl,
probe, sizeof(probe))`), which is itself a small internal
inconsistency worth normalising if this gets touched. A shared
`AppendVirtualTextRow(void* labelControl, int sortCx, int sortCy,
ProbeFn probe, void* panel)` helper (captureless-lambda-as-function-
pointer works here — none of the four lambdas capture anything) would
cut this to ~12 shared lines + 4 one-line call sites.

This is a genuine "an abstraction should own this" case per Section B,
not a maybe: the four blocks are structurally identical modulo one
function pointer, and B1a is a self-contained 157-line unit with a
single input contract. I'm not proposing the full nine-way split in one
step — flagging it as a decomposition candidate for the user to size,
same as the `room_topology` precedent, rather than pre-committing to an
exact cut.

Risk: **needs-in-game-test** if executed — this is the single most
heavily-exercised function in the menu mod (fires on every panel open
and every listbox repopulate across every screen in the game). Any
split must be verified to change zero behavior (same reads, same
writes, same order), and the smoke test would need to cover the
existing checklist item 1 ("arrow through inventory, a dialog, chargen,
and one listbox screen") plus specifically the Pazaak deck/wager
screens and PartySelection (the two panel kinds with the most
intricate `isDecorative` logic). Estimated delta: not sized without a
concrete split plan — flagging for user decision, not proposing exact
line counts.

### B2 — `menus_chargen_attr.cpp` / `menus_chargen_skills.cpp` share ~300 lines of near-verbatim shell around genuinely different domain logic

These two files are already a partial success story: an earlier pass
(see `menus_chargen_layout.{h,cpp}`, whose own header comment says "The
three helpers below were near-byte-identical in both .cpp files;
consolidating them keeps the public per-panel namespaces intact")
already extracted `IsPanelOfVtable`, `IndexFromButton`, and
`RowPitchFromButtonExtents`. That consolidation didn't go far enough —
four more function pairs are still near-verbatim copies, quantified
below. I checked each pair line-by-line before counting it; two other
pairs in the same files have real behavioral differences and are
**not** included (see the "not recommended" list after).

**Copy-paste, recommend consolidating:**

1. `SyncSelectedAbilityFromChainFocus` (`menus_chargen_attr.cpp:37-66`,
   30 lines) vs. `SyncSelectedSkillFromChainFocus`
   (`menus_chargen_skills.cpp:35-64`, 30 lines). Identical: read chain
   focus, resolve the panel-relative index via the already-shared
   `IndexFromButton`, SEH-write the "selected" int field, log on
   change. Differs only in: the selected-field offset constant
   (`kAbilitiesCharGenSelectedAbilityOffset` vs.
   `kSkillsCharGenSelectedSkillOffset`), the log tag string
   (`"Menus.ChargenAttr"` vs. `"Menus.ChargenSkill"`), and the field
   name in the log message (`"selected_ability"` vs.
   `"selected_skill"`).

2. `CaptureLabelsIfApplicable` (`menus_chargen_attr.cpp:74-110`, 37
   lines) vs. the same-named function in
   `menus_chargen_skills.cpp:66-102` (37 lines). Identical loop:
   iterate `0..N`, compute label/button control addresses from two
   array-offset constants + a per-element stride, read the label text
   via the same two-path fallback (`ReadGuiString` then
   `ExtractTextOrStrRefIndirect`), capture into the cycle-category
   cache, log. Differs only in the loop bound constant (6 vs. 8), the
   four offset constants, and the log tag.

3. `AnnounceChainStepDescription` (`menus_chargen_attr.cpp:187-263`, 77
   lines) vs. the same-named function in
   `menus_chargen_skills.cpp:160-237` (78 lines) — the largest of the
   four. Identical shape: SEH-call the panel's `OnEnterPointsButton`
   via a raw `PFN_OnEnter` cast at a hardcoded address, read
   `description_listbox.controls[0]`, extract its text via the same
   two-path fallback used in #2, flatten embedded newlines into a
   single-line log dump with the identical char-by-char loop, speak,
   log. Differs only in: the index-lookup function name
   (`AbilityIndexFromButton` vs. `SkillIndexFromButton`), the
   `OnEnterPointsButton` address constant, the description-listbox
   offset constant, and the log tag. Notably, the four `kLabel*Offset`
   constants used in the text-extraction fallback
   (`kLabelGuiStringPtrOffset`, `kLabelTextOffset`, `kLabelStrRefOffset`,
   `kLabelTextObjectOffset`) are already the *same* constants in both
   files (generic label-control offsets, not per-panel) — only the
   description-listbox-owner offset and the button-index lookup vary.

4. `IsChargenAttributesDescriptionListbox`
   (`menus_chargen_attr.cpp:269-276`, 8 lines) vs.
   `IsChargenSkillsDescriptionListbox`
   (`menus_chargen_skills.cpp:239-246`, 8 lines). Trivial but
   identical: check the chain's current panel is this panel kind, then
   pointer-compare against `panel + offset`.

Combined, these four pairs are ~305 of the ~700 lines across the two
`.cpp` files (roughly 44%). A natural home is `menus_chargen_layout.{h,cpp}`,
extending the existing pattern with four more parameterised functions
(vtable constant, array/field offsets, count, description-listbox
offset, `OnEnterPointsButton` address, log tag as parameters) that each
panel calls through a ~5-8 line wrapper — the same shape the three
already-shared helpers use. Rough sizing: today's ~305 duplicated
lines would become ~150-180 lines of shared implementation plus ~60
lines of thin wrappers (8 wrappers × ~7 lines) ≈ 210-240 lines total, a
net reduction of roughly 65-95 lines — but the real value is collapsing
four duplicate-bug-surfaces into one each (a fix to the newline-
flattening loop, for instance, currently has to be applied twice).

**Checked and NOT recommended for merging — real domain variation:**

- `AnnounceChainStepSuffix` (attr: 33 lines, computes a D&D ability
  modifier via `ComputeAbilityModifier`/floor-division plus a
  point-buy cost via the engine's `GetAbilityPointCost`; skills: 24
  lines, computes only a class/cross-class cost via
  `IsClassSkill`). The message format differs by one field
  (`FmtChargenAttrInfoSuffix` takes mod+cost, `FmtChargenSkillInfoSuffix`
  takes cost only) and the attr side does real arithmetic the skills
  side has no equivalent of. Forcing a shared function here would mean
  passing in an optional "extra field" callback for a two-line saving —
  not worth it.
- `AnnounceValueChange` (attr: 85 lines including a whole
  `ChangeTracker` struct that detects modifier/cost breakpoint
  crossings across four message-format variants; skills: 27 lines, a
  single bare "value, remaining" announce with no breakpoint tracking
  at all). This is the clearest case in the batch of "the variation
  makes a shared helper worse than the repetition" — the attr version
  is solving a problem (mid-range modifier/cost breakpoints) that
  structurally doesn't exist on the skills panel (skills have no
  modifier, and cost is constant across the whole 0-18 range for a
  given skill). Any shared version would need the skills panel to
  carry dead breakpoint-tracking state for a breakpoint that never
  exists.
- `IsChargenXPanel` / `XIndexFromButton` (attr: 4+7 lines; skills:
  4+7 lines) — already thin thunks over the shared
  `chargen_layout::IsPanelOfVtable`/`IndexFromButton`; there's nothing
  left to extract.

Risk if executed: **needs-in-game-test**. This is chargen Attributes
+ Skills specifically — smoke test would be creating a new character
through both panels, confirming the per-row "Stärke, 8" / "Computer, 0"
announces, the suffix ("Modifikator -1, Preis 1" / "Preis 1"), the
Left/Right value-change announce, and the long description text on
Up/Down, all still fire identically to today. Estimated delta: as
sized above, ~-65 to -95 net lines plus one new set of parameterised
helpers in `menus_chargen_layout.{h,cpp}`.

### Cross-batch note (not analysed, flagging only)

`menus_powers_levelup.cpp` (469 lines) is outside this batch but shares
`menus_skillflow_nav.h`/`.cpp` with `menus_chargen_feats.cpp` (483
lines) and is suspiciously close in size. If the same reviewer or a
later pass covers that file, it's worth checking whether its
`EnsureBound`/`NavVertical`/`NavHorizontal`/`AnnounceFocused` cluster
duplicates `menus_chargen_feats.cpp`'s (this batch's B-analysis budget
went to the chain + attr/skills files instead, per the batch
assignment — not pursued further here).

## Findings (possible bugs — user decides)

None. Specifically checked the submenu Up/Down clamp-vs-wrap
convention across every navigation site in this batch:
- `menus_chain_input.cpp:461-470` (`HandleNavStep`'s chain step) —
  clamps at 0 / `g_chainCount-1`. Correct.
- `menus_chargen_feats.cpp:349-371` (`NavVertical`) — clamps at 0 /
  `total-1`. Correct.
- `menus_chargen_feats.cpp:373-397` (`NavHorizontal`) — on hitting a
  column bound, resets to the pre-move column (`c = oldC`) rather than
  wrapping. Correct.
No wrap-where-clamp-expected pattern found in this batch.

## Candidate 28 — narrow-header include opportunities

- `menus_skillflow_nav.cpp` — only symbol used from `engine_offsets.h`
  is `kSkillFlowColumnsPerRow`, which lives in
  `engine_offsets_fields.h`. Could include that narrower header
  directly instead of the full aggregator.
- `menus_chargen_layout.cpp` — only symbols used are
  `kCSWGuiButtonSize` and `kControlExtentOffset`, both in
  `engine_offsets_fields.h`. Same opportunity.
- `menus_chargen_attr.cpp`, `menus_chargen_skills.cpp`,
  `menus_chargen_feats.cpp`, `menus_chain.cpp`, `menus_chain_input.cpp`
  — each uses a genuine mix of vtable/function addresses, field
  offsets, count values, and shared types (`CExoArrayList`) from
  `engine_offsets.h`. No narrowing opportunity for these five; they'd
  still need 3-4 of the post-C8 sub-headers.
- None of the seven files in this batch include `engine_player.h` /
  `engine_area.h` / `engine_panels.h` / `engine_reads.h` in a way that
  could narrow further — unlike `engine_offsets.h`, those three headers
  were never split into includable narrower pieces (checked: no
  `engine_reads_*.h`/`engine_panels_*.h`/`engine_player_*.h` *header*
  exists, only additional `.cpp` implementation files behind the one
  shared header), so there is nothing narrower to migrate to yet.

## Files scanned with nothing to report

- `menus_chargen_attr.h`, `menus_chargen_skills.h`,
  `menus_chargen_feats.h`, `menus_chargen_layout.h`,
  `menus_skillflow_nav.h` — all clean: accurate doc comments, no dead
  declarations, no include residue (none of them include anything).
- `menus_chargen_layout.cpp` — clean; this is the file that already
  demonstrates the consolidation pattern B2 proposes extending.
- `menus_skillflow_nav.cpp` — clean, minimal, already shared by two
  consumers as designed.
- `menus_chargen_feats.cpp` — clean structurally (the B2-style
  duplication doesn't apply here, it has no sibling in this batch);
  only the two hardcoded-fallback-string instances (A4) and the
  cross-batch note above.
