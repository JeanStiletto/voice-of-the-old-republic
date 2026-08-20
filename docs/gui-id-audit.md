# .gui control-id audit — getting off file-authored ids

Status: IN PROGRESS (started 2026-08-18). Tracks the conversion of every
lookup that trusts a `.gui`-authored control id to an engine-truth
mechanism. Update the per-surface status lines as work lands.

DONE + tested: InGameEquip (committed 54533ee), WorkbenchUpgrade
(committed df625d6), SaveLoad (tested both games 2026-08-18 — panel
identity moved to the CSWGuiSaveLoad vtable, controls to ctor members;
logs patch-20260818-132334 (K1) / patch-20260818-132234 (K2): no
GuiIdMismatch, row nav + Enter/Esc + K2 chain-sync/slot-info all clean).
DONE + tested: crafting trio (K2 test round 2026-08-18, log
patch-20260818-135559: no GuiIdMismatch; Sel row-commit, auto-open
redirect, component create/breakdown commits, Q/E flip, cost suffix and
titles all clean — and the logged pointer arithmetic confirms the mined
offsets at runtime: commit btn − panel = 0x2f8 on Sel, 0xfe4/0x1384/
0x38dc on the component panel. CAVEAT: the chemical panel ran no runtime
path — no lab station in the test save. Its offsets carry the same
double decompile witnesses; the shared code path is proven via the
component twin. If a tester log ever shows GuiIdMismatch/Craft.* on a
lab station, that's the place to look).
DONE + tested (2026-08-18): powers level-up — K2 test round log
patch-20260818-204814: panel identified by vtable, 25-row chart rebuild,
cell focus/pick (OnPowerPicked, status 0->4), all three button rows
announce + commit, zero Powers.* GuiIdMismatch — and the logged Enter
targets confirm the mined offsets at runtime (accept = panel + 0x1578,
recommended = panel + 0x11d8). CAVEAT: the K1 side ran no runtime path —
the K1 test save has no Jedi and "Kraefte" stayed unavailable (K1 chargen
has no Force step), so K1 carries decompile witnesses only (double, from
the named ctor listing); the shared code path is proven via K2. Same
precedent as the crafting chemical panel: if a K1 log ever shows
GuiIdMismatch/Powers.*, look here first.
DONE + tested (2026-08-18): container — test round on BOTH games, logs
patch-20260818-213834 (K1) / patch-20260818-214027 (K2): no
GuiIdMismatch, row nav + take-all + give-mode + Shift-arrow peek all
clean, and the logged pointer arithmetic confirms three of the four
mined offsets at runtime in each game (K1 lb − panel = 0x7f0, BTN_OK
0xad0, BTN_GIVEITEMS 0xe58; K2 0x824 / 0xb14 / 0xeb4). CAVEAT: every
close in both rounds went through Enter/BTN_OK, so BTN_CANCEL has no
runtime witness — it keeps its two decompile witnesses (tagged
InitControl + the OnBButtonPressed registration, plus K2's gamepad 'b'
bind). Same precedent as the crafting chemical panel: if a log ever
shows GuiIdMismatch/Container.BTN_CANCEL, look there first.
DONE + tested (2026-08-20): chargen feats (Talente) — K1 round 2026-08-19
(all flows clean), K2 round log patch-20260820-105858: no Feats.*
GuiIdMismatch, 32-row chart rebuild, cell focus + OnFeatPicked (status
0->4), button rows announcing their real labels, Enter committing through
the resolved member — and the logged Enter targets confirm two of the
three mined K2 offsets at runtime (BTN_RECOMMENDED = panel + 0xf98,
BTN_ACCEPT = panel + 0xbf8), with the K2 button ORDER confirmed too (End
lands on Empfohlen, Up on OK — the mirrored K2 layout). CAVEAT: every exit
in both rounds went through Enter/BTN_ACCEPT, so BTN_BACK has no runtime
witness — it keeps its three decompile witnesses (tagged InitControl, the
bit_flags clear + AddEvent registration, and K2's gamepad 'b' bind). Same
precedent as the container BTN_CANCEL: if a log ever shows
GuiIdMismatch/Feats.BTN_BACK, look there first.
NEXT: pazaak, keymap, skill info box, script select. NEW FINDING (see
inventory): the loose WorkbenchSelect structural fallback misidentifies
level-up sub-screens in BOTH games.

Noted during the SaveLoad round (not a regression, optional follow-up):
K2 Esc on the save/load screen logs "Menus.Esc: ... no cancel/close
button found; passing through" — the generic chain Esc handler's
label-text probe doesn't find K2's Back button, and the engine's native
Esc closes the screen instead. Works fine; could route through
SaveLoadPanelBackButton if native Esc ever misbehaves.

## Conversion workflow (follow this per surface — written to run cold)

1. **Find the panel constructors.** K1 project is named: `ListFunctionsByName.java
   "<ClassName>"` via analyzeHeadless on `C:/Tools/ghidra-projects` kotor1
   (program `k1_win_gog_swkotor.exe`), or FindCallers.java on any handler
   the ctor registers (registration sites are DATA refs; FindCallers
   includes them). K2 project (kotor2 / `swkotor2.exe`) is unnamed — reach
   its ctor from recorded K2 twin addresses in
   engine_offsets_addresses.h, or by FindCallers on a known K2 handler.
2. **Decompile both ctors** with `tools/ghidra-scripts/decomp.sh 0xADDR`
   (K2: prefix `KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe`).
   K1 decompiles show tag strings + named fields; get raw offsets from
   `PrintListing.java` (the `LEA reg,[ESI+0xNNNN]` + `PUSH <tag-string>`
   pair before each InitControl call). K2 decompiles often strip call
   args — cross-read the K2 member from a HANDLER that uses it (the
   OnSlotSelected/OnSelectSlot pattern: `SetActiveControl(this + dword)`
   names the listbox) and from the ctor's structural mirror of K1
   (vector-ctor banks, the `byte+0x54 = 0x10` / `flags &= ~4` triples).
3. **HARD RULE — every offset needs a direct witness.** A tagged
   InitControl, a handler decompile that uses the member, or a runtime
   log line. NEVER infer by adjacency/stride ("one button after X"):
   the K2 upgrade BTN_BACK was inferred that way, was wrong, and Esc's
   activate on the resulting non-button CRASHED the game
   (patch-20260818-113825.log; corrected from the tripwire line itself
   to +0x3b58). Prefer two independent witnesses; record them in the
   constant's comment.
4. **Add constants** to engine_offsets_fields.h (`acc::off::Pick`,
   embedded members = address-of, never deref the offset itself).
5. **Add resolvers** in menus_internal.{h,cpp} using
   `PanelMemberWithTripwire` (member is the answer; the old .gui id is
   only the GuiIdMismatch tripwire — pass guiId -1 where the historical
   id is ambiguous on one game). Slot/button-array membership =
   pointer-arithmetic index fns (see EquipSlotIndexFromButton /
   UpgradeSlotIndexFromButton; no SEH needed, no control deref).
6. **Convert every consumer** (grep the id constant + any id-compare
   predicate), delete the id-based matcher, keep announce-only labels on
   ids (Tier 2 — degradation there is acceptable and never drives input).
7. **Build (`kdev build`), apply BOTH games, test round, then grep the
   fresh logs**: `GuiIdMismatch` must be absent on vanilla installs, and
   the converted flows must show their normal lines. Only then commit
   (one tested batch per surface; changelog bullet if user-facing).
   Powers level-up and container shipped WITHOUT a changelog bullet —
   pure hardening, nothing a vanilla player notices. When the Unreleased
   section is cut into a release, fold them into the existing
   equipment/workbench/save-load/crafting hardening story rather than
   leaving the screen list looking arbitrary.

Lesson that rode along (not id-related but found by these rounds): a
picker's cursor park must use `ParkCursorToCorner`, not a button's
extent center — K1 hit-testing resolves warped coordinates offset from
extents on some panels, so a "safe" button center can leave the engine
hovering a list row, and hover-select then reverts every keyboard
selection (the K1 crystal-picker "only 2 of 8 crystals reachable" bug).

## Why (the 077noequipment failure)

A K2 beta tester's install carries a variant `equip_p.gui` whose control
ids are shifted by one in the tail band (their file has one extra control
in the stat-label region). On that install our hardcoded K2 ids resolved
to the wrong controls:

- LB_ITEMS: ours 41, theirs 42 → we read a button as a listbox
  (`rows=400065456`, a heap pointer), arrow nav dead, Enter-commit dead.
- BTN_EQUIP: ours 40, theirs 41 → commit would fire the Close handler.
- BTN_BACK: ours 39, theirs 40 → cursor park landed on the "Damage" label.
- The real LB_ITEMS rows leaked into the button chain (the exclusion
  filters the wrong id).

The slot buttons (15..25, 48) happened to match, so slot navigation
worked and only the picker half died — quietly. Every control id we
hardcode has this exposure: `.gui` files are data, and any content mod
may replace them per install. Which mod ships the tester's variant is
still unknown (their `override\equip_p.gui` was requested).

## Why tags are NOT the fix (decompile evidence)

`CSWGuiControl::Load` (K1 @0x00418840, the load-by-tag overload) iterates
the GFF CONTROLS list, reads each element's "TAG" field from the FILE,
compares against the requested tag, and loads the matching element. The
tag is consumed at load time; the in-memory control keeps only the int
`id` (K1 +0x50 / K2 +0x54, from the sibling overload @0x0041b8e0 reading
"ID"). There is no tag on the runtime object to compare against.

## The fix: panel-member offsets (engine's own bindings)

The panel constructors load each control they care about BY TAG into a
member — mostly EMBEDDED sub-objects (buttons 0x1c4 apart in
CSWGuiInGameEquip), sometimes pointers. Those members are how the engine
itself operates the screen (OnSelectSlot uses `&this->items_listbox`,
never an id lookup), so they are immune to `.gui` renumbering by
construction. Reading them is our existing offset paradigm — per-exe
constants behind the installer's SHA-256 gate — no new mechanism.

Conversion rule per site:
- PRIMARY: `panel + memberOffset` (address-of for embedded objects,
  deref for pointer members — check each ctor).
- FALLBACK + TRIPWIRE: keep the id lookup; when both resolve and
  disagree, trust the member and log `GuiIdMismatch` loudly (one line
  per panel instance) so tester logs reveal variant .gui files.
- Where no member exists (purely decorative controls the panel never
  touches), keep the id and accept announce-degradation as the failure
  mode; never let such a control drive input or state.

## Offsets mined so far (2026-08-18)

CSWGuiInGameEquip:
- items_listbox EMBEDDED: K1 +0x30d8 (OnSelectSlot @0x006b8eb0:
  `LEA EDI,[ESI+0x30d8]` → SetActiveControl/SetSelectedControl),
  K2 +0x372c (OnSelectSlot @0x008abe70: `in_ECX+0xdcb*4` passed to
  SetActiveControl, its first dword read as vtable).
- selected slot button storage: K1 +0x42a0, K2 +0x50c8.
- selected_slot type mask: K1 +0x4278 (K2 +0x5098, from earlier RE).
- picker-open flag: K1 +0x4270 / K2 +0x5094 (already shipped as
  kEquipPickerOpenFlagOff).
- BTN_BACK etc.: kEquipPanelBackButtonOffset K1 +0x385c / K2 +0x3edc and
  the four character-cycle buttons were already mined (embedded, 0x1c4
  stride).
- equip_button EMBEDDED: K1 +0x3698 (contiguous button run: back 0x385c −
  0x1c4; the run's other four members match the recorded party-cycle
  constants), K2 +0x3d0c (ctor "BTN_EQUIP" InitControl, dword 0xf43).
- slot buttons EMBEDDED ARRAY: K1 +0x68 stride 0x1c4 × 9, K2 +0x6c stride
  0x1d0 × 11 (ctor _eh_vector_constructor_iterator_ + per-slot InitControl
  loop). Slot labels array: K1 +0x104c stride 0x140, K2 +0x145c stride
  0x148. Slot ORDER identical in both games (weapL, weapR, head, armL,
  armR, body, hands, implant, belt [, weapL2, weapR2]); the ctor loop
  writes the index into each button's custom_value (+0x58 K1 / +0x5c K2)
  and seeds the parallel item-id array (+0x427c / +0x509c) in the same
  order. All shipped as kEquipPanel* constants 2026-08-18.
- Three independent cross-checks passed: K2 ctor's BTN_BACK dword matches
  the previously mined 0x3edc, and BTN_PREVNPC/BTN_NEXTNPC match the
  recorded character-cycle constants 0x50f0/0x52c0.

K2 OnSelectSlot behavioural note (affects UX, not ids): K2 refuses to
open the picker via a PER-SLOT no-candidates flag (+0x2274..+0x229c band,
helper @0x008ab640) that counts only UNEQUIPPED fitting items; K1 instead
checks `items_listbox.controls.size != 1`, which includes the equipped
row. So on K2 an occupied slot with no spare pops the "no items" modal —
vanilla behaviour, mouse included. The mod now follows that modal with
"Item still equipped: <name>" (FmtEquipStillEquipped).

CSWGuiSaveLoad (mined 2026-08-18; K1 ctor @0x006cc680 — named symbol,
tag strings dumped from the listing's InitControl pairs; K2 ctor
FUN_00850770, found via DATA refs to its named s_BTN_SAVELOAD /
s_LB_GAMES strings, tags dumped the same way; layouts mirror exactly):
- Panel identity = VTABLE, not ids: kVtableCSWGuiSaveLoad K1 0x00757650
  (labelled CSWGuiSaveLoad_vtable) / K2 0x009A3FBC (both written at
  object+0 by their ctors). Both IsSaveLoadPanel (menus layer) and
  IsSaveLoadStructural (engine layer) are now one HasVtable call; the
  old id-quartet probe and its workbench-collision defence are deleted,
  and the SaveLoad probe moved into IdentifyPanel's collision-proof
  vtable group.
- games_listbox EMBEDDED ("LB_GAMES"): K1 +0x934 / K2 +0x970.
- action button EMBEDDED ("BTN_SAVELOAD"): K1 +0xc14 / K2 +0x11c0.
  Second witness in both games: the ctor registers its onClick to a
  save- or load-handler depending on the mode param (K1 0x6cbb60/
  0x6cc0e0, K2 0x8578b0/0x8528b0); K2 also binds gamepad 'a' to it.
- back button EMBEDDED ("BTN_BACK"): K1 +0xdd8 / K2 +0x1390 (K2 gamepad
  'b' bind corroborates).
- delete button EMBEDDED ("BTN_DELETE"): K1 +0xf9c / K2 +0x1730 ('d'
  bind). Recorded but unused — no consumer activates Delete directly
  (it stays an ordinary chain button).
- K2 info labels EMBEDDED: LBL_PLANETNAME +0x308, LBL_AREANAME +0x450,
  LBL_TIMEPLAYED +0xda8 (K1 twins +0x2f4/+0x434 recorded; K1 has no
  time label). AnnounceK2SaveLoadInfo reads these members now — the
  Tier-2 id reads (3/5/9) came along for free with direct witnesses.
- Full member tables (labels, K2-only CB_CLOUDSAVE/BTN_FILTER/LBL_BAR*)
  in engine_offsets_fields.h at kSaveLoadPanel*Offset.

K2 crafting trio (mined 2026-08-18; all K2-only consumers, offsets poison
on K1). Panel identity was ALREADY vtable-based for all three — this round
converted control resolution only. Ctors found via FindCallers on the
known BTN_Accept handlers (their registrations are DATA refs in the ctors)
plus DATA refs to each vtable; all three ctors decompile WITH tag strings
on K2, so every member has a direct tagged-InitControl witness.
- CSWGuiUpgradeSelection ctor FUN_008c6650 (dtor FUN_008c6e90):
  LBL_TITLE +0x68, BTN_UPGRADEITEMS +0x2f8, BTN_BACK +0x4c8,
  LB_UPGRADELIST +0x117c. Second witnesses: BTN_BACK native-Esc bind
  (0x62), BTN_UPGRADEITEMS accept bind (0x61) AND its handler
  FUN_008c7f60 comparing its param against `this + 0x2f8`.
- CSWGuiCreateItem (component_p) ctor FUN_008d1020: LB_SHOPITEMS +0x35ec,
  LB_INVITEMS +0x38dc, BTN_Accept +0xfe4, BTN_Examine +0x1384, LBL_TITLE
  +0x2b4c. Cross-checks: `flags &= ~4` triple at each button +0x48,
  initial-visibility clear at LB_INVITEMS+0x48, and the ctor's
  skill-factor write landing exactly on the previously mined
  kCraftComponentSkillFactorOffset 0x3ed4.
- CSWGuiCreateMedicalItem (chemical_p) ctor FUN_008d6b90: LB_SHOPITEMS
  +0x2b0c, LB_INVITEMS +0x2dfc, BTN_Accept +0x118c, BTN_Examine +0x152c,
  LBL_TITLE +0xac4. Same cross-check families incl. the mined 0x33f4
  skill factor.
- Resolvers: UpgradeSelPanel* / CraftPanel* in menus_internal.cpp (the
  CraftPanel* ones dispatch component-vs-chemical on IdentifyPanel; the
  BTN_Examine tripwire id follows the kind — the two .gui files disagree,
  13 vs 14). The CraftRowCommit pending op no longer carries a button id
  at all: DispatchRowCommit re-resolves the commit button from the
  panel's members at drain time. The upgrade auto-open redirect now uses
  the existing UpgradePanelBackButton resolver. Titles for the three
  mined panels converted (Tier-2 for free); upgradeitems_p / upgrade_p
  titles stay id-based announce-only.

CSWGuiPowersLevelUp (mined 2026-08-18; K1 ctor @0x006f2180 — named
symbol, raw offsets from the listing's tagged-InitControl LEA pairs; K2
ctor FUN_009074E0 — decompiles WITH tag strings, offsets are dword
indices ×4 off in_ECX; same member order both games):
- Panel identity was ALREADY vtable-based (kVtableCSWGuiPowersLevelUp);
  this round deleted the id-based structural fallback ({lb 6, lb 7,
  buttons 11/12} — K1 ids that never matched on K2) from
  IsPowersLevelUpStructural, which is now one HasVtable call like
  SaveLoad.
- Member table (K1 / K2): SUB_TITLE_LBL +0x1ac/+0x1b8, LBL_POWER
  +0xbac/+0xab0, LB_POWERS +0xcec/+0xbf8, LB_DESC +0xfcc/+0xee8,
  RECOMMENDED_BTN +0x12ac/+0x11d8, ACCEPT_BTN +0x1634/+0x1578,
  BACK_BTN +0x17f8/+0x1748.
- Second witnesses both games: each button gets the ctor's
  `bit_flags &= ~4` clear + a gamepad-callback registration on the
  member (K1 OnYButtonPressed/OnXButtonPressed/AcceptButtonCallback/
  OnBButtonPressed; K2 'y'/'a'/'b' binds); LB_POWERS additionally gets
  the selection-changed + double-click callback registrations (K1
  0x6f1940/0x6f2110, K2 0x909D00/0x909D70) and is the ctor's final
  SetActiveControl target. K2 cross-check: SELECT_BTN lands at +0x13a8 —
  the exact value the earlier OnEnterPower RE had recorded as
  "ctor-witnessed"; LBL_BAR1/2 follow BACK_BTN at label stride 0x148.
- SELECT_BTN deliberately has no constant: Enter on a chart cell
  dispatches OnPowerPicked directly (the same engine action), as before.

CSWGuiContainer (mined 2026-08-18; K1 ctor CSWGuiContainer::CSWGuiContainer
@0x006b6dc0 — named symbol, raw offsets from the listing's tagged-InitControl
LEA pairs; K2 ctor FUN_008b1ea0 — decompiles WITH tag strings, offsets are
dword indices ×4 off in_ECX. Same member order in both games: six labels,
then the listbox, then ok / cancel / giveitems):
- Panel identity was ALREADY engine-truth — Container is a gui-manager panel
  slot (Same(0x54)), so this round converted control resolution only.
- Member table (K1 / K2): LB_ITEMS +0x7f0/+0x824, BTN_OK +0xad0/+0xb14,
  BTN_CANCEL +0xc94/+0xce4, BTN_GIVEITEMS +0xe58/+0xeb4.
- Second witnesses both games: the ctor's post-layout listbox setup lands on
  LB_ITEMS + kListBoxBitFlagsOffset exactly (K1 +0x2bc / K2 +0x2cc) and calls
  SetEnabled through the member's own vtable; each button gets the
  `bit_flags &= ~4` clear at member+0x44 (K2 +0x48) plus its AddEvent(0x27)
  handler registration (AcceptButtonCallback / OnXButtonPressed /
  OnBButtonPressed), and on K2 BTN_CANCEL also takes the gamepad 'b' bind
  (0x62) — the same corroboration SaveLoad's back button gave.
- Third cross-check: the two mined listbox offsets reproduce the values
  peek_description.cpp carried from Lane's DB, independently, from the ctors.
- Both .gui files number these identically (LB_ITEMS 2, BTN_OK 3,
  BTN_GIVEITEMS 4, BTN_CANCEL 5), so one tripwire id per control covers both
  games.

CSWGuiFeatsCharGen (mined 2026-08-18; K1 ctor
CSWGuiFeatsCharGen::CSWGuiFeatsCharGen @0x006f3d60 — named symbol whose
decompile names each member (accept_button / reccomended_button /
back_button) while the listing's tagged-InitControl LEA pairs give the
raw offsets; K2 ctor FUN_00909E00 — decompiles WITH tag strings, offsets
are dword indices x4 off in_ECX):
- Panel identity was ALREADY vtable-based (kVtableCSWGuiFeatsCharGen);
  this round converted control resolution only. The panel's name label,
  select button, both listboxes and the skill-flow chart were already
  member offsets — only the three activatable buttons were id-driven.
- Member table (K1 / K2): BTN_ACCEPT +0xcec/+0xbf8, BTN_BACK
  +0xeb0/+0xdc8, BTN_RECOMMENDED +0x1074/+0xf98. Both ctors call
  InitControl in the order accept / recommended / back / select while
  laying the members out accept < back < recommended < select, one button
  stride apart (K1 0x1c4, K2 0x1d0) — the run ends on the already-shipped,
  runtime-proven BTN_SELECT member, which is the layout cross-check.
- Second witnesses both games: each button gets the ctor's
  `bit_flags &= ~4` clear (K1 member+0x44, K2 member+0x48) plus its
  AddEvent(0x27) registration — K1 binds CSWGuiPanel::OnXButtonPressed to
  accept, OnYButtonPressed to recommended, OnBButtonPressed to back; K2
  binds the pad keys explicitly ('a' 0x61 accept, 'b' 0x62 back, 'y' 0x79
  recommended). Back and recommended carry the same pad semantics in both
  games.
- Third cross-check (K2): the same ctor independently reproduces four
  constants already shipped — LBL_NAME +0xab0, BTN_SELECT +0x1168,
  LB_FEATS +0x15c8, LB_DESC +0x18b8 — which validates reading its dword
  indices x4.
- Resolvers: FeatsPanelAcceptButton / FeatsPanelBackButton /
  FeatsPanelRecommendedButton in menus_internal.cpp. The per-game id
  tables in menus_chargen_feats.cpp are gone: ButtonRow now carries a
  resolver function pointer instead of an id, and the focus log line
  reports the tag + resolved target instead of the id. kBtnBackId is
  deleted; Enter and Esc both go through QueueControlActivate.
- menus_pending's chargen-sub-close probe now does pointer equality
  against FeatsPanelAccept/BackButton for this panel (the id 11/12 probe
  was doubly wrong on K2: it numbers Accept=10/Back=9, and its id 12 is
  LB_FEATS, a listbox). The id probe remains for attributes and skills
  until their batches.
- NOTE: QueueButtonByIdActivate is NOT dead yet — menus_listbox.cpp still
  uses it for the skill info box, workbench items and script select, all
  still on the to-mine list.

## Inventory (audit of all id-trusting sites)

Tier 1 — drives input/state; convert to member offsets:
- InGameEquip picker trio (LB_ITEMS / BTN_EQUIP / BTN_BACK). STATUS:
  DONE 2026-08-18 (in-game test pending). All consumers now resolve via
  EquipPanelItemsListBox / EquipPanelEquipButton / EquipPanelBackButton
  (menus_internal.cpp); the .gui id survives only as the GuiIdMismatch
  tripwire log. Chain exclusion, picker monitor, spec find, Enter-commit,
  cursor park, and the BTN_EQUIP/BTN_BACK chain filter all converted.
- InGameEquip slot buttons + labels. STATUS: DONE 2026-08-18 (in-game
  test pending). IsEquipSlotButtonId (id compare) deleted; every consumer
  uses EquipSlotIndexFromButton/FromControl (embedded-array pointer
  arithmetic) — chain input, click-pitch capture, per-kind announce
  (k_equipSlotsByIndex, keyed by engine slot index), Shift-arrow peek,
  and the refusal follow-up's item read. Side win: KOTOR 2's two
  second-weapon-set slots now announce/peek like every other slot.
- WorkbenchUpgrade. STATUS: DONE — tested in-game both games 2026-08-18
  (K1 crystal picker full browse/install/remove; K2 install + Esc clean,
  no GuiIdMismatch). K2 BTN_BACK corrected to +0x3b58 after the first
  round's tripwire+crash (see workflow HARD RULE above).
  Mined from ctors (K1 @0x006c6b60 named decompile + listing, K2
  @0x008c9e10 listing) + K2 OnSlotSelected @0x008ceb00:
  items_listbox K1 +0x1580 / K2 +0x2380 (K2 witnessed twice: ctor setup
  and OnSlotSelected's SetActiveControl); BTN_ASSEMBLE K1 +0x2aa0 / K2
  +0x3694 (identified by the OnAssemble registration + the byte+0x54 =
  0x10 / flags&=~4 triple both ctors share); BTN_BACK K1 +0x2d84 / K2
  +0x3864 (K1 tagged InitControl, K2 one stride after assemble; K1's
  picker-open flag at +0x2f48 sits right after it — layout cross-check);
  slot buttons ONE contiguous run K1 7×0x1c4 @+0x64, K2 9×0x1d0 @+0x7a8
  (banks abut exactly). All consumers converted: chain exclusion +
  greyed-slot filter, Enter detection, spec find/commit, picker monitor,
  cursor park, Esc back-button route, per-kind announce (fallback names
  now keyed by array index: crystal bank first, then weapons — ctor
  construction order), Shift+arrow peek, peek listbox resolver.
  IsWorkbenchUpgradeSlotButtonId deleted. NOTE this also fixes a latent
  K2 bug: the old assemble id (24) collided with a K2 slot-button id, so
  the K1-style commit lookup could grab a slot button; the K2 commit path
  never used it, but the resolver now skips the id tripwire on K2 for
  that reason. Slot semantics still come from custom_value (per-bank),
  NOT the array index — only membership/identity moved to the array.
  Title label (id 25) stays id-based: announce-only, degrades gracefully.
- Crafting (K2 only): kSelUpgradeListId/kSelUpgradeItemsBtn/
  kCraftShopListId/kCraftInvListId/kCraftAcceptBtnId/kCraftExamineBtn*
  (menus_crafting.cpp). STATUS: DONE — tested in-game K2 2026-08-18
  (chemical panel untested, no lab station in save; see status caveat).
  All converted to UpgradeSelPanel*/CraftPanel* resolvers (see
  mined-offsets section); the id constants are deleted from
  menus_crafting.cpp — the historical ids live on only inside the
  resolvers as tripwires. The K2 upgrade-screen auto-open redirect's
  kUpgradeBackBtnId also deleted (uses UpgradePanelBackButton now).
  Remaining id-based in that TU (Tier 2, announce-only): upgradeitems_p
  LBL_TITLE (3) and upgrade_p LBL_TITLE (12).
- Powers level-up: IdPowersListbox/IdDescriptionLb/BtnRecommended/
  BtnAccept/BtnBack (menus_powers_levelup.cpp). STATUS: DONE — tested
  in-game K2 2026-08-18 (K1 runtime pending, no Jedi in test save — see
  the status caveat above). All per-game id fns deleted;
  consumers resolve via PowersPanel* (menus_internal.cpp): chart binding
  (LB_POWERS), power-name label, description listbox, sub-title override,
  button rows (announce + Enter via QueueControlActivate), Esc → BACK_BTN.
  The menus_pending chargen-sub-close probe now does pointer-equality
  against PowersPanelAccept/BackButton for this panel (fixes K2, where
  the id-11/12 probe missed — K2 numbers Accept=10/Back=9 and 11 is
  RECOMMENDED_BTN); the id probe remains for attr/skills/feats until
  their batches. Tier-2 labels (LBL_POWER, SUB_TITLE_LBL, LB_DESC) came
  along free with ctor witnesses.
- SaveLoad: SaveLoadLbGamesId/BtnSaveLoad/BtnBack/BtnDelete
  (menus_internal.cpp, menus_listbox.cpp; also used as the panel
  IDENTIFIER via IsSaveLoadPanel/IsSaveLoadStructural — double
  exposure). STATUS: DONE — tested in-game both games 2026-08-18.
  Identity = vtable (see mined-offsets section); LB_GAMES /
  BTN_SAVELOAD / BTN_BACK resolve via SaveLoadPanel* resolvers
  (ids now tripwire-only); Enter/Esc queue the member controls via the
  new QueueControlActivate; the K2 selection-sync monitor and the K2
  info-label announce converted too. BTN_DELETE id deleted (was only
  consumed by the shape probe). Tier-2 K2 detail labels converted
  along the way (direct witnesses were free).
- WorkbenchSelect loose structural fallback (engine_panels.cpp):
  misidentifies level-up sub-screens as WorkbenchSelect in BOTH games —
  witnessed 2026-08-18 by the audit's own tripwires: K1 attributes screen
  (patch-20260818-204614, also present pre-audit in patch-20260818-142801,
  so NOT a regression) and K2 skills screen (patch-20260818-204814, where
  the crafting UpgradeSel resolvers then probed the wrong panel and their
  LBL_TITLE/LB_UPGRADELIST tripwires fired — those two GuiIdMismatch
  lines mean "wrong panel", not "variant .gui"). Benign so far: the
  level-up sub-screen handlers identify their panels independently, and
  the mis-probed paths are announce-only. Fix = tighten or vtable the
  WorkbenchSelect probe (its "id 0 + id 9 button + id 10 button" shape is
  exactly the id-trusting pattern this audit deletes). STATUS: to mine
  (fold into the workbench-family follow-up).
- Container: kContainerBtnOkId/GiveId/CancelId (menus_listbox.cpp).
  STATUS: DONE — tested in-game both games 2026-08-18 (Esc/BTN_CANCEL
  untested, every close used take-all; see status caveat). All four controls resolve
  via ContainerPanel* (menus_internal.cpp): the spec's findListBox, the
  Enter -> BTN_OK take-all and Esc -> BTN_CANCEL commits (now
  QueueControlActivate), MonitorContainerSelection's per-row monitor, and
  the G give-mode hotkey. The id constants are deleted from menus.cpp
  (they were already dead there) and menus_listbox.cpp. Side win: the
  monitor and the spec used the FindListBoxChild heuristic ("first listbox
  in controls[]") rather than an id — a variant .gui that added a second
  listbox would have taken the wrong one; both now name the engine's own
  member. peek_description.cpp's duplicated copy of the container listbox
  offset is gone too — it calls the shared resolver.
- Pazaak deck builder: kControlPlayId/kControlClearId + side arithmetic
  (menus_pazaakdeck.cpp private FindControlById). STATUS: to mine.
- Pazaak wager: WagerLess/More/MaxLabel gui ids (minigame_pazaak.cpp,
  menus_extract.cpp). STATUS: to mine.
- Keymap screen: kIdListBox/Default/Accept/Cancel/Filter* —
  K1-only screen (menus_keymap.cpp). STATUS: to mine.
- Chargen feats: kBtnBackId, buttonId table
  (menus_chargen_feats.cpp). STATUS: DONE — tested both games (K1
  2026-08-19, K2 2026-08-20). All three buttons resolve via FeatsPanel*
  (menus_internal.cpp) — announce, Enter, Esc, and the menus_pending
  close probe. Offsets + witnesses in the mined-offsets section. The
  panel's other controls (name label, select button, feats/desc
  listboxes, chart) were already member-based.
- SkillInfoBox: kSkillInfoBoxLbSkillsId/TitleId (menus_listbox.cpp).
  STATUS: to mine.
- Script select (AI state): kScriptSelectLbAiStateId. STATUS: to mine.

Tier 2 — announce-only; keep id, degradation acceptable (revisit only if
tester logs show breakage):
- InGameMenu strip icons (strref table keyed by id).
- K2 saveload detail labels — CONVERTED with the SaveLoad Tier-1 batch
  (the ctor witnesses were free); no longer id-based.
- Journal listbox detection (already pointer/offset-based per
  menus_chain.cpp isJournalItemsLb — verify), credits value labels
  (kCraftPoolValueGuiId etc. — anchored by id but text-only).
- Class-select description label (kClassSelDescLabelId).
- Equip stat rows: already member offsets (kEquipPanel*LabelOffset). DONE
  by construction.
- Charsheet: already member offsets. DONE by construction.
- InGameMessages listbox: already member offset
  (kInGameMessagesMessagesListBoxOffset). DONE by construction.
- InGameMap arrows: already member offsets. DONE by construction.

## Execution order

1. InGameEquip picker trio (proven broken) + slot buttons. Includes
   removing the id-based chain exclusion at menus_chain.cpp:481.
2. WorkbenchUpgrade picker (same class of risk, same code shape).
3. Crafting screens, SaveLoad (its shape-check doubles as panel
   identification — highest silent-failure cost after the pickers).
4. Powers level-up, container, chargen feats, skill info, script select,
   keymap, pazaak surfaces.
5. Tier-2 review pass: add GuiIdMismatch tripwires where cheap.

Every converted surface keeps the id path as fallback + tripwire until a
test round on both games confirms the member path, then the id constant
stays only as the fallback.
