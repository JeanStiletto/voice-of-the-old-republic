# Phase 3 scan — per-screen menu handlers batch

Scope (13 screens, 26 files):
- menus_modsettings.cpp (791) / .h
- menus_editbox.cpp (639) / .h
- menus_store.cpp (631) / .h
- menus_powers_levelup.cpp (469) / .h
- menus_keymap.cpp (430) / .h
- menus_keybinds.cpp (405) / .h
- menus_charsheet.cpp (397) / .h
- menus_abilities.cpp (350) / .h
- menus_pazaakdeck.cpp (325) / .h
- menus_journal.cpp (244) / .h
- menus_galaxymap.cpp (195) / .h
- menus_equipstats.cpp (152) / .h
- menus_credits.cpp (108) / .h

Method: full read of all 26 files (not excerpted). Cross-file comparison by
reading every `HandleInput`/`TryHandleInput` dispatcher and every "virtual
chain-row anchor" trio side by side. Verified specific claims with targeted
greps (quoted per finding below) rather than trusting a single read — in
particular re-derived call sites before calling anything unused, per the
brief's trap list. Read STATE.md's "Execution findings" and "Phase 2 status"
sections first; nothing below re-raises a settled item.

## Section A — general low-level cleanup

### A1 — `menus_journal.cpp:214` uses a raw offset that already has a name

`LogEntryCounts` reads the journal's items-listbox with a bare literal:

```cpp
void* lb = reinterpret_cast<unsigned char*>(panel) + 0x5c4;
```

`kJournalItemsListBoxOffset = 0x5c4` already exists
(`engine_offsets_fields.h:1124`, documented in the same struct-layout comment
block at line 1110) and is used by name elsewhere in the same subsystem
(`menus_chain.cpp:773`). `menus_journal.h:3`'s own doc comment even spells out
"`items_listbox at +0x5c4`" in prose right next to where the .cpp uses the raw
number instead of the constant. This is the diagnostic-only cross-check
function, so the risk of the magic number silently drifting from the real
offset is low-but-real (the two other offsets in the same file,
`kJournalDescriptionListBoxOffset` and `kJournalItemsListBoxOffset`'s siblings,
are already named).

Proposed change: replace the literal with `kJournalItemsListBoxOffset`.
Risk: mechanical. Line delta: 0.

### A2 — `menus_journal.cpp:230-242` `ForceRepopulate` skips the `Ok()` guard its siblings use

The brief asked me to check that this file's `R()`/`Ok()` guards (added during
Phase 2's address-rebase fix) read sensibly. Two of the file's three `R()`
addresses have an explicit `acc::addr::Ok(...)` pre-check with an informative
"unresolved on build %s" log line before the call:
`kAddrJournalOnControlEntered` (`SpeakDescription`, lines 81-97) and
`addrGetQuestJournal`/`addrGetInGameGui` (`LogEntryCounts`, lines 184-188).

`ForceRepopulate` calls `kAddrJournalPopulateItemListBox`
(defined in `engine_offsets_addresses.h:703`, already `R()`-wrapped there)
with only a bare `__try`/`__except` and no `Ok()` pre-check:

```cpp
void ForceRepopulate(void* panel) {
    if (!panel) return;
    __try {
        auto fn = reinterpret_cast<PFN_PanelThiscall>(
            kAddrJournalPopulateItemListBox);
        fn(panel);
        ...
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Journal", "ForceRepopulate SEH (panel=%p)", panel);
    }
}
```

This is not a live bug — `kAddrJournalPopulateItemListBox`'s reference address
(`0x00645330`) is already present in `engine_rebase_table.inc:173`, so `R()`
resolves it correctly on both known builds today, and even if it ever didn't,
the SEH catch would still prevent a crash (calling through a null function
pointer faults, which the `__except` swallows). The gap is purely diagnostic:
on a hypothetical future build where this address goes unmapped, the log would
read a generic "SEH (panel=%p)" instead of the same "unresolved on build %s"
message the other two guards in this file give, making the failure harder to
tell apart from a genuine access violation.

