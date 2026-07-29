# Phase 3 scan — listbox and extraction batch

Scope:
- `menus_extract.cpp` (1897 lines) / `menus_extract.h` (67 lines)
- `menus_listbox.cpp` (1693 lines) / `menus_listbox.h` (109 lines)
- `menus_listbox_picker.cpp` (306 lines)

Method: full read of all five files top to bottom (two paginated reads for
the two >1100-line files). Cross-checks: `git log`/`git show` on the
candidate-23 split commit (`41400bf`) to diff the picker file against its
pre-split form line-for-line, rather than trusting the commit message alone.
Targeted greps: mojibake sweep (`â€`, `Ã¤` etc.) across the batch AND the
whole `patches/Accessibility` tree for comparison; call-site greps for every
function/constant I considered flagging as dead or duplicated
(`ForEachWagerRowAnchor`, `ResetCycleCategoryCache`, `CaptureCycleCategory`,
`GetTitleOverride`, `TryHandleInput`, `TickListboxMonitors`,
`kWorkbenchUpgradeLbItemsId`, `kWorkbenchUpgradeBtnBackId`, `kEquipBtnBackId`,
`PFN_MoveMouseToPosition`/`kAddrMoveMouseToPosition`) — all confirmed live
with real callers, none was a filename-vs-namespace trap.

## Section A — general low-level cleanup

### A1 — Mojibake corruption, isolated to menus_listbox.cpp (48 occurrences)

`menus_listbox.cpp` has 48 instances of the UTF-8-interpreted-as-Latin-1
double-encoding artifact `â€"` / `â€"` / `â€™` / `â‰¤` / `â†’` standing in for
em-dashes, "≤", "→", and a few `Ã¼`/`Ã¤` standing in for ü/ä — all inside
comments, plus two inside `acclog::Write` log-string literals (lines 424,
1072: `"Enter â€" op already pending; ignoring"`). Confirmed via
`Grep -n "â€" menus_listbox.cpp` (48 hits) and a repo-wide
`Grep "â€" patches/Accessibility` (same 48 hits, same one file) — the other
four files in this batch, and the rest of the tree, are clean. So this is
not a systemic encoding problem, just this one file having been saved once
through a lossy round-trip.

Representative lines: 5, 52, 70, 104, 191, 240, 281, 372, 421, 424, 518,
553, 651, 709, 806, 909, 973, 1072, 1184, 1381, 1406, 1507 (full line list
available by re-running the grep — 48 total, all in comments except the two
log strings above).

- What: double-encoded punctuation, comment-only except two English log
  strings.
- Why a problem: reads as garbage in any editor/terminal that isn't doing
  the same double round-trip; the two log-string instances would print
  garbage in `logs/patch-*.log` (English diagnostic text, not
  user-facing/spoken, but still meant to be human-read).
- Proposed change: mechanical find-replace of the ~6 broken sequences back
  to their intended characters (em dash, →, ≤, ü, ä). No code semantics
  touched — comments and two log format strings only.