Proposed change (optional, cosmetic): add the same `Ok()` pre-check +
informative log used by `SpeakDescription`. Risk: low (purely additive log
path, no behavior change on any currently-known build). Line delta: +4.

### A3 — `menus_editbox.cpp:434-455` `FindMatchingPanel` and its per-spec `matches()` predicates lack the SEH guards every sibling vtable-check in this batch has

`FindMatchingPanel` walks the GUI manager's `panels[]` array (mirroring the
shape `FindPanelByKind` in `engine_panels.cpp:782-804` also walks) but, unlike
`FindPanelByKind`, does not wrap the `panelCount`/`panelData` reads in SEH:

```cpp
PanelMatch FindMatchingPanel() {
    PanelMatch m = {nullptr, nullptr};
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return m;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    ...
```

Worse, the two per-spec `matches()` predicates it calls per panel —
`ChargenNameMatches` (line 80-84) and `SaveNameMatches` (line 124-128) — read
the candidate panel's vtable with no SEH either:

```cpp
bool ChargenNameMatches(void* panel) {
    if (!panel) return false;
    void** vt = *reinterpret_cast<void***>(panel);
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiNameChargen;
}
```

Every other "is this panel/control the right vtable?" check in this same
batch wraps the read: `menus_store.cpp`'s `IsStorePanel` (194-203) and
`IsStoreItemEntry` (74-83), `menus_journal.cpp`'s `IsJournalEntry` (65-74),
and `menus_pazaakdeck.cpp`'s `VtableIs` (42-45) all use
`__try { ... } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }`.
`FindMatchingPanel` runs every tick via `TickEditboxMonitors` and walks up to
16 panels of arbitrary, unclassified kind (this scan is not
`FindPanelByKind`-shaped — see Candidate-28-adjacent note below — so it sees
panel types the rest of the code never touches). An unreadable first word on
any one of those panels currently has no fault boundary here.

Proposed change: wrap the manager-pointer reads in `FindMatchingPanel` and the
two `matches()` bodies in `__try`/`__except`, mirroring `IsStorePanel`'s
pattern exactly. Risk: low (matches an established, already-proven-safe
pattern used four other times in this same batch; changes only the fault path,
not the success path). Line delta: +10 to +12.

### A4 — `menus_pazaakdeck.cpp:181-196` reimplements the shared `FindControlById` with a different scan cap

```cpp
void* FindControlById(void* panel, int id) {
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (!list->data || list->size <= 0) return nullptr;
        int n = list->size > 256 ? 256 : list->size;
        ...
```

`acc::menus::detail::FindControlById` already exists
(`menus_internal.cpp:106-120`, declared in `menus_internal.h:66`) and is used
directly by `menus_powers_levelup.cpp` (`using
acc::menus::detail::FindControlById;`). It is logically identical to the
pazaakdeck copy except for the scan cap: the shared version caps at 64
controls, pazaakdeck's local copy caps at 256. Given the deck panel embeds 18
`all_cards` + 10 `sidedeck_gui` card widgets (28 widgets alone, stride
`0x31C`, at `menus_pazaakdeck.cpp:34-40`) plus overlay labels and buttons, it
plausibly does exceed 64 — so the higher cap looks like a deliberate,
needed fix rather than an accidental copy, and simply switching pazaakdeck to
call the shared helper would silently reintroduce a 64-entry truncation.

Proposed change: not a pure mechanical dedup. Either (a) raise the shared
`acc::menus::detail::FindControlById`'s cap to 256 (touches
`menus_internal.cpp`, outside this batch, and is a global behavior change for
every other caller: `menus_listbox.cpp`, `menus_chain.cpp`,
`peek_description.cpp`, `menus.cpp`, `menus_chargen_feats.cpp`,
`menus_listbox_picker.cpp`), or (b) add a `maxScan` parameter defaulting to 64
so pazaakdeck can opt into 256 without changing anyone else's behavior, then
delete the local copy. Risk: needs-in-game-test either way (exercise: open the
Pazaak side-deck builder, arrow through the full collection row and all 10
deck slots, confirm every card and the Play button still announce). Line
delta: roughly -16 (delete local copy) + a few in menus_internal.h/.cpp.

## Section B — AI-pattern findings

### B1 — `menus_charsheet.cpp` `MaybeAnnounce` reimplements the spec table `ExtractStatRow` already encodes

The file already carries a data-driven spec table, `k_statRowSpecs`
(lines 166-193), and a generic per-row formatter, `ExtractStatRow`
(lines 240-296), that reads any one stat row's engine label(s) and applies the
right `Fmt*` string for its `StatRowKind` (single value / value+modifier /
value+threshold / slider).

`MaybeAnnounce` (lines 298-395) does not use either. It re-declares 18 local
buffers, calls `ReadCharSheetLabel` on each of the same offsets the spec table
already lists by name, and re-implements the exact same format dispatch by
hand with a bespoke `append` lambda:

```cpp
if (str[0])    append(Get(Id::FmtCharSheetStr),
                      str, strMod[0] ? ", " : "", strMod);
if (dex[0])    append(Get(Id::FmtCharSheetDex),
                      dex, dexMod[0] ? ", " : "", dexMod);
...
```

Every one of these branches produces exactly what `ExtractStatRow` already
produces for the matching spec row (same offsets, same `Fmt*` ids, same
`", "` separator convention) — `MaybeAnnounce` is a hand-unrolled duplicate of
`ForEachStatRowAnchor` + `ExtractStatRow` composed into one string instead of
spoken as separate chain entries. The duplication already caused one real bug
in this file's history: the header comment at lines 48-55 documents that an
earlier commit swapped the HP/FP field assignment based on a confused godmode
session, and had to be corrected by re-verifying against a live log — exactly
the kind of drift that having the same offset→format mapping written out
twice (once in the table, once by hand in `MaybeAnnounce`) makes more likely
to recur.

Proposed change: rewrite `MaybeAnnounce` to iterate
`ForEachStatRowAnchor(panel, callback, &msg)` and call `ExtractStatRow` per
anchor, appending each row's already-formatted text into the composed
message instead of re-reading and re-formatting by hand. The FP-only-for-
Force-users gate is already enforced by `ForEachStatRowAnchor` (line 232) and
`FindSpecForControl` (line 206-208), so `MaybeAnnounce` would no longer need
its own separate `DisplayedHasForce` re-check either (line 370).
Risk: needs-in-game-test (exercise: open the Charakterblatt for a Force user
and a non-Force party member, confirm the composed first-sight speech still
reads class/level/XP/HP/(FP)/six attributes/alignment in the same order and
wording). Estimated line delta: roughly -55 (the 18 buffer declarations + 18
`ReadCharSheetLabel` calls + 12 `append` branches collapse to a ~10-line
loop).

### B2 — Three files hand-roll the same "virtual chain-row anchor" trio

`menus_credits.cpp`, `menus_equipstats.cpp`, and `menus_charsheet.cpp` each
implement the identical three-function shape for surfacing engine label
fields as virtual, text-only chain entries (labels that aren't
`IsChainNavigable` so the generic chain walker would otherwise skip them):

- `bool Is<X>RowAnchor(void* panel, void* labelControl)`
- `void ForEach<X>RowAnchor(void* panel, bool (*callback)(void* labelControl, int sortCy, void* userData), void* userData)`
- `bool Extract<X>Row(void* panel, void* labelControl, char* outBuf, size_t bufSize)`

All three are backed by a small spec table (`{valueOffset, format id(s),
sortCy}`) and a `FindSpecFor{Control,Panel}` lookup that walks the table
comparing either a panel-relative offset or a panel kind. Rough size of the
scaffolding versus the file:

- `menus_credits.cpp` — 108 lines total; `FindSpecForPanel` (8),
  `IsCreditsRowAnchor` (6), `ForEachCreditsRowAnchor` (15) — roughly a
  quarter of the file is the trio's plumbing around a 2-row table.