- Risk: mechanical. Not compiler-checked (it's text in comments/strings),
  but trivially diffable — every replacement is `broken-sequence` →
  `intended-character` with no ambiguity, and a clean `kdev build` proves
  nothing else moved.
- Estimated line delta: 0 (in-place character fixes only).

### A2 — Owner-panel resolution duplicated verbatim (menus_extract.cpp:450-461 and :1097-1105)

Two blocks compute "the owning panel of `control`, with fallback and a
stale-pointer filter" using identical logic and even a comment that
self-admits the duplication:

Block 1 (inside `FromControl`'s section 0, local to that `{ }` scope):
```cpp
void* owner = ownerPanel;
if (!owner) owner = FindOwningPanel(control);
if (!owner) owner = g_currentPanel;
// Filter stale/wild owner pointers ...
if (owner && !acc::engine::IsPanelInManager(owner)) owner = nullptr;
```

Block 2 (before section 9a, its own local `ownerForPerkind`):
```cpp
void* ownerForPerkind = ownerPanel;
if (!ownerForPerkind) ownerForPerkind = FindOwningPanel(control);
if (!ownerForPerkind) ownerForPerkind = g_currentPanel;
// Same stale-owner filter as section 0 above — drops freed
// g_currentPanel before any of the per-kind detectors...
if (ownerForPerkind && !acc::engine::IsPanelInManager(ownerForPerkind)) {
    ownerForPerkind = nullptr;
}
```

- What: the exact same 4-line resolve-with-fallback-and-filter recomputed
  under a different local name, because `owner` from block 1 goes out of
  scope at the end of section 0's `{ }` (line 598) before block 2 needs it
  again at line 1097.
- Why a problem: same logic maintained in two places; the second copy's own
  comment ("Same stale-owner filter as section 0 above") shows the author
  knew it was a repeat.
- Proposed change: hoist a single `void* owner = ResolveOwnerPanel(ownerPanel, control);`
  at the top of `FromControl` (function scope, computed once), used by both
  call sites, dropping the second private helper/local entirely. Both
  computations take the same inputs (`ownerPanel`, `control` — both function
  parameters, unchanged across the function body) and nothing between them
  mutates `g_currentPanel`, so a single up-front computation is behavior
  preserving.
- Risk: low. Requires confirming none of the section-0 sub-branches
  (`ExtractStatRow`, `ExtractCreditsRow`, `ExtractEquipStatRow`,
  `ExtractCardLabel`, the Pazaak-wager branch) has a side effect that
  changes `g_currentPanel` before section 9a reads it — none of the four
  calls take a `g_currentPanel`-mutating path from what's visible in this
  file, but they're calls into other TUs (`menus_charsheet.cpp` etc.), so a
  full guarantee needs a quick grep of those four functions before
  executing. Purely a same-file, same-scope hoist — no header change.
- Estimated line delta: about −15 lines.

### A3 — Three byte-identical listbox announce callbacks (menus_listbox.cpp)

`InGameMessagesAnnounce` (771-782), `ExamineAnnounce` (1203-1214), and
`WorkbenchItemsAnnounce` (930-941) are the same body (extract row text via
`FromControl`, format with `FmtContainerItemAt`, `prism::Speak`), differing
only in buffer sizes (512/640 vs 256/320) and whether they early-out on
`r.rowCount <= 0` (Examine and WorkbenchItems do; InGameMessages doesn't):

```cpp
void <Name>Announce(void* /*lb*/, const ListBoxNavResult& r) {
    if (!r.row) return;                    // + `|| r.rowCount <= 0` on two of three
    char rowText[N];
    if (!acc::menus::extract::FromControl(r.row, rowText, sizeof(rowText))) {
        return;
    }
    char msg[N2];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, r.newSel + 1, r.rowCount);
    prism::Speak(msg, /*interrupt=*/false);
}
```

- Why a problem: same ~10-line body copy-pasted three times in one file.
- Proposed change: a shared static helper, e.g.
  `void SpeakRowAtDefaultPosition(const ListBoxNavResult& r, size_t bufCap)`
  taking the row-text buffer capacity (message buffer can just be
  `bufCap + 64` or a second parameter) and doing the extract+format+speak;
  the three specs' `announce` fields point at thin wrappers or the shared
  function directly if the signature matches `void(*)(void*, const
  ListBoxNavResult&)`. Also fold in the `r.rowCount <= 0` guard uniformly
  (all three rows only exist when `rowCount > 0` in practice — this is a
  no-op behavior unification, not a functional change, but flag it
  explicitly since it does touch InGameMessages' guard).
- Risk: mechanical for the extract/format/speak body; low (not mechanical)
  for unifying the `rowCount <= 0` guard — call that out separately if the
  user wants strict behavior preservation over consolidation.
- Estimated line delta: about −20 lines.

### A4 — Four near-identical dialog-listbox specs (menus_listbox.cpp:841-904)

`kDialogCinematicSpec`, `kDialogCinematicCopySpec`, `kDialogComputerSpec`,
`kDialogComputerCameraSpec` are four `ListBoxPanelSpec` literals, each ~16
lines, identical in every field except `logTag` and the one-line `matches`
function (`IdentifyPanel(p) == PanelKind::DialogCinematic` vs
`::DialogCinematicCopy` vs `::DialogComputer` vs `::DialogComputerCamera`).
All four share `findListBox = DialogFindRepliesLb`, `announce = nullptr`
(deliberately — speech is owned by a separate poll monitor, per the comment
at 830-839), and identical `onEnter`/`onEsc`/title/empty-state/fallthrough
fields.

- Proposed change: one spec with a combined matcher
  (`bool DialogRepliesMatches(void* p) { auto k = IdentifyPanel(p); return
  k == PanelKind::DialogCinematic || k == PanelKind::DialogCinematicCopy ||
  k == PanelKind::DialogComputer || k == PanelKind::DialogComputerCamera; }`),
  replacing 4 structs (~64 lines) and 4 near-duplicate matcher functions
  with 1 of each.
- Caveat (not purely mechanical): the four specs currently log under four
  distinct tags in the per-move nav line (`acclog::Write(spec.logTag, ...)`
  in `DispatchKeyDownEdge`). Merging collapses that to one tag (e.g.
  `"DialogReplies"`), losing the ability to tell from the log alone which
  of the four dialog-panel variants was active during a given nav step.
  User-audible behavior is unchanged (no `announce`/`onEnter` differ), but
  this is a real diagnostic-fidelity trade-off, not a no-op.
- Risk: low, with the log-tag caveat above named explicitly so it isn't
  approved as if it were mechanical.
- Estimated line delta: about −50 lines.

### A5 — Vestigial single-iteration loop (menus_extract.cpp:770-812, section 7)

```cpp
if (lb && lb->data && lb->size == 1) {
    int n = 1;
    ...
    for (int i = 0; i < n; ++i) {
        void* row = lb->data[i];
        ...
    }
```
The branch is already gated on `lb->size == 1`, and `n` is hardcoded to `1`
(not `lb->size`), so the loop always runs exactly once over `lb->data[0]`.

- Why a problem: dead generality — reads as if it could process multiple
  rows but structurally can't (both the gate and the hardcoded `n=1` fix it
  at one iteration).
- Proposed change: drop the loop, operate directly on `lb->data[0]`.
- Risk: mechanical (no behavior change — the loop body already runs exactly
  once today).
- Estimated line delta: about −4 lines.

### A6 — Truncated header-comment sentences (menus_extract.h:1-6, menus_extract.cpp:1-8)

Both files open with a comment whose first sentence is missing its subject,
e.g. `menus_extract.h`:
```
// control text extraction (the announce ladder).
//
// of menus.cpp into its own TU as `acc::menus::extract::FromControl`.
```
and `menus_extract.cpp`:
```
// control text extraction (the announce ladder).
//
// FromControl) and its three extract-only helpers (FindSiblingLabel,
// IsCycleFlankerArrow, LookupCycleCategory) lift out of menus.cpp.
```
Both read as if a clause like "This function was pulled out" / "This TU
(originally the announce-text branch of menus.cpp's `AnnounceControl`,
here" was dropped from the front of the sentence during an earlier edit.
- Why a problem: stale/broken prose right at the top of both files — the
  first thing a reader (human or LLM) sees.
- Proposed change: reword the opening sentence in both files so it reads as
  a complete sentence (e.g. "`FromControl` and its three extract-only
  helpers ... were lifted out of menus.cpp into their own TU."). Comment-only.
- Risk: mechanical (comment text only).
- Estimated line delta: 0.

### A7 — Live temporary diagnostic block (menus_listbox_picker.cpp:249-283)

`MonitorWorkbenchUpgradePicker` carries a block explicitly marked
"TEMPORARY DIAGNOSTIC (lightsabercrystalcrash investigation)" with a closing
instruction "Remove once the mechanism is identified." This is exactly the
"leftover debug/diagnostic path" category Section A asks about — but per
`STATE.md`'s verification-gap note, the crystal-picker path (this monitor's
whole reason to exist) **has never executed in-game since the split**, so
the mechanism this trace exists to identify is still open.
- What: a per-frame `acclog::Trace` of LB_ITEMS selection/top/items-per-page
  state while the workbench upgrade picker is armed.
- Recommendation: **keep for now** — do not remove as part of this sweep.
  Revisit after the crystal-picker in-game test (item 3 on the STATE.md
  verification-gap checklist: "Workbench upgrade — arm a crystal slot,
  arrow, commit") either confirms the mechanism (then remove) or is still
  needed (then it stays until it is).
- Risk: n/a — no change proposed here, just flagging its presence and
  status per the brief's checklist.

## Section B — AI-pattern findings

### B1 — `FromControl` is a ~1470-line function (menus_extract.cpp:410-1881)

This is the file-level oversized-function case Phase 1 named as comparable
to `room_topology.cpp`'s `ClassifyCluster`/`BuildForArea`. `FromControl`
runs a strictly-ordered ladder of ~20 numbered sections (the code's own
comments number them: `-1`, `0`, `1`..`9`, `6b`, `7b`, `9a`, `9b`, `9b2`,
`9b3`, `9c`, `9d`), each of the shape "if `source` isn't set yet, try this
extraction path; on success set `source` and (usually) fall through to the
next section only if it failed." Concretely:

- `-1` (417-431): mod-settings root-anchor short-circuit
- `0` (433-598): 7 per-kind virtual-row formatters (charsheet stat row,
  credits row, equip-stat row, Pazaak deck widget, Pazaak wager widget,
  journal quest-items button, keybinding row) sharing one owner resolution
- `1`-`5` (600-676): tooltip, button, buttontoggle, label, labelhilight
- `6`/`6b` (678-745): slider, editbox
- `7` (747-813): single-row listbox
- `7b` (815-922): PartySelection portrait resolver — **note:** this section
  has an actual early `return nullptr` out of the whole function (line 920)
  when its vtable matches but no name resolves, not just a "skip this
  section" — see the callout below.
- `8` (924-1067): speculative read against a small allowlist of known
  vtable overrides
- `9a`-`9d` (1069-1758): six more per-kind fallbacks (InGameMenu icons,
  InGameEquip slots, InGameMap arrows, WorkbenchUpgrade slots, chargen
  class-selection icons, chargen portrait cycle)
- `9` (1760-1793): generic sibling-label fallback
- tail (1801-1878): three post-processing passes over whatever `source`
  found — cycle-category prefix, toggle-state suffix, disabled suffix

Every one of these sections already reads as a self-contained unit: it
takes `control` (and sometimes the resolved owner panel), writes into
`outBuf`/`bufSize`, and either sets `source` to a tag string or leaves it
null. That is exactly the shape of a function that decomposes cleanly.

**Why this split is materially safer than the room_topology/transitions
attempts that were reverted:** those were cross-*file* splits, where
function-name analysis missed anonymous-namespace *state* shared across the
proposed file boundary (candidates 13, 24). This split proposes staying
**inside the same translation unit and the same anonymous namespace** —
`FromControl`'s existing sibling helpers (`FindSiblingLabel`,
`IsCycleFlankerArrow`, `LookupCycleCategory`, `FindWagerMaxLabel`,
`ExtractWagerRow`, `kPortraitByRow`) are already file-static and already
callable from anywhere in this file. No header would need to publish
anything; no new cross-TU coupling is created. The risk category this
avoids entirely is the one that burned candidates 13/24.

Proposed shape — `FromControl` becomes an orchestrator of ~15-160-line
static helpers, called in the same order, e.g.:

```cpp
namespace {
const char* TryRootAnchorShortCircuit(void* control, char* outBuf, size_t bufSize);          // -1
const char* TryPerKindVirtualRow(void* control, void* owner, char* outBuf, size_t bufSize);   // 0 (7 sub-checks)
const char* TryTooltip(void* control, char* outBuf, size_t bufSize);                          // 1
const char* TryButton(void* control, char* outBuf, size_t bufSize);                           // 2
const char* TryButtonToggle(void* control, char* outBuf, size_t bufSize);                     // 3
const char* TryLabel(void* control, char* outBuf, size_t bufSize);                            // 4
const char* TryLabelHilight(void* control, char* outBuf, size_t bufSize);                     // 5
const char* TrySlider(void* control, char* outBuf, size_t bufSize);                           // 6
const char* TryEditbox(void* control, char* outBuf, size_t bufSize);                          // 6b
const char* TryListBoxSingleRow(void* control, char* outBuf, size_t bufSize);                 // 7
// 7b returns a tri-state: matched-with-text / matched-no-text-STOP / no-match.
// See the callout below — do not collapse this into the same bool-return
// shape as the rest without preserving the STOP behavior.
const char* TryPartySelectionPortrait(void* control, char* outBuf, size_t bufSize,
                                      bool& outForceStop);                                    // 7b
const char* TrySpeculativeVtableOverride(void* control, char* outBuf, size_t bufSize);        // 8
const char* TryInGameMenuIconFallback(void* control, void* owner, char* outBuf, size_t bufSize);   // 9a
const char* TryEquipSlotFallback(void* control, void* owner, char* outBuf, size_t bufSize);        // 9b
const char* TryInGameMapArrowFallback(void* control, void* owner, char* outBuf, size_t bufSize);   // 9b2
const char* TryWorkbenchSlotFallback(void* control, void* owner, char* outBuf, size_t bufSize);    // 9b3
const char* TryClassSelectionFallback(void* control, void* owner, char* outBuf, size_t bufSize);   // 9c
const char* TryPortraitCharGenFallback(void* control, void* owner, char* outBuf, size_t bufSize);  // 9d
const char* TrySiblingLabelFallback(void* control, char* outBuf, size_t bufSize);             // 9

void ApplyCycleCategoryPrefix(void* control, char* outBuf, size_t bufSize);   // tail 1
void AppendToggleStateSuffix(void* control, char* outBuf, size_t bufSize);    // tail 2
void AppendDisabledSuffix(void* control, void* ownerPanel, char* outBuf, size_t bufSize); // tail 3
}  // namespace
```

**Callout — section 7b's early return must be preserved exactly.** At line
920, when `control`'s vtable is the PartySelection-portrait vtable but no
name resolved, the *original* code does `return nullptr;` from `FromControl`
itself — skipping sections 8, 9a-9d, 9, and all three tail passes entirely,
not just section 7b. A naive extraction that turns every section into "try
this, if it returns non-null set `source` and continue" would silently
change this one case from "stop everything" to "fall through to the next
section," which could then produce a different announced string for that
control. The extraction must carry this section's `bool& outForceStop` (or
equivalent) and have `FromControl` check it immediately after calling
`TryPartySelectionPortrait`, returning `nullptr` right there if it's set —
mirroring today's exact control flow.

- Risk: mostly mechanical (same-TU, same-namespace, no header change,
  section boundaries are already comment-delimited in the source) **except**
  the section-7b early-return, which needs the explicit tri-state handling
  above, and section 0's owner-resolution hoist from A2 above (do A2 first,
  then thread the single `owner` into the new `TryPerKindVirtualRow`,
  `TryInGameMenuIconFallback`, etc. as a parameter instead of each
  recomputing it).
- Suggested in-game check given the "needs-in-game-test" bar for a
  1470-line rewrite even when behavior-preserving on paper: walk the chain
  through at least one control per extraction path that's reachable without
  a rare game state — a button, a label, a slider (Sound options), an
  editbox (chargen name), the InGameMenu icon strip, an equip slot, the
  workbench upgrade slot names, and the chargen class-selection / portrait
  screens. That's most of the existing Phase-1 smoke-test's chargen/menu
  coverage already, not new ground.
- Estimated line delta: roughly neutral in total lines (extraction adds
  function signatures and closing braces), but `FromControl` itself drops
  from ~1470 lines to an estimated 120-180 lines of orchestration, which is
  the actual goal (this is a readability/navigability change, not a size
  reduction).

### B2 — Section 8 hand-rolls the same text-extraction ladder `ExtractTextOrStrRefIndirect` already provides (menus_extract.cpp:970-1042)

Sections 2-5 of the same function call the shared helper
`ExtractTextOrStrRefIndirect(ctrl, textOffset, strRefOffset, textObjectOffset,
outBuf, bufSize)` (declared in `engine_reads.h`, defined in
`engine_reads.cpp:226`), which already does: (1) `ReadGuiString` first, (2)
inline `CExoString`, (3) strref → TLK, (4) text-object indirection with its
own SEH guard around step 4.

Section 8 (the speculative-vtable-override path) instead hand-writes the
same 3-of-4 steps twice — once for the "label" attempt, once for the
"button" attempt — as "Path A / Path B / Path C" comments, each wrapped in
its own `__try`/`__except` with `Menus.SpecRead` tracing:
```cpp
__try {
    if (ReadCExoString(control, kLabelTextOffset, text, sizeof(text))) got = true;
    if (!got) { uint32_t strref = ReadU32(control, kLabelStrRefOffset); got = LookupTlk(strref, text, sizeof(text)); }
    if (!got) { /* text_object indirection, same shape as step 4 of ExtractTextOrStrRefIndirect */ }
} __except (EXCEPTION_EXECUTE_HANDLER) { ... }
```
- Why flagged as an AI-pattern (copy-paste an abstraction should own) rather
  than a straight duplication cleanup: the two aren't byte-identical.
  Section 8's version **skips the `ReadGuiString` step** that
  `ExtractTextOrStrRefIndirect` tries first, and adds its own
  hit/miss/empty `Trace` calls that the shared helper doesn't have. Whether
  `ReadGuiString` would ever succeed for these speculative vtables (which by
  definition failed the normal `AsLabel`/`AsButton` downcast) is unverified
  — it's plausible it would, since `ReadGuiString` reads by struct offset,
  not by vtable identity.
- Proposed change (needs a decision, not a mechanical swap): replace the
  manual Path A/B/C block with a call to `ExtractTextOrStrRefIndirect`,
  wrapped in the same outer `__try`/`__except` for the existing
  `Menus.SpecRead` trace line. This is a **behavior risk**, not just a
  refactor, because it adds the `ReadGuiString` path that section 8
  currently doesn't try — if that path fires where it previously didn't,
  the extracted text could differ (probably an improvement — an extra
  chance to find real text — but it's a change, not a no-op).
- Risk: needs-in-game-test if executed. Exact action: focus a control that
  currently resolves via `"label-spec"`/`"button-spec"` (the InGameMenu
  icon strip is the canonical case per the section-8 comment) and confirm
  the same text is announced before/after.
- Estimated line delta: about −55 lines if merged.

## Findings (possible bugs — user decides)

None found beyond the one already known and accepted in `STATE.md` (the
`Disarm*` park-latch clear, confirmed intentional and unchanged by this
scan). I diffed the candidate-23 split commit (`41400bf`) against the
pre-split `menus_listbox.cpp` line-for-line specifically looking for a
behavior change the commit message didn't call out, and found none: the
state moves, the two monitors move with identical logic (module-for-module,
down to the exact `s_equipPickerActive`/`IsEquipPickerArmed()` etc.
substitutions), and `MonitorEquipPickerSelection`'s later switch to
`FindPanelByKind` (visible in the CURRENT file but not in `41400bf`'s diff)
comes from the separate, already-approved `a42a152` commit, not from the
split itself.

One thing worth the user's eyes even though it isn't a bug: `MonitorEquipPickerSelection`
and `MonitorWorkbenchUpgradePicker` gate differently — the workbench monitor
returns immediately `if (!s_workbenchUpgradePickerActive)`, while the equip
monitor runs its full panel-lookup and row-selection-announce logic
unconditionally (only the cursor-park sub-step is gated on
`s_equipPickerActive`). This asymmetry predates the split (present
identically in the pre-split code per the diff) and isn't something this
scan can call a bug — just flagging the inconsistency in case it's not
intentional. Not proposing a change; would need the equip-vs-workbench
panel semantics to judge whether one of the two is actually the right
shape for both.

## Candidate 28 — narrow-header include opportunities

- `menus_extract.cpp`: includes the `engine_offsets.h` aggregator and uses
  symbols spanning at least three of the four post-C8 sub-headers —
  `_fields` (dozens of `k*Offset` constants), `_addresses`/`_values`
  (`kAddrCSWCCreatureGetPortraitId`, `kVtableAsButton`,
  `kVtableCSWGuiFeatsCharGen`, `kVtableCSWGuiPortraitCharGen`, etc.). No
  narrow-header win available — migrating would still need most of the
  split.
- `menus_listbox.cpp`: same pattern — offsets (`kListBoxControlsOffset`,
  `kEquipItemEntryFlagsOffset`, ...), addresses (`kAddrGuiManagerPtr`,
  `kAddrRulesGlobal`, `kVtableCSWGuiFeatsCharGen`), and value constants
  together. No narrow-header win.
- `menus_listbox_picker.cpp`: includes `engine_offsets.h` for
  `kAddrGuiManagerPtr`, `kAddrMoveMouseToPosition`,
  `kListBoxControlsOffset`/`kListBoxSelectionIndexOffset`/etc. — again a
  mix of addresses and fields. No narrow-header win.
- `menus_extract.h` / `menus_listbox.h`: neither includes `engine_offsets.h`
  or any of the other tracked aggregators (`engine_player.h`,
  `engine_area.h`, `engine_panels.h`, `engine_reads.h`) — nothing to
  migrate.
- Note: `engine_reads.h`, though named in the brief's aggregator list, is
  currently a single 257-line header with no narrower sibling headers
  (`Glob engine_reads*.h` returns only itself) — it was the **.cpp** that
  got a companion split in Phase 1 (candidate 4, `engine_reads_items.cpp`),
  not the header. So there is no narrower header to migrate any of this
  batch's files to for `engine_reads.h` specifically.

## Files scanned with nothing to report

- `menus_listbox_picker.cpp` — clean, aside from A1 (mojibake, not present
  here — this file has zero mojibake matches) and A7 above (the temporary
  diagnostic, which is a keep-for-now flag, not a cleanup item). Confirmed
  byte-for-byte behavior match against its pre-split source via `git show`.