- `menus_equipstats.cpp` — 152 lines; `FindSpecForControl` (13),
  `IsEquipStatRowAnchor` (3), `ForEachEquipStatRowAnchor` (13) — the
  scaffolding is near-identical to credits' despite an unrelated 4-row table
  with dual-wield formatting.
- `menus_charsheet.cpp` — 397 lines; `FindSpecForControl` (18),
  `IsStatRowAnchor` (3), `ForEachStatRowAnchor` (18) — same shape again over
  a 12-row table (see B1 for the fourth copy of this pattern's logic inside
  the same file).

`menus_modsettings.cpp`'s `ForEachRootAnchor` (lines 359-382) is a simpler,
single-entry cousin of the same visitor shape (`callback(sentinel, sortCx,
sortCy, userData)`) rather than a table-driven lookup — worth naming for
completeness, but its callback signature already differs (extra `sortCx`)
and it isn't spec-table-backed, so it doesn't fit the same generalisation
cleanly.

What genuinely varies between the three files is the *format* dispatch inside
`Extract*Row` — credits reads one plain value, equipstats branches on a
dual-wield peer label, charsheet branches on four different `StatRowKind`
shapes (including a slider read). The *lookup and iteration* scaffolding
(`FindSpec`, `IsAnchor`, `ForEachAnchor`) is where the real, mechanical
duplication is.

A shared helper's signature would look like:

```cpp
struct VirtualRowSpec {
    acc::engine::PanelKind gateKind;  // PanelKind::Unknown = "match any"; checked
                                       // first so credits' per-kind-only spec and
                                       // equipstats'/charsheet's per-offset specs
                                       // both fit the same table shape
    size_t   valueOffset;
    int      sortCy;
    // Per-row read+format hook — kept as a function pointer rather than an
    // enum of row "kinds" because the four existing kinds (plain, value+mod,
    // value+threshold, slider, dual-wield) don't obviously stay closed; a
    // shared enum would just relocate the duplication into one big switch.
    bool (*format)(void* panel, const VirtualRowSpec& self,
                   char* outBuf, size_t bufSize);
};

bool IsVirtualRowAnchor(void* panel, void* labelControl,
                        const VirtualRowSpec* table, int count);
void ForEachVirtualRowAnchor(void* panel, const VirtualRowSpec* table, int count,
                             bool (*callback)(void* labelControl, int sortCy,
                                              void* userData),
                             void* userData);
bool ExtractVirtualRow(void* panel, void* labelControl,
                       const VirtualRowSpec* table, int count,
                       char* outBuf, size_t bufSize);
```

Each of the three files would keep its own spec table + per-row `format`
callbacks (where the genuine differences live) and drop the ~35-45 lines per
file of `FindSpec`/`IsAnchor`/`ForEachAnchor` boilerplate.

Honesty check: this is a real, well-evidenced duplication, but executing it
is not simply "delete the copies" — both callers of these three modules
(`menus_chain.cpp`'s `RebindChain`, which calls all three `ForEach*Anchor`
functions, and `menus_extract.cpp`'s `FromControl`, which calls all three
`Is*Anchor`/`Extract*Row` pairs) live outside this batch, so a correct
extraction needs to read those two files first and touches three already-
shipped, in-game-load-bearing chain-entry mechanisms at once. Given Phase 1's
candidates 13/24 were reverted specifically because a function-level read
missed shared state, I'd weight this as medium-confidence-execute, not a
slam dunk: the scaffolding duplication is real and the risk is bounded (no
state is being merged, only structurally-identical lookup code), but it's a
three-file, two-outside-caller change, not a same-file mechanical one.

### B3 — the same "clamp cursor to \[0, count) with Up/Down/Home/End" block is written out four times

`menus_keybinds.cpp` contains the identical block twice in the same file —
category level (lines 315-326) and action level (lines 352-363):

```cpp
// category level
if (up || down || home || end) {
    int ni = g_catCursor;
    if      (up)   ni = g_catCursor - 1;
    else if (down) ni = g_catCursor + 1;
    else if (home) ni = 0;
    else           ni = kCatLevelCount - 1;
    if (ni < 0) ni = 0;
    if (ni >= kCatLevelCount) ni = kCatLevelCount - 1;
    g_catCursor = ni;
    SpeakCategory(/*interrupt=*/true);
    return true;
}
```

```cpp
// action level, same file
if (up || down || home || end) {
    int ni = g_actCursor;
    if      (up)   ni = g_actCursor - 1;
    else if (down) ni = g_actCursor + 1;
    else if (home) ni = 0;
    else           ni = c.count - 1;
    if (ni < 0) ni = 0;
    if (ni >= c.count) ni = c.count - 1;
    g_actCursor = ni;
    SpeakActionRow(/*interrupt=*/true);
    return true;
}
```

`menus_keymap.cpp`'s tab-level block (lines 324-337) is the same shape a
third time:

```cpp
if (isUp || isDown || isHome || isEnd) {
    int ni = s_tabCursor;
    if      (isUp)   ni = s_tabCursor - 1;
    else if (isDown) ni = s_tabCursor + 1;
    else if (isHome) ni = 0;
    else             ni = kTabEntryCount - 1;
    if (ni < 0) ni = 0;
    if (ni >= kTabEntryCount) ni = kTabEntryCount - 1;
    s_tabCursor = ni;
    AnnounceTabEntry(activePanel, s_tabCursor);
    ...
```

`menus_abilities.cpp`'s tab-level block (lines 300-314) is a related fourth
instance, though shaped slightly differently (it clamps an index into a
filtered array of available tabs rather than a flat cursor, and only supports
Up/Down — Home/End fall through at that level).

This is the same clamp-not-wrap idiom the project already treats as a
deliberate, named convention elsewhere (submenu Up/Down clamps, never wraps —
the repeated boundary label is the intended cue), which makes it a good
candidate for a single named helper rather than four hand-written copies that
could drift out of sync with each other. A small helper in
`menus_internal.h`:

```cpp
// Step `cur` by ±1 (Up/Down) or jump to 0/count-1 (Home/End), clamped to
// [0, count-1]. No wrap. `count <= 0` returns 0.
int ClampedCursorStep(int cur, int count, bool up, bool down, bool home, bool end);
```

would let all three verbatim sites collapse from ~9 lines to 2
(`int ni = ClampedCursorStep(g_catCursor, kCatLevelCount, up, down, home, end); g_catCursor = ni;`),
and the `abilities.cpp` site could reuse it for its up/down-only case (pass
`home=false, end=false`). Risk: mechanical (pure integer function, trivially
equivalent to the four existing bodies — I compared all four control-flow
shapes line by line above). Estimated line delta: -21 across the three
verbatim sites, +8 for the new helper, net roughly -13; `abilities.cpp`'s
partial adoption would save a further few lines if taken.

## Findings (possible bugs — user decides)

None. Nothing found in this batch that looks like a behavior bug rather than
a cleanup/consistency item — A3's missing SEH guard is a latent crash-hardening
gap, not a demonstrated bug (no fault has been observed; the existing pattern
elsewhere in the batch is what flags it), so it's filed under Section A rather
than here.

## Candidate 28 — narrow-header include opportunities

- `menus_modsettings.cpp` — does not include `engine_offsets.h`; already narrow.
- `menus_keybinds.cpp` — does not include `engine_offsets.h`; already narrow.
- `menus_editbox.cpp` — uses only `kAddr*`/`kVtable*` (addresses) and `k*Offset`
  (fields); could drop to those two split headers instead of the full aggregator.
- `menus_store.cpp` — same as editbox: addresses + fields only.
- `menus_powers_levelup.cpp` — addresses + fields + `CExoArrayList` (types);
  no `values.h` content used.
- `menus_keymap.cpp` — addresses + fields + `CExoArrayList` (types); no
  `values.h` content used.
- `menus_charsheet.cpp` — fields only (no `kAddr*`, no `kVtable*`, no real
  `CExoArrayList`/`CExoString` type usage — those words only appear in
  comments). Cleanest candidate in the batch.
- `menus_abilities.cpp` — uses all four split headers (addresses, fields,
  types via `CExoArrayList`, and values via
  `kAbilitiesPanelCodeChartUp/Down`); no narrowing available.
- `menus_pazaakdeck.cpp` — fields + types only; the file's own include
  comment (`// CExoArrayList, kPanelControlsOffset, kControlIdOffset`)
  already documents exactly this — its local vtable/address constants are
  all separately `R()`-defined in-file, not pulled from `engine_offsets.h`.
- `menus_journal.cpp` — addresses (`kVtableCSWGuiJournalItemEntry`) + fields +
  `CExoArrayList` (types); no `values.h` content used.
- `menus_galaxymap.cpp` — fields only; the file's own include comment
  (`// kLabelGuiStringPtrOffset`) already says so. Its `kAddr*` constants are
  separately `R()`-defined in-file. Cleanest candidate alongside charsheet.
- `menus_equipstats.cpp` — fields only.
- `menus_credits.cpp` — fields only.

Note: `engine_panels.h` (also named in the brief's aggregator list) has zero
`#include` directives of its own — it is already the narrowest possible form,
so there is nothing to further split for the files in this batch that include
it.

## Files scanned with nothing to report

- `menus_modsettings.h`, `menus_editbox.h`, `menus_store.h`,
  `menus_powers_levelup.h`, `menus_keymap.h`, `menus_keybinds.h`,
  `menus_charsheet.h`, `menus_abilities.h`, `menus_pazaakdeck.h`,
  `menus_journal.h`, `menus_galaxymap.h`, `menus_equipstats.h`,
  `menus_credits.h` — all 13 headers are documentation-heavy and accurate
  against their .cpp; no stale references or dead declarations found.
- `menus_store.cpp` and `menus_pazaakdeck.cpp`'s SEH-wrapped one-off field
  readers (`ReadListBoxControlBitFlags`, `ReadRowObjId`, `ReadItemStock`,
  `ReadStorePlayerGold`, `ReadIntAt`, `VtableIs`, etc.) look repetitive at a
  glance but each reads a different field/type through the established
  per-file SEH-guard convention — not flagged as duplication needing an
  abstraction.
- `menus_galaxymap.cpp` — clean; already uses `FindPanelByKind` (one of the
  eight Phase 2 converted), and its two `R()`/`Ok()` guards
  (`kAddrGalaxyHandleInput`) read sensibly with an informative unresolved-log
  path, matching the pattern the brief asked me to check.
- Checked explicitly and confirmed NOT applicable: whether
  `menus_editbox.cpp`'s `FindMatchingPanel` (see A3) should be converted to
  `FindPanelByKind`. It cannot — `FindPanelByKind` matches a single
  `PanelKind` via `IdentifyPanel`, but neither the chargen Name panel nor the
  save-name popup have a `PanelKind` enumerator (grepped
  `engine_panels.h` for `ChargenName`/`SaveName`/`NameChargen`: no matches).
  `FindMatchingPanel`'s per-spec vtable-equality scan is the only way to
  classify these two panels, so it is correctly a hand-rolled scan, not a
  missed conversion — it just needs the SEH guard noted in A3.
- Checked and confirmed NOT an issue (Trap #1 discipline): before treating
  anything as unused/dead in this batch I re-derived actual call sites rather
  than trusting a file-name grep. Notably `abilities::HandleInput` and
  `powers_levelup::HandleInput` both take unused `(int n, void* thisPtr)`
  parameters that look like copy-paste bloat in isolation — grepping
  `menus_dispatch.cpp` (outside this batch) shows they deliberately share the
  6-argument `(n, thisPtr, activePanel, param_1, param_2, rv)` shape with
  `listbox::TryHandleInput`, `editbox::TryHandleInput`, and
  `chargen_feats::HandleInput` in the same dispatch chain, even where a given
  handler doesn't need `n`/`thisPtr` internally. This is a deliberate
  dispatch-contract match, not an inconsistency local to this batch — not
  reported as a finding.
