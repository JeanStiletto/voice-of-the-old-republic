# KOTOR 2 port — plan and findings

**Status: ACTIVE (started 2026-07-31).** Supersedes the conclusion of
`kotor2-port-feasibility.md`, whose *measurements* remain valid — the sigscan
result especially — but whose cost estimate predates the RTTI finding below.

**Controller support is a separate, self-contained workstream** — investigated,
planned and fully implemented 2026-08-06 (Phases 0–5), built clean, awaiting its
first live round. It does not block or depend on the batches below. Plan, as-built
notes and the combined test round: `docs/kotor2-controller-plan.md`. Engine
reference: `docs/llm-docs/k2-controller-support.md`. Code: `pad_input.{h,cpp}`,
`pad_quickmenu.{h,cpp}`, `pad_actionmenu.{h,cpp}` plus seam edits listed in the
plan.

## WHERE TO RESUME (read this first)

**Character-creation batch, round 3 — IMPLEMENTED 2026-08-03 (sixteenth
session). Built green, applied to both games, awaiting test.** Round 2's
test (`patch-20260803-102731.log`) confirmed the Attribute and Fähigkeiten
chains clean (9 and 11 entries, every row "Stärke, 8" / "Computerkenntn.,
0", no stray "1", no sibling fallbacks anywhere in 8227 lines) and class
selection walking left to right one icon per press. Three items left:

- **The last "control N" in chargen: `DrainPendingAnnounce` was missing the
  class-icon guard that `AnnounceControl` already had.** An icon's name is
  only readable once the panel's own OnEnterButton has written class_label,
  which happens a beat AFTER the panel-open SetActiveControl that queued the
  announce. KOTOR 1 wins that race (its log speaks "Männlich: Gauner"
  directly); KOTOR 2 loses it every time and spoke "control 11", after
  which the anchored icon's name was never heard until the user navigated
  back onto it. Deferring is not a silenced fallback — the focus monitor
  speaks the name a tick later, since the drain never primes its last-seen
  control. Keyed on the drain's own panel, not `g_currentPanel`.
- **Portrait cycling: the stem's digits are not a position.** K2 adds a
  fourth head family 'h' (P_MAL_H / P_FEM_H — rows 45/60 male, 58/59
  female) which fell through to the raw-resref branch ("Porträt:
  po_pmhh02"), and its variant digits are shuffled relative to row order:
  the five dark-skinned males are rows 28..32 spelling 06, 08, 10, 07, 09,
  so cycling sounded random. Replaced `kPortraitByRow` (which was also
  silently off by one against K1's real 2da — row 24 is po_pmhc2, not
  po_pmhc3; harmless only because the live `GetPortrait` read always won)
  with per-game `PortraitRow` tables of the forpc=1 rows and a
  `PortraitVariantRank` that numbers each portrait within its own (gender,
  family) group. K1's digits already equal its ranks, so its wording is
  unchanged; an unknown row (portrait mods) still falls back to the stem
  digits. Cycle order verified from the log: ids 60, 45, 32 … 18 wrapping,
  exactly the forpc=1 && sex=0 set with the forpc=0 row 22 skipped.
  **Open: `PortraitRaceTypeH` ships as a neutral "Typ H" / "type H"
  placeholder** — a/b/c carry ethnic readings, 'h' has none, and inventing
  one would be a claim we cannot check. Awaiting a name from the user.
- **NEW FEATURE — class descriptions after the class name.** Both games
  number LBL_DESC **id 5** in their own classsel .gui (K1 `classsel.gui`,
  K2 `classsel_p.gui`), so one `kClassSelDescLabelId` serves both and no
  per-game field offset was needed — the by-.gui-id method again. The
  engine fills it on hover in the same OnEnterButton pass that sets
  class_label, so it is captured at that same moment and cached per icon,
  then spoken as a follow-up line by the chain step. Two shapes worth
  keeping: (a) it is stored ALONGSIDE the name, never concatenated — every
  `FromControl` caller passes a 256-byte buffer and the German blurb alone
  runs past 200 chars, so a composed string would have overflowed and
  killed the whole extraction; (b) both games author LBL_DESC with a
  placeholder (empty on K1, the literal "This is just a test line."
  repeated on K2), so `WalkAndCaptureOnFirstSight` snapshots the authored
  text before any hover and `ClassDescIsEngineWritten` compares against it.
  That is what tells content from default without hard-coding either
  placeholder, and it fails closed — no snapshot, no description.
  **Unverified offline: that the engine writes LBL_DESC at all.** The
  placeholder guard means the failure mode is silence, not garbage, and
  the composed string shows in the `ClassSelection cache+speak` log line.
- **The 256-byte announce buffer was hiding a bug, not just clipping.**
  Nine per-kind extraction steps ended in `if (len + 1 <= bufSize)
  memcpy(...)` and simply reported "no text" when it did not fit — so the
  CALLER'S buffer size decided whether a control had a NAME, not merely how
  much of it you heard. Two probe call sites pass 64 bytes and only test
  the return value (`FindAdjacentArrow`, `SquashCycleFlankers`), so any
  control whose label ran past 63 characters read as text-less to them and
  could be squashed out of the chain outright. All nine now go through
  `EmitText`, which truncates — the same failure every snprintf path in the
  ladder already had. The announce buffers themselves moved to a shared
  `acc::menus::detail::kAnnounceTextMax` (1024), including the focus
  monitor's snapshot and `SpeakIfChanged`'s two dedup slots — both of which
  COMPARE against their stored string, so a short snapshot would have
  reported two long lines as identical whenever they shared a prefix. That
  is what let the class description be part of the announce string instead
  of a separate follow-up utterance.

**Character-creation batch, round 2 — IMPLEMENTED 2026-08-03 (fifteenth
session; zero Ghidra, all six defects resolved offline from the round-1 test
log + the two games' own .gui files). Built green, applied to both games,
awaiting test.** The user's report after round 1: Left/Right on an Attribut
or Fähigkeit changes the value silently; a stray "1" entry sits between the
last rows of both Attribute and Fähigkeiten; class-choice and portrait
navigation skip entries, need repeated presses, or announce silence; the
portrait names are mangled. Evidence: `patch-20260803-095222.log` (K2, 3019
lines, no faults) plus `abchrgen_p.gui` / `skchrgen_p.gui` / `custpnl_p.gui`
/ `classsel_p.gui` / `portcust_p.gui` out of each game's gui.bif.

- **ROOT CAUSE FOR THREE OF THE FOUR REPORTS — KOTOR 2 pushes every chargen
  wizard step without firing `CSWGuiPanel::SetActiveControl`.** abchrgen_p,
  skchrgen_p and portcust_p all open straight out of the parent step-list's
  button handler, so `g_currentPanel` stayed latched on custpnl_p for the
  whole visit while the routing layer correctly drove the chain off the
  manager's foreground panel. The log says it plainly: `Routing:
  fg=1D497390 current=1D3E3030 (using fg)` on every tick of the Attribute
  screen, and no `Menus.PanelWalk: panel=1D497390` line anywhere.
  `g_currentPanel` is the only thing three separate consumers keyed on:
  - `WalkAndCaptureOnFirstSight` never ran, so `CaptureLabels` never bound
    `ability_labels[i]` onto `ability_buttons[i]` — the rows spoke a bare
    "8" instead of "Stärke, 8". Fixed by calling the walk from
    `RebindChain` as well; it self-guards on a last-panel latch, so on
    KOTOR 1 (where SetActiveControl already ran) it is an immediate no-op.
  - The focused-control monitor's `g_chainPanel != g_currentPanel` gate
    returned early for the whole visit, which is why twelve `Menus.Cycle`
    dispatches and twelve `FireActivate` calls produced zero
    `AnnounceValueChange` lines — the silent +/- the user reported. The
    gate's existing CSWGuiPortraitCharGen bypass was that same bug, patched
    one vtable at a time; it now asks the manager for its foreground panel,
    which is the invariant the special case was approximating.
  - `TrySiblingLabel` resolved BOTH of its callees against `g_currentPanel`
    instead of the caller-supplied owner. So `IsCycleFlankerArrow` looked
    for a value button on custpnl_p, found none beside a ± stepper, and let
    the sibling-label fallback fire; `FindSiblingLabel` then captioned the
    stepper with whichever of custpnl_p's **LBL_NUM1..6 step numbers**
    lined up vertically. That is the stray "1" — geometry confirms it
    exactly: at 2880x1800 LBL_NUM1 sits at (1526,1107), INT_PLUS at
    (1363,948) and WIS_PLUS at (1363,1071) are the only two steppers inside
    the scaled 50-unit vertical reach whose nearest candidate is the
    numbered label rather than its empty LBL_n twin, and those are the only
    two entries that leaked (chain indices 4 and 6, on both panels).
    `TrySiblingLabel` now takes FromControl's already-resolved `owner`.
- **Class selection: the cursor-warp column shim is a KOTOR 1 fact.**
  K1's classsel.gui hit-test resolves one column RIGHT of each icon's own
  extent — `patch-20260731-140902.log` shows every warp aiming centre+87
  and every one reporting `after == target`. K2 has no such shift, so the
  K1 compensation overshot by exactly one column
  (`MoveMouseToPosition(1257,769) target=<SEL2> after=<SEL3>`), the engine
  bounced focus back to the selected icon, and the per-icon class-label
  cache filled for whichever icon the cursor actually hit — hence skipped
  entries, repeated presses, and silence on the focused one.
  `ComputeClassIconClickOffset` is now K1-only.
- **Same class of shim, same fix, one screen over:** the Attribute/Skills
  row-pitch warp compensation was exiting on K2 only by ACCIDENT, via a
  bare `pitch > 100` reject that K2's stretched 41-unit authored pitch (123
  px at 2880x1800) happened to trip. At a window narrow enough to keep the
  pitch under 100 it would have come back and shifted every row by one.
  `RowPitchForCursorWarp` is now gated on the game, not on a pixel count.
  **This is the third instance of the round-1 lesson: a bare pixel constant
  is a K1 assumption.** Grep for new ones whenever geometry code is added.
- **Chain reading order within a row.** classsel.gui stores BTN_SEL6 before
  BTN_SEL5, and the y-sort's stability then walked the six icons
  1,2,3,4,6,5 (true on both games). `ChainEntry` gained a `geometricOrder`
  flag and the sort a left-to-right secondary key — applied only between
  panel-direct controls, so listbox blocks and virtual rows (whose order is
  the engine's, not the geometry's) are untouched.
- **Portrait names: K2 numbers its variants with a zero-padded TWO-digit
  field and mixes case.** `po_PMHC06` where K1 has `po_pmhc3`. The parser
  read a single char at [7], so every K2 portrait announced as variant
  "0"; an exact-case compare would additionally have dropped the parse for
  the upper-case rows. Now case-insensitive with a multi-digit variant
  read, so id 24 reads "männlich hellhäutig 6". The static
  `kPortraitByRow` fallback table is K1's row order (K2's row 24 is
  po_PMHC06, K1's is po_pmhc3) and went K1-only — on K2 a failed live read
  now falls through to the numeric id rather than naming the wrong
  portrait. No better source exists: neither game's portraits.2da carries
  a display name, and appearance.2da only has model labels
  ("P_MAL_C_MED_02").
- Test items for the next K2 round: Attribute and Fähigkeiten rows speak
  "Stärke, 8" / "Computer benutzen, 0" (name + value, not bare value); no
  "1" entry anywhere in either chain; Left/Right on a row speaks the new
  value plus remaining points; the first row is announced on panel open
  without pressing Up; class selection walks the six icons left to right,
  one press per icon, each speaking its own class; portrait cycling speaks
  "Porträt: männlich hellhäutig 6"-style names. K1 regression: one full
  chargen pass — the class-icon warp shim, the attribute row-pitch shim,
  the chain sort and the sibling-label panel source all changed.

**Character-creation batch — IMPLEMENTED 2026-08-03 (fourteenth session; six
Ghidra rounds + a gui.bif mine, driven by a chargen test round the user ran
end to end). Built green, applied to both games, awaiting test.** The user's
report was: unnamed controls all over Attribute and Fähigkeiten, and the
feats screen's OK button speaking "0 hat die Gruppe verlassen" and doing
nothing. Three unrelated causes, one of them a class of bug we had not hit
before. The witness ledger:

- **The Attribute and Fähigkeiten panels were never ported at all.** Every
  offset in both `CSWGuiAbilitiesCharGen` and `CSWGuiSkillsCharGen` was
  still `Todo(...)`, and all four of their addresses still plain `R()`
  (which is 0 on K2). Both vtables were Pick'd, so `IsPanel` said yes and
  every consumer then read poison: `CaptureLabels` bound nothing, so a row
  spoke its bare value ("8") instead of "Stärke, 8"; the suffix and the
  description read no-op'd; `SyncSelected*` never fired. The descriptions
  the user did hear came from the engine's own hover echo, which our
  listbox silencer also failed to recognise — working by accident.
- **All twelve numbers resolved offline**, each with three independent
  witnesses (ctor tag binding, dtor vector-iterator size+count, and the
  engine's own `panel + base + i*stride` arithmetic inside
  OnEnterPointsButton), plus a fourth from the live log's chain pointers.
  Full reasoning inline at the declarations. Abilities: labels 0xDA0,
  buttons 0x1550, selected 0x487C, LB_DESC 0x70, remaining 0x738.
  Skills: labels 0xEE8, buttons 0x1928, selected 0x4CEC, LB_DESC 0x70,
  remaining 0x738. Control sizes: label 0x148, button 0x1D0. Addresses:
  abilities OnEnterPointsButton 0x00913180 / GetCost 0x00913570, skills
  OnEnterPointsButton 0x0090F610 / IsClassSkill 0x009103A0.
  `kAbilitiesCharGenModifierValueOffset` went Kotor1Only — K2 has no
  LBL_ABILITY_MOD, it shows six per-row LBL_BONUS_* labels instead.
- **The feats panel's three button ids were KOTOR 1 literals.** K2's
  ftchrgen_p.gui renumbers everything and each id lands on a different
  button (K1 9/11/12 = Recommended/Accept/Back, K2 9/10/11 =
  Back/Accept/Recommended). K1's BTN_BACK id 12 is K2's **LB_FEATS, a
  listbox** — read as a button it walked off the CSWGuiButton layout, the
  gui_string read faulted, and the str_ref fallback resolved a garbage
  dword into "<CUSTOM0> hat die Gruppe verlassen." Enter then fired the
  listbox. Now two per-game tables, each in its game's on-screen
  left-to-right order (the two games mirror the row).
- **NEW CLASS OF BUG — KOTOR 2's control extents are in SCREEN pixels.**
  KOTOR 1 ships one .gui variant per resolution and hands us the authored
  extents verbatim (mainmenu8x6.gui's BTN_NEWGAME centre is 485,292 and
  that is exactly what the chain logs). KOTOR 2 stretches a single 800x600
  layout to the window and stores the RESULT — at the user's 2880x1800
  that is 3.6x horizontally. `menus_focus_k2.cpp` already recorded this for
  the cursor warp, but nobody swept the chain code for the consequence:
  **every pixel threshold measured off a KOTOR 1 .gui is wrong on K2, and
  wrong by a factor that changes with the player's resolution.**
  `SquashCycleFlankers`' 80-unit reach is why all twelve chargen +/-
  steppers (27 authored units away, 97 at this resolution) leaked into the
  chain as "control 12" … "control 34". New
  `acc::menus::detail::ScaleGuiThresholdPx` (menus_internal, next to
  `GetControlCenter` whose units it converts) scales at the point of
  comparison from the window's own client width — self-calibrating to any
  resolution, an exact no-op on K1.
  **The sweep found two more sites, both in menus_extract.cpp**, and they
  matter more than the chain one: `IsCycleFlankerArrow`'s 5/80 pair, and
  `FindSiblingLabel`'s 5/50/80 triple — the latter being *the* generic
  "what is this control called" path. An unscaled 50-unit vertical reach
  rejects any caption more than ~14 authored units from its widget on K2,
  so that constant alone was silently turning named controls into
  "control N" across the whole UI, not just chargen. All three sites now
  route through the helper. The options sub-screens' 130-unit spinner
  reach (108 authored → ~389) is fixed by the same change and had not been
  reported yet.
  **Lesson to carry: a bare pixel constant is a K1 assumption.** The three
  known sites are converted; grep for new ones whenever geometry code is
  added, and prefer deriving reaches from the controls' own extents.
- Test items for the next K2 round: Attribute rows speak "Stärke, 8" not
  "8"; "Modifikator -1, Preis 1" follows; description follows; no "control
  N" anywhere on Attribute or Fähigkeiten; Left/Right on a row changes
  THAT row's value (the cursor-warp row-pitch compensation is KOTOR 1
  empirical and unverified on K2 — if + raises the row above, that branch
  is the suspect, and the per-tick `SyncSelected*` should already be
  masking it); Fähigkeiten rows speak name + "Preis 1"/"Preis 2"; feats
  screen's three buttons speak Abbrechen / OK / Empfohlen in that order
  and OK commits. K1 regression: one full chargen pass (Attribute,
  Fähigkeiten, Talente) — the offsets went Pick, the feats tables were
  restructured, and both chain thresholds now route through a helper.

**Equipment-screen batch — IMPLEMENTED 2026-08-03 (thirteenth session; four
Ghidra rounds + a gui.bif mine, driven by the FIRST combined test round).
Built green, applied to both games, awaiting the combined test.** This batch
is different from the twelve before it: it came out of real in-game feedback,
not the batch plan. The witness ledger:

- **The equip picker's Enter did nothing** because BOTH pending ops declined
  — `EquipSelect` (kind 4) and `EquipCommit` (kind 5) — all four
  `CSWGuiInGameEquip` handlers still being KOTOR 1 addresses. The listbox
  populated anyway (vtable-dispatched path), which is exactly why it looked
  like it should have worked.
- **All four handlers resolved.** OnEnterSlot 0x008AC330, OnSelectSlot
  0x008ABE70, OnItemSelected 0x008AF2D0, OnOKPressed 0x008AEC20. Full
  reasoning inline at the declarations in `engine_offsets_addresses.h`.
  The chain that unlocked them: the K2 equip ctor was already known
  (0x008A92D0, recorded in the stat-label note) → its per-slot init loop
  registers OnSelectSlot/OnEnterSlot in KOTOR 1's exact order → its first
  tail registration is OnOKPressed on the equip button → OnOKPressed's
  SECOND reference site is AddItemEntryToList 0x008B01B0 → that registers
  OnItemSelected. Each was then body-verified against KOTOR 1's documented
  shape, and OnEnterSlot's reference set matches KOTOR 1's site-for-site.
- **Method note worth keeping:** none of the four is a virtual and none is
  CALLed by its own class — the Odyssey GUI passes a handler to
  `CSWGuiControl::AddEvent` as an immediate, so its only reference is a
  DATA one. `FindCallers.java` was filtering DATA refs out and reported
  "0 found"; it no longer does. For a callback-registered handler, "who
  references this" is the whole search.
- **The K2-only second weapon set is now named.** BTN_INV_WEAP_L2 / _R2
  (.gui ids 20/21) used to speak as "control 20" / "control 21". Their
  panel item-id offsets came out of the engine's own slot-bit → array-index
  mapper 0x008A91C0, which re-derives all nine KOTOR 1 slots at exactly the
  offsets already recorded — so the two new arms are as trustworthy as the
  table. Both weapon rows now carry a set qualifier taken from the engine's
  own "Konfig 1"/"Konfig 2" strrefs, so it localises for free.
- **BTN_SWAPWEAPONS (id 42) is a real K2 feature**, not noise to filter —
  it swaps the active weapon set. Already reads its own text.
- **Cross-game bug found by this batch:** the disabled-suffix check and the
  chain diagnostic both hardcoded KOTOR 1's `bit_flags` / `is_active`
  offsets instead of the per-game constants. On K2 that read four bytes low,
  so EVERY navigable control on EVERY panel announced "nicht verfügbar",
  and every K2 chain dump printed nonsense flags. Both fixed; swept for
  other literal-offset sites, only those two existed.
- Test items for the combined round are in the reply that closed this
  session; the K1 regression surface is the disabled suffix (shared code)
  and the equip screen generally (its slot table is untouched).
- **CONFIRMED IN GAME:** equipment screen and equipping fully functional.

**Workbench batch — IMPLEMENTED 2026-08-03 (same session, straight after the
equipment one; five Ghidra rounds, zero test rounds). Built green, applied,
UNTESTED — the user knew testing would come later and asked for it anyway.**
The workbench upgrade picker is the same architecture as the equipment picker
and had the same five addresses missing, so the method transferred whole:

- **All five resolved.** OnEnterSlot 0x008CE3F0, OnSlotSelected 0x008CEB00,
  OnUpgradeSelected 0x008CDB00, OnAssemble 0x008CFD10, ShowItems 0x008CB2F0.
  Reasoning inline at the declarations. Entry point was the already-known
  slot-button event-0 handler, whose DATA references named the K2 ctor
  0x008C9E10; from there the REGISTRATION COUNTS identify each handler and
  every count matches KOTOR 1's (2 / 4 / 2 in the ctor, 2 in CreateItemEntry).
- **Bonus:** kUpgradePickerOpenFlagOff is no longer Todo — K2 0x3d28, read
  straight off CloseItems 0x008CB290, whose body is literally the ShowItems(0)
  + clear-bit-0 pair that constant exists for.
- **Two K2 shape differences worth knowing before reading a log:** (1) K2's
  CreateItemEntry registers a DIFFERENT handler for rows it greys out — a
  stub that only pops a "can't select" message — so the shared event codes
  are not sufficient identification there; (2) K2's OnUpgradeSelected calls
  CloseItems instead of inlining the close tail, which is why its ShowItems
  has one fewer distinct caller than KOTOR 1's. Neither is a mismatch.
- **A live bug the addresses would have exposed:** the slot-type table's
  INDEX FORMULA is per-game (K1 biases the slot by four, K2 biases the
  category by one and packs six slots per category instead of four). Both
  call sites carried byte-identical copies of KOTOR 1's arithmetic, so the
  workbench would have named the wrong slot on K2 even with every address
  correct. The header had warned about exactly this; the branch was never
  written. Now one shared `LookupUpgradeSlotType` in the engine layer owns
  the index, the bounds and the SEH-guarded read.
- Lesson worth carrying: **a "callers must branch per game" note in a header
  is not a branch.** Grep for the constants such notes guard and check the
  call sites, the same way raw-literal offsets get swept.

**Footstep-suppress port — IMPLEMENTED 2026-08-02 (eleventh session; one
kotor2 caller sweep + decompile round + byte dump, zero test rounds).
Built green, NOT tested in game.** The witness ledger:

- **The Batch-5 candidate 0x0077D390 is REFUTED** — decompiled, it is the
  class-selection screen's idle-fidget animation driver (rand-picked
  "greeting"/"hturnl"/"hturnr"/"pause*" resrefs; its callers construct
  CSWGuiClassSelection). Its [this+0x68]/+0xc6 "early-out" is that GUI
  panel's model-loaded check, nothing to do with footsteps.
- **The real CSWCCreature::PlayFootstep twin is 0x00765E90**, found by the
  Play3DOneShotSound-facade (0x0070BA90) caller sweep (36 callers; the
  creature-region shortlist decompiled). Landmark-for-landmark match: same
  [this+0x20] early-out (K1's field6_0x20 — offset UNMOVED), FootstepType +
  footstepsounds + surfacemat 2DA reads, rand()%3 variant concat, K1's
  exact priority-group constants 0x13/0x12/0x14, listener-distance gate
  max+2.0, Play3DOneShotSound, footprint-visual twin 0x00772900, water
  DoFootstepAudio twin 0x00773560, rumble 0x00766460. The play-vs-compute
  split that deferred this hook is VERIFIED: one function both computes
  and plays, as on K1.
- **Hook at 0x00765EEA** (cut = CMP [EBP-0x3C],0 + JNZ, 6 bytes,
  skip_original_bytes, consumed_exit 0x00765F10). The branch sense is
  INVERTED vs K1 — the JNZ jumps to the play path and falls through into
  the early-out cascade — so the handler maps the shared verdict per game
  (K2: return 0 = suppress, nonzero = play; K1 unchanged: 1 = suppress).
  EAX carries `this` at the cut. Full forensic note in kotor2.hooks.toml.
- Gates cleared: the HandlerEnabled gate in OnPlayFootstep + the core_tick
  if(k1) around footstep_suppress::Tick. Chain audit clean — the whole
  Tick/stuck-probe closure (player position, area iteration, cached walls,
  combat mode) was already Pick'd by earlier batches.
- Test items for the next K2 round: footsteps audible in normal walking;
  grind into a wall until net progress dies → steps go SILENT while still
  pushing; keep pushing ~5 s → the free-directions probe speaks; in combat
  steps always pass through. K1 regression: the same wall-grind check
  (shared handler restructured — per-game verdict mapping, log field
  verdict= renamed suppress=).

**Static-data batch — IMPLEMENTED 2026-08-02 (twelfth session; pure offline
round — TLK dumps + .gui mining + 2DA classification, zero Ghidra, zero test
rounds). NOT tested in game.** Closes the three K1-data-on-K2 holes the user
flagged (combat anchors / subtitle-suppress lists / decorative ids) plus the
equip-panel id family found on the way. The ledger:

- **Combat anchors (combat_strings.cpp): K2 keeps every combat strref the
  parser uses with byte-identical German text except four** — found by
  running `kdev combat-strings-extract` against both installs' TLKs and
  diffing. K2-DE deltas (BuildDeK2, selected in Get()): phrase_hit gained a
  double space (strref 42043 grew a trailing space), feat_marker became
  " verwendet ." (42046 ditto), prefix_auswirkung gained a trailing space
  (42157 — anchor must carry it or every extracted target name starts with
  a space), ability_use_marker swapped verbs "benutzt" → "verwendet"
  (32292). Auto-hit/-fail tags kept the K1 form deliberately (strstr'd
  substrings — match either way). Non-DE locales stay on K1 anchors (no K2
  TLKs locally; mismatch falls through to raw speech). Glue-order caveat as
  ever: confirm with one K2 combat capture (`MsgBuf: raw:` vs `emit-*`).
- **Subtitle-suppress data (dialog_speech.cpp) went per game.** The
  human-appearance bitmask was REGENERATED from K2's own appearance.2da
  (424 human rows of 671; build/dump-2da now takes a k1|k2 arg — the K1
  path reproduces the shipped mask byte-for-byte). K2 classifier notes:
  Twilek_* (K2 renamed K1's Alien_Twilek_* family — the old prefix rule
  would have MISSED them), Party_NPC_* blanket-human after droid/Wookiee
  exclusions (covers K1 leftovers reused in flashbacks), Darth Nihilus
  excluded (non-language vocalisations — subtitle is the only channel).
  kNeverSuppressTags/kAlwaysSuppressTags are per game, K2 empty (IsKotor2
  short-circuit; all shipped entries were K1 characters). The user's German
  K2 install has German VO (confirmed by the user 2026-08-02 — an earlier
  "K2 was never dubbed" claim in this ledger was WRONG), so the suppress
  semantics carry over from K1 unchanged.
- **The equip/charsheet/partyselect .gui id family went per game**
  (mined from K2's character_p/equip_p/partyselect_p.gui — the install's
  Aspyr override copies are id-identical to gui.bif). K2 RE-NUMBERS
  everything: slot buttons 7..23-odd → 15..25 block with implant at 48,
  LB_ITEMS 5 → 41, BTN_BACK/BTN_EQUIP 36/37 → 39/40, workbench-upgrade
  BTN_BACK 28 → 13. menus_internal.h constants are now runtime-resolved
  Pick-style consts; new shared IsEquipSlotButtonId replaced the two
  9-way OR sites and includes K2's second-weapon-set pair (BTN 20/21 —
  K2-only; their panel item-id offsets are UNMINED, so they navigate +
  activate but announce as "control 20/21" until a K2 ctor round extends
  the kEquipPanel*IdOffset band). TryEquipSlot's label ids are explicit
  per game (K2 broke the K1 label=button+1 rule); slot-name strrefs
  31375-31383 verified present in the K2 TLK.
- **Decorative chain filter (IsDecorativeControl) per-game where id-keyed.**
  CRITICAL fix: K1's charsheet drop-set {1,64,65,66,67} lands on K2's
  BTN_AUTO (65) and BTN_LEVELUP (66) — real actions the old filter would
  have DROPPED from the chain. K2 set is just BTN_3DCHAR (id 5).
  kPartySelectionAddBtnId is K1-only (-1 on K2 — the panel has no single
  add button; every portrait is its own BTN_NPCn). Equip OK/Back filtering
  rides the per-game constants. Offset-keyed entries (equip party-cycle,
  level-up back/cancel, map note-stepper) were already Pick'd or compare
  inert.
- Test items for the next K2 round (fold into the combined round): combat
  lines parse (hit/miss phrasing, feat clause, ability use "verwendet",
  Auswirkung target names have no leading space); a human NPC's subtitle
  suppression classifies right + an alien (e.g. any Twi'lek thug) still
  speaks; equip screen: all 9 slot buttons announce slot name + item, the
  two weapon-set-2 buttons populate the picker on Enter (they will speak
  as "control 20/21" — known gap), OK/Schliessen stay out of the chain,
  charsheet chain includes BTN_AUTO/BTN_LEVELUP but not the model rotator,
  party selection portraits navigate. K1 regression: one equip pass (slot
  announce + picker + chain), one charsheet chain walk, one combat
  capture, one human/alien subtitle check (shared code: constants went
  runtime-const, slot predicate deduped, extract table restructured).**

**State as of 2026-08-02 (tenth session, remaining-surface sweep): SIX
batches implemented and committed in one sitting, all offline, zero test
rounds — level-up (3a4d812), peek (a05024a), item-description cluster
(64b1ea8), map cursor + user markers (ddd7235), chargen feat grids
(008f6ce), plus the K2 systems pass from the ninth session (4d0d045 +
9137254) — ALL UNTESTED IN GAME. The entire non-minigame surface is now
ungated on KOTOR 2. THE BIG COMBINED TEST ROUND IS THE NEXT GATE — its
per-batch checklists sit in each batch ledger below; run it BEFORE starting
the minigame investigations (pazaak/swoop are fresh-RE-scale, not
twin-hunting, and stacking them on an unvalidated base compounds risk).
A K1 regression pass is equally due: Dispatch lost five more if(k1) blocks,
GetClientArea gained a branch, the workbench detector/slot-id logic went
per-game, and ComputeSectionOffsets was restructured.**

**State as of 2026-08-02 (ninth session, systems pass): Batches 3d+4+5 are
TEST-CONFIRMED in game** (tutorial played through; K1 regression pass also
clean). Two bugs found and fixed on the way: the Batch-5 consume-exit crash
(both audio hooks pointed consumed_exit_address at the bare RET past the
epilogue while their cut executed the prologue — exits moved onto the
`MOV ESP,EBP; POP EBP; RET` epilogues; commit b558b5b) — the lesson is now
also inline in kotor2.hooks.toml.

**The systems pass then ungated, with offline witnesses (commits 4d0d045 +
9137254, NOT yet tested in game):** view_mode + poll, discovery cycling
(GetModuleResourceName twin 0x00561030 — area keys were collapsing to
"untitled"), locked_recall, party_leader_announce, camera_orient +
camera_spin_guard (edge-band constants witnessed; width moved to
[internal+0x274] on K2), trap_watch (detected-by lists in the K2
UpdateMineCheck twin 0x0056C310), stealth_watch (creature+0x511),
MaybeDrivePassiveSelection (whole MainLoop-gate chain witnessed — Peragus
scripted holds need it like the Endar Spire), probe_audio_frame, abilities
screen (stale Batch-1 gate), galaxy map (both labels from the K2 ctor),
keymap (K2 RESTRUCTURED: single SetFilter 0x00900E30 + index at +0x1364,
capture via HandleInputEvent Enter case, flag +0x1368 — the per-game
branches live in menus_keymap.cpp). K2 Override now carries the
prioritygroups sentinel rows (K2 SCHEMA DIFFERS: volume_pc/xbox split —
tools/re-scripts/append_accgroup_2da.py, byte-verified against K1) plus the
four swoop cue wavs. The installer still needs the K2 row set.

**USER DECISION (2026-08-02): the turret minigame does NOT exist in KOTOR 2**
— OnTurretBulletHit/OnPlayerFire and turret_game stay K1-only permanently
(mark constants Kotor1Only when touched). Port the rest of the minigames.

**Level-up batch — IMPLEMENTED 2026-08-02 (tenth session, one offline round:
two kotor2 Ghidra rounds + one kotor1 round + capstone scans, zero test
rounds). Worklist 18/18, chain audit clean, built green, NOT tested in
game.** The witness ledger:

- **CGuiInGame::ShowLevelUpGUI → 0x007CB7C0** and
  **CSWGuiInGameCharacter::ShowLevelUpGUI → 0x0084FCD0**, both
  decompile-matched landmark for landmark (initialized +0x128, the
  GetCharacterChangeInProgress twin 0x00740FC0, two-arg SetSoundMode, lazy
  charsheet build new(0x4b7c)+0x0084C3A0 into [gui+0x14], SetStats
  0x0084E6F0 — the Batch-3d twin, mutually confirming — then new(0x2cb8) +
  CSWGuiLevelUpCharGen ctor 0x008F4AF0). On BOTH games the wizard alloc is
  CSWGuiLevelUpCharGen (today's K1 decompile corrected the old
  "CSWGuiLevelUpPanel" comment — that is the embedded sub-panel).
- **level_up_mode moved +0x10C → +0x12C** (the +0x20 ring shift),
  double-witnessed by both twins' gates; **SetLevelUpMode → 0x007BFA20**
  (byte-identical compare-then-store on +0x12C, found by displacement scan).
- **CSWSCreatureStats::CanLevelUp → 0x006B9790**, disasm-matched line for
  line (ServerInfo max_level_ +0x94, Rules exp table internal+0x38,
  experience +0x68, stats->creature +0x24, shape-equivalent dead-check
  tail); reached from K2 ShowSWInGameGui 0x007C9DF0's default-panel branch,
  K1's exact caller. Still a pure read-only predicate.
- **CSWGuiPowersLevelUp**: ctor 0x009074E0 / dtor 0x00908070 via RTTI
  vtable 0x009AA34C; **chart at +0x1BF8** (`add ecx,0x1bf8` before the
  chart ctor 0x0089A650; tail member — chart+0x14 == the new-size 0x1c0c
  allocated by the LevelUpPanel powers-button callback 0x00904420).
  **OnEnterPower → 0x00908F70**, **OnPowerPicked → 0x00908E30**, both
  decompile-confirmed (OnPowerPicked keeps K1's message strrefs
  0xa5e6/0xa621/0xa4c9/0xa4ca verbatim and routes the box through the
  moved MessageBox slot +0xA0; reached from the ctor-registered
  selection-changed/double-click callbacks 0x00909D00/0x00909D70 — K1's
  OnPowerSelectionChanged/OnDoubleClick positions).
- **The pwrlvlup control ids are RE-NUMBERED between the games and
  COLLIDE** (K1 id 6 = LB_POWERS vs K2 id 6 = a label; K1 id 12 = BTN_BACK
  vs K2 id 12 = LB_POWERS) — the "both games assign the same ids" rule from
  Batch 3d does NOT hold for this panel. menus_powers_levelup.cpp now
  resolves ids per game (K2 set mined from its own gui.bif, pwrlvlup_p.gui).
- Gates cleared: the reader's Batch-1 decline + the levelup_pause Dispatch
  phase. Shift+L was already reachable on both games; it now works instead
  of declining through the poisoned addresses.
- Test items for the next K2 round: earn a level (Peragus start has one
  pending), Shift+L → wizard opens + world freezes, wizard sub-screens
  navigate/speak, powers picker: rows/cells announce with status word +
  description, Enter picks (both the pick and each refusal message), Esc =
  BACK_BTN, Accept closes + pause releases (levelup_pause channel). K1
  regression: one Shift+L level-up pass (shared reader restructured:
  per-game id functions replaced the constexpr ids).

**Peek batch — IMPLEMENTED 2026-08-02 (tenth session, after the level-up
batch; two Ghidra rounds + capstone/registration scans, zero test rounds).
Worklist 36/36, built green, NOT tested in game.** The witness ledger:

- **The OnControlEntered family is registered per control with EVENT ID 0**
  through the same registrar the button callbacks use (0x00418AF0) — that
  registration scan is what found every twin: inventory **0x008A8100**
  (registered by its CreateItemEntry 0x008A75F0; body has K1's literal
  "Error: Invalid item"), store **0x008B6E90** (registered by 0x008B5DB0;
  K1's exact 0xa3df unlimited-stock strref), upgrade **split in two on K2** —
  the ctor registers 0x008CE3F0 on the slot-button banks while CreateItemEntry
  0x008CE930 registers **0x008CCB80** on the picker rows (K1's exact 0x7dac
  empty-description fallback; the saber keyed-bonus concat is ABSENT from the
  K2 row handler — listen for missing bonus lines in the test round). Our
  constant points at the row handler.
- **kUpgradeDescLabelOffset → Pick(0x1f60, 0x2ee0)** from the K2
  SetDescription twin 0x008CCD70 (SetText on label+0xf0, extent at +0x2ee4,
  GetFontHeight text object +0x2fb8, SetExtent vtable at [this+0x2ee0]).
- **Item-entry row id +0x1c4 → +0x1d0** (the embedded button grew 0x1c4 →
  0x1d0): witnessed by the K2 entry ctor 0x008B0A60 initialising
  [entry+0x1d0] = 0x7f000000 and by all three twins reading [row+0x1d0].
  kStoreItemEntryObjIdOffset went Pick with the same values.
- **Raw K1 literals converted to Pick with ctor witnesses** (the
  GetInputClass lesson — worklist can't see raw literals): inventory
  description_listbox 0x844→0x878, container items_listbox 0x7f0→0x824,
  equip items_listbox 0x30d8→0x372c (each from its own K2 panel ctor,
  cross-checked against Lane's K1 DB member maps; the equip ctor scan
  independently reproduced the witnessed 0x3edc back-button offset).
- **K2 re-numbers upgrade_p.gui and BTN_BACK lands at id 13 — inside K1's
  12..18 slot-button range.** New shared per-game predicate
  `IsWorkbenchUpgradeSlotButtonId` (engine_panels) replaced the range test
  at all three sites (peek, chain nav, chain input; the chain site's raw
  0x44 bit_flags read now uses kControlBitFlagsOffset). K2 slot buttons:
  7/8/6 normal + 17/18/19/23/24/25 saber-bank.
- **IsWorkbenchUpgradeStructural never identified the panel on K2** (its
  id-15 probe hits a label there); probes went per game (K2: BTN_ASSEMBLE
  id 11, BTN_UPGRADE31 id 7). WorkbenchItems/Select detectors survive
  unchanged (ids match / vtable Pick).
- Deliberate degrades left in place, all behind existing decline gates:
  the item-description builder cluster (kAddrItemAdd*,
  GetPropertyDescription, ClientToServerObjectId, GetItemByGameObjectID,
  kSwsItem* fields) stays unresolved — on K2 SpeakItemBlocks declines and
  peek falls through to the engine-rendered description-listbox path, which
  the ported twins feed; menus_pending's workbench/equip engine ops decline
  via EngineOpsReady. **That builder cluster is the natural next batch** —
  it also unlocks examine-view extras and the equip picker commit.
- Test items for the next K2 round: Shift+Up/Down on inventory rows, a
  store (buy + sell lists), journal entries, abilities rows, equip slot
  buttons + a container; workbench: category select → item pick → slot
  buttons announce installed mods, slot picker rows read descriptions.
  K1 regression: same list (shared code touched: the slot-button
  predicate, the upgrade structural detector, chain bit_flags read).

**Item-description cluster — IMPLEMENTED 2026-08-02 (tenth session, after
the peek batch; two Ghidra rounds + GFF string-xref scans + a strref
fingerprint pass, zero test rounds). NOT tested in game.** The ledger:

- **CSWSItem::GetPropertyDescription → 0x00607790**, decompile-matched
  (GetBaseItem guards, builder cascade, description_indentified GetString
  tail with K1's 0x7dac fallback; called by all three K2 OnControlEntered
  twins). The server facades went Pick too: ClientToServerObjectId →
  0x0051C8B0, GetItemByGameObjectID → 0x0051C0E0 (thin forwarders, K1's
  shape, consumer-witnessed three ways).
- **All nine ItemAdd* builders paired by strref-immediate fingerprints** —
  each K1 builder's pushed GUI-strref set matched exactly one K2 candidate
  (Damage 4/4, Defence 3/3, Misc 7/7, OnHit 4/5, singles 1/1). **K2
  RESTRUCTURED the cascade**: a NEW builder runs before FeatRequirements,
  four more after it, and the weapon guard became
  `weapon_type != 0 || base_id == 0x2d` with the crit builder SKIPPED for
  lightsabers (base 0x2d). ComputeSectionOffsets now replicates each
  game's own sequence (five new K2-only builder addresses banked via the
  new `acc::addr::Kotor2Only` selector); a wrong replica degrades to
  description-only via the existing divergence guard. K2 also added item
  type 0x31 to the skip-all guard (replicated per game).
- **Item fields all witnessed individually** (the K2 item GFF loader
  0x00601740 / saver 0x00602DD0 + the OnControlEntered twins): the whole
  band is K1+0x40 — charges 0x298, max_charges 0x29c, DescIdentified
  0x2b0, Description 0x2b8, LocalizedName 0x2c0 (already Pick'd),
  bit_flags 0x2c8, stack_size 0x2cc. Infinite-stock bit Same (bit 2).
  CExoLocString strref Same(+4) (K2 GetString twin 0x007356B0).
  CSWBaseItem item_type/weapon_type Same(0xac/0x09) (witnessed in
  0x00607790's guards). New kSwsItemBaseItemIdOffset Same(0xc).
- **Force-points block resolved too** (charsheet SetStats twin 0x0084E6F0):
  stats root clientCreature+0x310 (double-witnessed with Batch 3d's HP
  chain), max FP short +0x126, current FP = (short)(+0x12a + +0x12c) — the
  FP band shifted +8 while HP (+0x4c) did not; bands move independently.
  This makes examine-view FP live on K2.
- GetKeyedPropertyString stays R() DELIBERATELY: its wrapper has no
  callers and the K2 row handler dropped the keyed-bonus concat.
- What this unlocks on K2: Shift+Up/Down block navigation (tags / values /
  properties / description), container + equip-picker + workbench item
  peeks, store stock counts, examine-view item lines.
- Test items: fold into the peek-batch list above — additionally check the
  block navigation reads all four blocks on a weapon, a stim and a saber
  (the saber exercises the K2-only forced weapon block), and that the log
  shows no "offsets diverged" lines (a diverged replica speaks the whole
  description as one block — functional but un-sliced).

**Map batch (map_ui_cursor + map_user_markers) — IMPLEMENTED 2026-08-02
(tenth session; one Ghidra round + capstone scans, zero test rounds).
Worklist 39/42 — the three survivors are deliberate. NOT tested in game.**
The ledger:

- **CSWSAreaMap transform block is Same** (already witnessed by the K2
  SetMapData 0x005F6B90 note): NorthAxis +0x10, world-per-px +0x18/+0x1c,
  origin +0x20/+0x24. **IsWorldPointExplored → 0x005F73F0** (the bit-test
  body), **GetMapRotateCCW → 0x005F7170** (atan2 + NorthAxis switch,
  called from the K2 SetPartyMemberWorldOrientation twin at K1's exact
  position; same by-value-Vector/ST(0) contract). NOTE: K2's map-pixel
  space is 588x294 (K1 440x256) — engine-internal, our code never bakes it.
- **Map panel: hider embed 0xE38 → 0x1160** (map ctor 0x00893950;
  its BTN_UP/BTN_DOWN wiring reproduces the witnessed +0x610/+0x7e0).
  **Hider waypoint list +0x238 → +0x248** (the K2 GetNextMapNote twin
  0x00896D10 walks the CExoLinkedList at [hider+0x248] with the same
  0x7f000000 sentinel; GetNext/GetPrev = the equal-size adjacent pair
  0x00896D10/0x00896FB0).
- **The whole pin-creation chain witnessed in K2's own script pin-creator
  0x0082D670**: operator new → 0x00919723, CSWCMapPin ctor → 0x00893460
  (pin size 0x110 — SAME as K1), note assign CExoString::operator=(char*)
  → 0x007338D0, **AddMapPin → 0x007A9640** (appends via the shared
  array-append 0x0083EA60 to the client-area pin array at +0x1c8 — the
  triple shifted +4: ptr/count/cap 0x1c8/0x1cc/0x1d0). **CSWCMapPin layout
  is IDENTICAL** (position +0x24 via the SetPosition virtual 0x007EEF90,
  enabled +0xfc, note +0x100, flags +0x108, subtype +0x10c).
- **GetClientArea routes around the unwitnessed K2 back-pointer**: on K2 it
  resolves client-module → [module+0x48] (the exact route 0x0082D670
  uses; new Kotor2Only offset — off:: gained the selector to match
  addr::'s). kAreaClientAreaBackOffset stays Todo, K1-only in practice.
- The ReadGlobalNumber globals pair stays R() deliberately: its only
  consumer is endar_softlock, K1-only by design.
- Gates cleared: map_ui_cursor + map_user_markers Dispatch phases (no
  per-file declines existed).
- Test items: open the map on K2 — cursor arrows announce position/room,
  cycling map notes speaks them (waypoint-list walk), fog reads
  unexplored/explored correctly (IsWorldPointExplored), rotation-correct
  direction words on a rotated map (GetMapRotateCCW), Shift+N drops a
  marker + it appears in map-note cycling and the narrated-target slot,
  markers reset on area change. K1 regression: same list (GetClientArea
  gained a branch, two Dispatch phases moved out of if(k1)).

**Chargen feats/grids batch — IMPLEMENTED 2026-08-02 (tenth session; one
Ghidra round + capstone, zero test rounds). Worklist 30/30, built green,
NOT tested in game.** The ledger:

- **CSWGuiFeatsCharGen K2**: ctor 0x00909E00 / dtor 0x0090A9A0 (RTTI
  vtable 0x009AA434, already Pick'd). Ctor tag-wiring: LBL_NAME +0xab0,
  BTN_ACCEPT +0xbf8, BTN_BACK +0xdc8, BTN_RECOMMENDED +0xf98, BTN_SELECT
  +0x1168, LB_FEATS +0x15c8, LB_DESC +0x18b8 — the panel RE-ORDERS members
  vs K1 (buttons before listboxes), so nothing follows a delta rule.
  **Chart at +0x1bf4** (the second class the level-up round's
  SetSelectedSkill census flagged — mystery closed). The four
  feat lists sit at +0x1ba8..+0x1bcc (12-byte stride), each list's
  IDENTITY pinned by the K2 BuildButtons twin 0x0090BD50 painting K1's
  exact status codes (1=existing, 2=granted, 0=available, 4=chosen).
- **OnEnterFeat → 0x0090B9B0** (name strref [feat+0x8] onto the name
  label, desc strref [feat+0xc] into SetDescription 0x0090C390);
  **OnFeatPicked → 0x0090B890** (DetermineFeat 0x0090BCB0, add/remove +
  repaint, K1's exact strref cluster 0xa622/0xa4c6-c8), reached from the
  ctor-registered selection-changed/double-click callbacks
  0x0090C530/0x0090C5A0 — the PowersLevelUp pattern verbatim.
- **Rules feat table from the banked GetFeat twin 0x006A20F0**: array
  POINTER [rules+0x108], count word [rules+0x11c], stride 0x50 (K1
  0x90/0xa4/0x48); name strref Same(+0x8). Same *kAddrRulesGlobal base.
- Gates cleared: the chargen-feats reader decline + the diagnostic dump
  decline (its rules-table walk is now resolved).
- Test items: chargen step 5 (Talente) — 2D grid navigation with
  name/status/description per cell, Enter picks/unpicks (+ each refusal
  message), the granted-feats overlay, recommended/accept/back buttons;
  same again on the LEVEL-UP wizard's feat step (same panel class). K1
  regression: chargen feats + level-up feats pass.

**STILL K1-GATED (the remaining port surface, in suggested order):**
pazaak + pazaakdeck, swoop race/audio, OnRulesInit/mouse-guard decision,
camera probes (deliberately deferred dev diagnostics). endar_softlock and
floor_puzzle stay K1 by design (K1-module-specific content workarounds).
footstep_suppress was ported 2026-08-02 — see its ledger at the top.

**State as of 2026-08-02 (eighth session): Batches 3d, 4 and 5 are ALL
IMPLEMENTED in one sitting (user decision: everything except Batch 6 at
once, accepting a bigger combined test round). `k2_hook_status.py` reports
21 of 25 READY** — the four not-READY are the three Batch 6 hooks
(RulesInit / TurretBulletHit / PlayerFire) and OnPlayFootstep, deferred
with a documented candidate (see Batch 5). Built clean, committed, NOT
tested in game. All three address rounds ran offline (three kotor2 Ghidra
rounds, one kotor1 round, heavy capstone work; zero test rounds spent).

**THE COMBINED TEST ROUND THIS NEEDS (KOTOR 2):**

1. `kdev apply --game k2`, launch, load into the world.
2. **Sub-screens read (3d):** character sheet — every stat row (XP,
   HP, FP on a Jedi, six attributes with modifiers, alignment slider;
   class/level rows are expected ABSENT on K2 — the panel has no such
   labels). Equipment — hover a slot: defense + attack/to-hit rows
   (the HP row is expected absent); swap weapon-set and note whether
   values still read (we anchor set 1 — log finding if silent).
   Inventory — rows read, filter buttons (7 on K2) repopulate + chain
   rebinds. Journal — entries read, Enter speaks description, the three
   sort buttons re-sort + list re-reads, swap-text works. Abilities —
   tab cycling (Skills/Powers/Feats), skill rows with rank/bonus/total,
   feat/power chart rows speak name+description (chart internals were
   re-derived: columns SHRANK to 0xb4 on K2).
3. **Editbox (3d):** chargen name field — typing narrates, random/accept
   buttons work; save-game name popup likewise.
4. **Messages screen (3d):** open it — K2 has separate Dialog/Feedback/
   Combat/Effects listboxes; the feedback + dialog boxes are mapped, the
   two new ones ride the generic reader.
5. **Combat (4):** fight something. Expect: combat-mode entry/exit
   announces, combat-log lines spoken (MsgBuf was live since Batch 2 —
   now the round diagnostics attribute them), queue announce "X, Platz N"
   on chained bare-key actions in combat, queue-full line at 4, examine
   view (Ö) shows HP/wound state (effect ICONS are expected missing —
   deliberately unresolved). Watch `Combat.Diag` ADD/CLEAR/SETCUR/REMLAST
   lines for the four hooks' first fires.
6. **Pause (4):** Space pause/unpause speaks (`Pause` channel shows the
   shadow transitions); Esc-menu open/close does NOT double-speak; popup
   close unpauses (the engine.inputReassert path now runs on K2).
7. **Audio cues (5):** the wall/proximity cue set should now PLAY (was
   open defect 3 — `drop-engine-fail` should disappear). Volume slider
   in mod settings still routes cues; cue pitch is stable across fires
   (the jitter neutraliser consumes the K2 twin). View-mode cursor
   listener override works (ListenerHook channel).
8. **KOTOR 1 regression pass:** menus + charsheet/equip/inventory/journal/
   abilities read; one fight with queue announces; pause; cues + volume
   slider; editbox typing. Shared code touched: monitors guard sweep,
   spec-table constexpr→const conversions, Dispatch restructured a FIFTH
   time (combat/pause/audio phases ungated), SetSoundMode/SetPauseState
   call sites went per-game, FindControlById gained SEH.

~~Batch-5 leftover, deliberate: **OnPlayFootstep has no K2 hook.**~~
**DISCHARGED 2026-08-02 (eleventh session):** the scouted candidate
0x0077D390 was decompiled and REFUTED (it is the class-selection idle-anim
driver — its "6 rand calls building the footstep resref" pick idle
animations, and its "early-out" is a GUI model check). The real twin is
0x00765E90; see the footstep ledger at the top of WHERE TO RESUME.

**State as of 2026-08-01 (sixth session): Batch 3 is IMPLEMENTED — address
round complete, hooks written, gates cleared, `k2_hook_status.py` reports
12 of 25 READY. Built clean, NOT tested in game, NOT committed.** The whole
address round ran offline (two parallel Ghidra rounds + capstone scans, zero
test rounds spent). See "Batch 3" under THE BATCH PLAN for everything that
resolved this session and the test round it will eventually need.

**USER DECISION (2026-08-01): the Batch 3 test round is DEFERRED.** Too much
of the in-game loop is still silent for testing to be meaningful, so two new
batches were scoped and come first: **Batch 3b — Dialog** and **Batch 3c —
Interaction** (walk-to-target, Enter-interact, action surfaces; 37 unresolved
constants, several sessions). The risk this accepts: more untested code
stacks up before the first combined test round — mitigated by keeping every
batch's offline verification at the Batch 3 bar (worklist to zero on live
paths, chain audit clean, byte-confirmed cuts). ~~Do not let a KOTOR 1
regression run slip much further — Dispatch has now been restructured THREE
times without one.~~ **DISCHARGED 2026-08-01: the user ran the KOTOR 1
regression pass over the Batch 2/3/3b changes and found no regressions.**

**Batch 3b — Dialog is IMPLEMENTED (2026-08-01, same session): 12/12
constants resolved, slot rows closed, dialog_speech phase live on both
games, built clean. Tested via the K1 regression pass; committed with
Batch 3.** See its section under THE BATCH PLAN for every witness.

**FIRST KOTOR 2 IN-WORLD TEST ROUND RAN 2026-08-01 (log
patch-20260801-225529.log, 5950 lines, ZERO faults / zero crashes).** What
worked: chargen → world, sub-screen lifecycle, dialog speech (the Ebon Hawk
prologue line), camera-turn direction readout, category cycling
(BuildListing scanned all 177 area objects and answered correctly), the
whole wall/room data pipeline (159 edges, 11 named doors, calibrated change
detector), and both new input hooks (103 events logged, including the
in-world bare keys). Three defects found, two fixed in the same session:

1. **FIXED — every hovered/cycled object failed to narrate.** Both the
   passive narration and the Q/E re-announce logged "handle ... failed to
   resolve, silent". Cause: `kClientObjectServerObjectOffset` was left
   `Todo` on the theory that KOTOR 2 consumers all branch to the engine
   resolver — but `ResolveClientObjectHandle` reads the field directly, so
   it read through the poison offset and returned null. KOTOR 2's own
   `CSWCObject::GetServerObject` (0x007F2540) tests and fills
   `[this+0xf8]`, the SAME offset as KOTOR 1's, so the constant is now
   `Same(0xf8)`. **Lesson: "designed Todo" is only true per CONSUMER —
   audit every reader before declaring an offset deliberately unresolved.**
2. **FIXED — sub-screens announced right but read wrong.** KOTOR 2's
   dialog.tlk assigns KOTOR 1's menu strrefs to unrelated strings (48218 =
   "Ablativplatten Mk 1", 48223 = a spacesuit hint), which is what the icon
   reader spoke. K2's cluster is 48620..48628, mined by parsing its TLK
   header. Both tables (menus_extract.cpp icons, menus_monitors.cpp
   sub-screen titles) now carry strrefK1/strrefK2. Two K2 captions differ
   in wording ("Charaktere", "Tagebuch") and K2's messages entry (48624) is
   empty, so that row uses the standalone 1563. The icon table is now keyed
   by the control's own **.gui ID** instead of its position in controls[]:
   both games assign the same ids (0..7 LBLH_*, 8..15 BTN_*, verified by
   gff2xml over each game's own gui.bif) while the two engines build
   controls[] in different member orders, so id-keying is correct on both
   and the array order stops mattering.
3. **OPEN — no wall/proximity cues.** The cue data is all there; every cue
   drops with "drop-engine-fail" because cue playback is Batch 5 (Audio),
   not ported yet. Expected, not a defect. Room-shape speech needs one
   look on top of that once cues exist.

**SECOND KOTOR 2 ROUND (log patch-20260801-232432.log): room-shape speech,
Q/E, and interaction all confirmed WORKING.** Two defects found, both fixed:

4. **FIXED — crash when cycling sub-screens quickly.** The process died in
   OUR dll, not the engine: dump CrashDumps/swkotor2.exe.46564.dmp faults
   reading `[esi+0xc]` at accessibility.dll+0x18FFFA, which disassembles to
   `GetControlCenter`'s `ext[2]` (menus_internal.cpp). It null-checked the
   control but read it without SEH; a fast screen switch freed the panel
   while the chain still held one of its controls, so the pointer was
   stale-but-non-null. Now SEH-guarded. **This is the fourth crash of the
   identical class** (FocusProbe, TryPartyPortrait, TrySpeculativeVtableRead,
   now this): a non-null control pointer is NOT proof of life, and KOTOR 2
   tears panels down on paths KOTOR 1 does not. Worth a sweep for any
   remaining unguarded control reads rather than waiting for the fifth.
5. **FIXED — the character sheet was the one sub-screen that never
   announced.** Its CGuiInGame slot row was a `Todo`, on a note claiming
   KOTOR 2 puts a CSWGui3DSceneView at 0x14. That note was wrong (same
   slot-table-tool misattribution as the BlackenedLabel case): the K2 panel
   creator 0x007BE4C0 builds the character panel at 0x0084C3A0 — the
   function that stores the CSWGuiInGameCharacter vtable 0x009A3E7C — and
   writes it with `MOV [gui+0x14],EAX`. The row is now `Same(0x14)`.

Slot rows still unresolved after this: PartySelection (0x78),
InGameGalaxyMap (0x80), ControllerLossBox (0xa4), DialogMessagesAux (0xf8),
DialogMessages (0xfc). None of them blocked anything in the two rounds so
far; resolve them with the per-sub-screen batch below.

**Q/E on KOTOR 2 — answered:** next/prev target ARE natively bound to E/Q,
identically to KOTOR 1. The ini keymaps are byte-identical for these
(`Action204=67`, `Action205=55`), the engine's dispatcher has the same
0xcc/0xcd cases, and the test log shows both codes arriving at our hook
in-world. The silence was defect 1, not a binding difference. (A first pass
over the log wrongly reported the codes as absent — the search pattern
skipped the `key=?(204)` form the unnamed codes print.)

**Batch 3c — Interaction is IMPLEMENTED (2026-08-01, seventh session):
the full worklist went 71 unresolved → 1 (the one survivor is the DESIGNED
kClientObjectServerObjectOffset Todo whose consumers branch to the engine
resolver on KOTOR 2), both in-world input hooks are written
(OnClientHandleInputEventK2 + OnProcessInput on the K2 dispatcher/frame
tick), the Dispatch interaction phases and cycle_input's Batch-1 decline
are cleared, chain audit is clean, and the build is green.
`k2_hook_status.py` reports 14 of 25 READY. NOT tested in game, NOT
committed.** The whole address round again ran offline (eight K2 Ghidra
rounds + capstone scans, zero test rounds spent). See "Batch 3c" under THE
BATCH PLAN for the witness ledger, the three calling-convention divergences,
and the combined test round this now needs. Next session: run the combined
Batch 3+3b+3c KOTOR 2 test round (chargen → world → sub-screens → dialog →
walk/interact/cycle), or scope Batch 4 — Combat first if the user prefers
more offline porting before spending a test round.

Batch 1 (GUI spine) is TESTED AND WORKING — the user confirmed menu navigation,
Options + sub-panels, listbox rows and speech on KOTOR 2. Batch 2 (in-game GUI
lifecycle) is the four hook targets with byte-confirmed cut points plus
Show/Prev/SetInputClass (see "The four hook addresses — ALL IDENTIFIED" under
Batch 2).

**Batch 2 is IMPLEMENTED (2026-08-01, same session) — built clean, NOT yet
tested in game, NOT committed.** `k2_hook_status.py` now reports **9 of 25
READY** (GUI spine + the four in-game-GUI-lifecycle handlers). What landed:

- `kotor2.hooks.toml`: five new entries — Switch @0x007CA575, Hide
  @0x007CA066, SetSWGuiStatus @0x007C9C46, and BOTH AppendToMsgBuffer rings
  @0x007BE093 / @0x007BE1B3 sharing one wrapper. All cuts byte-confirmed.
- K2 wrappers: `OnSwitchToSWInGameGuiK2` / `OnHideSWInGameGuiK2` /
  `OnSetSWGuiStatusK2` at the bottom of `engine_subscreen.cpp`,
  `OnAppendToMsgBufferK2` at the bottom of `msg_router.cpp`; exported. The
  address-style handlers keep their caller_eip trick because EBP+8 IS the
  esp+4-LEA address and [EBP+4] the return address.
- Constants: PrevSWInGameGui, HideSWInGameGui, SetInputClass (facade
  0x0073FEE0), SetSWGuiStatus, GetPlayerCreature (facade 0x0073F450),
  GetServerCreature (Pick 0x0060FB20 / 0x0077D800), GetLoadFromSaveGame
  (Pick 0x004af050 / 0x0051CDE0, via facade-cluster alignment), input_class
  +0x9C now a named Same constant (`kClientInternalInputClassOffset`,
  engine_app.h), slot rows InGameMenu Same(0x8) and MainInterface
  Pick(0x90, 0x98) (+ the canonical `kGuiInGameMainInterfaceOff`).
- `GetPlayerServerObject` gained a K2 branch calling the engine's own
  `CSWCCreature::GetServerCreature` instead of the K1 field read (+0xf8 is
  unestablished on K2 and the resolver is layout-proof). This makes
  `GetPlayerPosition` REAL on KOTOR 2 — without it the msg handler's replay
  gate would have silently suppressed every feedback line.
- Gates cleared: the three in `engine_subscreen.cpp` + msg_router's. NOT
  OnSetPauseState (Batch 4). `handler_chain_audit.py` over the whole chain
  set: 1 flagged line, in K1-gated `TickCombatLog` — unreachable on K2.
- Deliberately deferred: kAddrSetPauseState / kAddrSetSoundMode /
  kAddrExoSoundPtr stay unresolved. Their only consumers
  (TickInputClassReassert → DispatchUnpauseCleanup, tutorial_popup) are
  K1-gated ticks. CAUTION for whoever resolves SetSoundMode: KOTOR 2's
  (0x0070BC60, ExoSound global 0xA1B494) takes TWO args where KOTOR 1's
  takes one — banking the address without adapting the call corrupts the
  stack.

**Crash found by the first Batch 2 test attempt (2026-08-01, FIXED, needs
retest):** opening the chargen NAME field on KOTOR 2 crashed the process —
WER: c0000005 in accessibility.dll @0x198511. The faulting line was
`TryPartyPortrait`'s vtable read, which ran BEFORE its own `__try`: the
panel-walk's control array held a non-null garbage entry after the name
panel's last real control, every OTHER extractor in the ladder faulted
quietly inside its own guard, and this one unguarded head killed the
process. `TrySpeculativeVtableRead` had the identical unguarded head; both
now read the vtable under SEH and skip the control. Same crash class as
Batch 1's FocusProbe lesson — and NOT an input-field/editbox gap: the
editbox handler correctly declines on KOTOR 2 (typing is not narrated yet;
that surface comes with its own batch). The K1 crash-history dumps show
three identical chargen crashes on the Batch 1 build the evening before, so
this predates Batch 2 entirely.

### The poison only degrades safely if nothing FORMS A POINTER from it

**Learned 2026-08-01, by crashing on the first in-world arrow key — and the
most important structural lesson since the batch plan itself, because it
affects every remaining batch rather than one feature.**

`acc::off::Todo()` poisons to `kUnportedOffset` (0x7BAD0000) so a premature
read faults instead of silently returning a neighbouring field. That contract
holds for a READ. It does NOT hold for `base + offset`, which is ordinary
arithmetic yielding a wild but emphatically **non-null** pointer — and a
non-null pointer passes every `if (!p)` check between there and whatever
finally dereferences it, possibly in another TU.

The crash: an arrow key with a dialogue panel foreground. Panel identity
resolved fine (DialogCinematic is a ported slot), the listbox spec matched,
and its finder returned `panel + kDialogRepliesListBoxOffset` — still `Todo`.
The caller's `lb && ...` guard passed, and `DriveListBoxSelection` — which had
no SEH, because on KOTOR 1 that pointer is always real — dereferenced it.

**Why KOTOR 1 never saw this, though the code is identical:** on KOTOR 1 every
one of these offsets is a real value, so the pointer is always valid and the
missing guard never mattered. The defect cannot fire there. It is not
"unported in-world logic misbehaving" either — that class degrades correctly
by design. It is the *decline mechanism itself* having a hole, which is why
porting more code would not have fixed it: every future `Todo` offset used
this way crashes the same way, and Batch 3 alone carries ~60.

The fix, in three parts:

- `acc::off::Ok(off)` and `acc::off::Ptr(base, off)` in
  `engine_offsets_select.h`. **Use `Ptr` wherever an interior pointer is
  RETURNED or STORED** rather than read immediately under SEH; it converts a
  wild pointer into an honest null that existing guards handle.
- `DriveListBoxSelection` / `DriveListBoxSelectionEngine` now run their bodies
  under SEH (split into `*Body` helpers, since C2712 forbids objects in a
  `__try` frame). They are engine reads and every other engine read here is
  guarded.
- Converted the reachable-on-KOTOR-2 pointer-formers: the three listbox
  finders in `menus_listbox.cpp`, `GetServerPartyTable` (its comment
  explicitly reasoned "address arithmetic only, no guard needed" — precisely
  the assumption that breaks), and `PlayerVarTable`.

Still on raw addition, deliberately: the finders in `engine_radial.cpp`,
`engine_actionbar.cpp` and `minigame_pazaak.cpp`. Their modules are gated on
`IsKotor1()`, so they are unreachable on KOTOR 2 today — convert them when
their batch clears the gate, and prefer `Ptr` in new code from the start.

**The test round this batch needs (chargen + arrow-key crash fixes included,
retest from step 0):**

0. Character creation: open the name field (crashed before the fix), type or
   take the default/random name, proceed into the world. Typing is NOT
   expected to speak yet — the editbox surface is a later batch; the field
   itself, the buttons around it and the rest of chargen should navigate
   and speak, and nothing may crash.

1. `kdev apply --game k2`, launch, load into the world.
2. Sub-screen lifecycle: open Equipment/Inventory/Map/Journal by hotkey,
   switch between them by hotkey while one is open (the Switch handler's
   PrevSWInGameGui cleanup), Esc to close, Esc from world into the pause
   menu. Watch `SubScreen.Switch` / `SubScreen.Status` / `SubScreen.Hide`
   log channels; the Switch first-fire line proves the hook installed.
3. Feedback lines: pick up loot / get hit in combat and confirm spoken
   output + `Combat.MsgBuf raw:` lines for BOTH rings (combat and feedback
   categories land in different rings — exercise one of each).
4. Save-load replay suppression: quick-load and confirm the historical
   lines log as `replay-suppressed` rather than speaking.
5. KOTOR 1 regression pass afterwards (shared code moved: GetInputClass
   ungated, MainInterface offset now Pick, GetPlayerServerObject branch,
   four handlers ungated): menus + one in-world area, sub-screen open/
   close, one combat message.

The first test round surfaced two crash classes, both fixed and re-verified:

- **Unguarded engine reads in a K2-only diagnostic path.** AnnounceFocus read
  control captions via ReadCExoString/ReadU32 (guard-free by design) with no
  SEH — a garbage caption pointer crashed the process inside memcpy, including
  in a PRE-batch session. The probe now runs in one SEH-guarded pass
  (FocusProbe in menus_focus_k2.cpp) and an unreadable control is skipped, not
  announced. Lesson: the audit tool only sweeps files it is pointed at —
  K2-only TUs must be audited too, not just the shared chain.
- **The CGuiInGame slot walk ran on KOTOR 2.** IdentifyPanel's slot-table walk
  dereferences KOTOR 1's CGuiInGame layout and ran before the vtable fall-
  through; on KOTOR 2 it dereferenced a garbage base unguarded (WER fault in
  accessibility.dll, resolved by disassembling the DLL at the crash offset).
  Now: SlotTableLookup declines on KOTOR 2 outright and is SEH-guarded on
  KOTOR 1 too. Batch 2 ports the real table and clears the decline.

KOTOR 1's behaviour is intended to be untouched throughout, but this batch's
changes are the first that alter code KOTOR 1 executes (`Dispatch()` gained an
`if (k1)` structure, four handlers lost their `HandlerEnabled()` gates, the
slot walk moved into a guarded helper), so the next KOTOR 1 session should
sanity-run menus + one in-world area before trusting it.

**What Batch 1 shipped:**

- `kotor2.hooks.toml` now installs five hooks: SetActiveControl (was live) plus
  Update @0x004113A9, HandleInputEvent @0x00410AC8, HandleFocusChange
  @0x00418FE6, ListBox SetActiveControl @0x0041E9A4 (the implementation; the
  vtable slot is a forwarder). All cut bytes byte-confirmed off the exe.
- The three new frame-unpacking wrappers live at the bottom of
  `menus_focus_k2.cpp` (OnHandleInputEventK2 / OnHandleFocusChangeK2 /
  OnListBoxSetActiveControlK2), exported in exports.def. OnUpdate is hooked
  directly — it ignores its argument.
- **The HandleInputEvent hook uses `skip_original_bytes = true`** because its
  cut's first instruction loads EAX and the wrapper's consumed-exit TEST reads
  EAX after the cut replay (KPatchManager bug 2, by design). The wrapper
  emulates the cut's one effect (`this->input_code = param_1`, via the new
  `kMgrInputCodeOffset = Same(0x68)`). consumed_exit_address = 0x00410FA9, the
  single common epilogue where FS:[0] is unregistered — the engine's own
  repeat-debounce jumps there from mid-body, so the shape is engine-native.
- `Dispatch()` in core_tick.cpp is game-aware: KOTOR 2 runs the menu spine
  only (ValidatePanels, help, TickMonitors, PollHomeEnd, modsettings,
  update_checker, TickPendingOps, hotkeys/watchdog); every world / combat /
  camera / minigame / dialog phase sits in `if (k1)` blocks that later batches
  clear one by one.
- Panel-specific sub-handlers whose KOTOR 2 constants are still Todo/R now
  **decline at their own entry** with `if (acc::game::IsKotor2()) return...`:
  abilities, chargen feats, powers level-up, editbox (+ its monitor), galaxy
  map (+ Tick), keymap (+ Tick), pazaak board + deck builder, peek, cycle
  input, and the chargen-feats diagnostic dump. `grep -rn "KOTOR 2 (Batch"
  patches/Accessibility` enumerates them; clearing one means resolving its
  constants, deleting the decline, and re-running handler_chain_audit.py.

The first test round exercised the full loop (2026-08-01, PASSED after the two
crash fixes above): main-menu arrow navigation with speech, Enter/Esc into
Options and its sub-panels, the Gameplay settings chain (13 entries), listbox
row announces. Log channels for future rounds: K2.Focus / Menus.Input /
Menus.ListBox / Menus.FocusChange, plus "probe faulted" lines naming controls
whose caption offsets need per-class fixing.

Every other hook handler stays gated off by `acc::game::HandlerEnabled()`.

**Menu-subsystem worklist:** regenerate with

    python tools/re-scripts/port_worklist.py patches/Accessibility \
        menus.cpp menus_focus.cpp menus_chain.cpp menus_extract.cpp \
        menus_dispatch.cpp menus_internal.h engine_manager.h \
        engine_reads.cpp engine_panels.cpp

115 constants used, 110 resolved, **5 to verify** (was 79). The menu-reading and
navigation path is COMPLETE; the 5 are dialog-reply fields, one party field
parked on a dependency, and the two activation-only click primitives.

**Done this session** — all of it offline, no test round spent:
- Every panel-identity vtable in the codebase (see "Panel identity" below).
  `CSWGuiQuestItem` and `CSWGuiScriptSelect` established as K1-only.
- The shared control classes whole: `CSWGuiControl` tooltip + parent,
  `CSWGuiListBox` navigation state, `CSWGuiSlider`, `CSWGuiButtonToggle`.
- `CSWGuiListBox::SetSelectedControl` and `CTlkTable::GetSimpleString`.
- Panel layouts by .gui tag out of each panel's own constructor: portrait,
  map, level-up, store, journal, class selection, keymap row, editbox, wager,
  equip, item-entry row.
- `MoveMouseToPosition` — previously the doc's open item — plus
  `CAurGUIStringInternal`'s buffer pointer, previously the one gap in the text
  chain.

**The next task: clear the handler gates and take the first KOTOR 2 menu test
round.** The constants no longer block this. What stands between here and a
build worth testing:

1. Each menu hook handler's whole read/call chain has to be resolved before its
   `HandlerEnabled()` gate is replaced with an explicit branch — that is the
   `IsLoadingSaveGame` → null-pointer trap from the first session, and it is
   what "clear the gate" actually costs. `port_worklist.py` drives this, but
   expect it to pull in constants outside the nine files currently listed.
2. Swap the `SetCursorPos` stand-in in `menus_focus_k2.cpp` for the real
   `MoveMouseToPosition`, now resolved.
3. Act on the three CODE divergences listed under "Divergences that are CODE"
   below. They will not appear in any constant count and each misbehaves
   silently if KOTOR 1's logic is reused.

The 5 remaining constants are listed with their routes under "The last 5" and
can be picked up whenever their subsystem's turn comes.

The user's standing decision (2026-07-31) is to finish the subsystem offline
rather than take an interim test round: on KOTOR 2 the surrounding machinery is
what makes a sub-panel reachable at all, so a half-ported subsystem is both hard
to navigate to and likely to fault on arrival. Spend a test round only when its
result would settle or change how the remaining investigation is done.

`MoveMouseToPosition` is now resolved and should replace the `SetCursorPos`
stand-in in `menus_focus_k2.cpp` when the menu path is enabled.

**Do not** repeat the minimal-slice approach — see THE METHOD.

**Tools built for this port** (all in `tools/re-scripts/`, all offline):
- `rtti_scan.py` — class → vtable map from KOTOR 2's RTTI
- `vtable_map.py` — KOTOR 1 → KOTOR 2 addresses by vtable slot
- `vtable_xrefs.py` — the functions that STORE a class's vtable, i.e. its
  constructor and destructor. This is the one that reaches panel layouts.
- `find_thiscall_targets.py` — methods called on a known singleton
- `port_worklist.py` — what a subsystem needs, and what is unresolved
- `k2_caller_trace.py` — call-site census with pre-call context windows,
  forwarder-shape scan, and accessor-follow method tally. Caller COUNTS are a
  cross-game fingerprint usable before any decompile round; this is what
  identified all four Batch 2 hooks.

`find_thiscall_targets.py` and `vtable_xrefs.py` both run against KOTOR 1 too,
given `build/re/imagedump/swkotor-image.bin` from `kdev dump-text` (the Steam
exe is SteamStub-encrypted, so its bytes are not readable from the file). Doing
the same scan on both games and diffing the results is what located
`MoveMouseToPosition`.

**A KOTOR 1 offset is almost always cheaper to look up than to decompile.**
Three sources, in order of usefulness:

- `third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db` — a SQLite
  database with **4720 offsets keyed class + member name** and 9710 named
  functions. This is the best KOTOR 1 reference we have and it was found late;
  prefer it over decompiling. `select member_name, offset from offsets where
  class_name='CSWGuiInGameEquip' order by offset` prints a whole panel's layout
  instantly. It independently confirmed every portrait and equip value derived
  from constructors this session, and the `CSWGuiControl` fields (parent 0x14,
  tooltip_string 0x28, gui_object 0x34) read out of the KOTOR 1 decompiles.
- `docs/llm-docs/re/swkotor.exe.h` — struct definitions with member ORDER and
  embedded types. Complements the database: the database gives offsets, the
  header gives the sequence, and the sequence is what pairs against a KOTOR 2
  constructor's construction order.
- `k1_win_gog_swkotor.exe.xml` — every function and vtable SYMBOL by name, which
  is what turns a KOTOR 1 address into a class name.

The matching `kotor2_steam_aspyr.db` holds only 48 functions, 21 offsets and 14
globals, all of them creature/VM/inventory level — it was checked against the
menu worklist and contributes nothing there. Do not re-check it for GUI work.

Ghidra: project `kotor2`, program `swkotor2.exe`, at `C:\Tools\ghidra-projects`.
Decompile with
`KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe tools/ghidra-scripts/decomp.sh 0xADDR`.

**Two `decomp.sh` runs against the SAME project cannot overlap** — the second
dies on the project lock with `DECOMP_ERROR: no output produced`. Pass several
addresses to one invocation instead; the ~30-60s startup dominates, so extra
addresses are nearly free while a second concurrent run is a wasted round. A
`kotor1` run and a `kotor2` run in parallel are fine, and that pairing is the
right way to work: one round per game, compared afterwards.
Function catalogue (11,652 entries) at `docs/llm-docs/re/k2/k2-functions.csv`.
A space-free copy of the exe lives at `C:\Tools\k2re\swkotor2.exe` — the Ghidra
batch launcher cannot handle the spaces in the Steam path.

**Testing:** `kdev apply --game k2`, then read
`<K2 install>\logs\patch-*.log`. The K2 focus path logs under `K2.Focus`.

## THE BATCH PLAN (decided 2026-07-31)

**Frontload everything offline; test only when a whole system exists.** Decided
after a single cleared gate produced a KOTOR 2 build that navigated correctly
and spoke nothing — see "The hook gate is not the unit of work" below.

The work is split into batches so a FRESH SESSION can take one without
re-deriving context. Each batch is cut along dependency lines rather than
convenience: every batch closes at least one producer→consumer loop, so it is
independently testable and cannot produce the half-system failure again.

Start any session by running

    python tools/re-scripts/k2_hook_status.py patches/Accessibility

which reports, per hook, whether KOTOR 2 has it installed AND has its
`HandlerEnabled()` gate cleared. Both halves are needed; either alone is worse
than neither. After Batch 1: **5 of 25 READY** (the whole GUI spine).

Each batch means the same three things: resolve the constants its handlers'
call graphs touch (`port_worklist.py`), find its KOTOR 2 hook cut points (one
`PrintListing` read per hook — KOTOR 2's unoptimised build means NO KOTOR 1 cut
point, cut length or register source transfers), and clear its gates. Then one
test that exercises a complete loop.

Before each handler goes live, run

    python tools/re-scripts/handler_chain_audit.py patches/Accessibility <files>

and fix anything it calls UNGUARDED. That is what separates "degrades on
KOTOR 2" from "crashes on KOTOR 2".

**Batch 1 — GUI spine.** `OnUpdate` (the per-frame tick, and the ONLY thing that
drains the pending-announce slot), `OnHandleInputEvent` (input dispatch + the
navigation chain), `OnHandleFocusChange`, `OnListBoxSetActiveControl`. Gates in
`core_tick.cpp`, `input_pipeline.cpp` (2), `menus_dispatch.cpp` (2). Completes
the menu system, whose constants are already 110/115 — so this is mostly hook
cut-points. KOTOR 2 addresses already known: Update 0x004113A0,
HandleInputEvent 0x00410AA0, HandleFocusChange 0x00418FE0, ListBox
SetActiveControl 0x0041FEE0 (a forwarder to 0x0041E9A0 — decide which to hook).

### Batch 1 cut points (listings read 2026-07-31; WRITTEN + byte-confirmed)

All four are now in `kotor2.hooks.toml`. The bytes were confirmed straight off
the exe (it is not SteamStub-encrypted; `capstone` is installed for the Python
at reference_python_path, so full-function disassembly needs no Ghidra round).
Every cut is frame- or register-relative with no absolute operand, which is
what makes it safe to relocate into a trampoline. The listings below are kept
as the design record; where the implementation differs (HandleInputEvent's
skip_original_bytes), WHERE TO RESUME is authoritative.

**`OnUpdate` — `CSWGuiManager::Update` @ 0x004113A0. Cut at 0x004113A9, 9 bytes.**

    004113a6  MOV [EBP-0x4c],ECX          ; this stored
    004113a9  MOV EAX,[EBP-0x4c]          ; \ cut, 3 bytes
    004113ac  MOV ECX,[EAX+0x8c]          ; / cut, 6 bytes — panels.size

The +0x8c read is the same field KOTOR 1's hook point reads, which confirms the
function identity a third time. `OnUpdate` ignores its argument, so pass EBP.
**This is the hook the whole announce path depends on** — nothing else drains
the pending-announce slot.

**`OnHandleInputEvent` — `CSWGuiManager::HandleInputEvent` @ 0x00410AA0. Cut at
0x00410AC8, 9 bytes.**

    00410ac5  MOV [EBP-0x68],ECX          ; this stored
    00410ac8  MOV EAX,[EBP-0x68]          ; \
    00410acb  MOV ECX,[EBP+0x8]           ;  } cut, 3+3+3 = 9 bytes
    00410ace  MOV [EAX+0x68],ECX          ; / this->input_code = param_1

Arguments: `this` = [EBP-0x68], param_1 = [EBP+8], param_2 = [EBP+0xC] — so pass
EBP and read them, as `OnSetActiveControlK2` does. KOTOR 1 takes three registers;
none of that transfers.

**The two open problems this section used to list are SOLVED** (third session,
whole-function disassembly via capstone):

- The consumed-exit target is **0x00410FA9** — the function's single common
  epilogue (one RET; every path funnels there), which restores FS:[0] from
  [EBP-0xC] and so unregisters the SEH frame itself. The engine's own
  repeat-debounce consumes events by jumping there from mid-body (0x00410BF1,
  0x00410C45), so the jump shape is engine-native. At the hook the SEH scope
  index [EBP-4] is still -1 and stack depth matches the natural fall-through.
- A third problem surfaced and forced a design change: the cut's first
  instruction loads EAX, and the wrapper's consumed-exit TEST reads EAX after
  the cut replay (KPatchManager bug 2 — unfixable by design). Hence
  `skip_original_bytes = true` with the handler emulating the cut's one effect,
  the `this->input_code = param_1` store (`kMgrInputCodeOffset = Same(0x68)`).
  Register liveness at the resume CMP was checked across both branch paths:
  EAX/ECX/EDX are each written before their next read.

**`OnHandleFocusChange` — `CSWGuiControl::HandleFocusChange` @ 0x00418FE0. Cut at
0x00418FE6, 7 bytes.**

    00418fe6  MOV [EBP-0x10],ECX          ; \ cut, 3 bytes
    00418fe9  CMP [EBP+0x8],0x0           ; / cut, 4 bytes
    00418fed  JZ 0x0041904e               ; NOT in the cut — relative

Stop before the JZ: it is a relative jump and relocating it changes its target.
`this` is NOT yet stored when the handler runs, so take it from ECX and pass EBP
alongside for param_1 at [EBP+8]. The trampoline replays the CMP immediately
before returning to the JZ, so EFLAGS are set correctly — provided the wrapper
preserves flags across the handler call, which the local KPatchManager does
(see project_kpatchmanager_consume_test_bugs).

**`OnListBoxSetActiveControl` — hook the IMPLEMENTATION at 0x0041E9A0, not the
vtable entry. Cut at 0x0041E9A4, 6 bytes.**

Vtable slot 2 (0x0041FEE0) is only a forwarder; 0x0041E9A0 is the real body and
is what KOTOR 1 hooks the equivalent of. Its listing confirms the identification
outright, and incidentally confirms two constants resolved separately:

    0041e9b2  ADD ECX,0x2ac               ; kListBoxControlsOffset  = 0x2ac
    0041e9c1  CALL 0x0041e870             ; SetSelectedControl      = 0x0041E870

Cut covers `MOV [EBP-0x4],ECX` (3) + `MOV EAX,[EBP+0xc]` (3). `this` is in ECX at
entry; param_1 = [EBP+8], param_2 = [EBP+0xC].

**Batch 2 — In-game GUI lifecycle.** `OnSwitchToSWInGameGui`,
`OnHideSWInGameGui`, `OnSetSWGuiStatus`, `OnAppendToMsgBuffer`; gates in
`engine_subscreen.cpp` (4) and `msg_router.cpp`. Includes porting the CGuiInGame
slot table, which is what makes equipment / inventory / journal / map classify
at all — until then they fall through to vtable identification and read Unknown.

*Started 2026-08-01.* The constant surface is tiny — `port_worklist.py` over
`engine_subscreen.cpp` + `msg_router.cpp` reports **7 constants, 3 resolved, 4
unresolved**, and all four belong to `tutorial_popup.cpp` / the combat-pause
setter rather than to the handlers themselves. So Batch 2's real work is the
slot table and the four hook addresses, not offset archaeology.

#### The CGuiInGame slot table — RECOVERED (2026-08-01)

29 of ~35 slots, by a method that needs no decompiler and is now a tool:
`tools/re-scripts/k2_slot_table.py`. RTTI names each panel class's vtable; the
constructor is whoever stores that vtable into `[this]`; CGuiInGame's creator
(`0x007BE4C0`, with a smaller second creator at `0x007D0760`) calls each
constructor and files the result into its own slot. KOTOR 2's unoptimised build
is what makes the last step tractable — every intermediate lands in a named
stack temporary, so following the returned pointer to its `mov [this+off], reg`
is a short chain of `mov`s.

    python tools/re-scripts/k2_slot_table.py C:/Tools/k2re/swkotor2.exe \
        docs/llm-docs/re/k2/k2-functions.csv docs/llm-docs/re/k2/k2-vtables.csv \
        0x007be4c0 0x007d0760

**The result is the port's structural model in miniature: identical up to
+0x74, then KOTOR 2 inserts members and everything above shifts.** Do NOT mark
this table `Same` wholesale.

Unchanged (K1 == K2): Equip 0x0c, Inventory 0x10, Abilities 0x18, Journal 0x20,
Map 0x24, Options 0x28, DialogCinematic 0x40, DialogComputer 0x44, BarkBubble
0x4c, Examine 0x50, Container 0x54, CreateItemMenu 0x58, CreateItemSubMenu
0x5c, DialogLetterbox 0x60, Fade 0x6c, LoadModuleDebugMenu 0x70,
PowersFeatsSkillsDebugMenu 0x74, InGamePause 0x7c, Store 0x84.

Moved — and these are the ones that would misclassify silently:
- **InGameMessages 0x1c → 0x78.** Worse than a shift: KOTOR 1's 0x78 is
  PartySelection, so a stale table maps the two onto each other.
- SoloModeQuery 0x8c → 0x94, AreaTransition 0x94 → 0x9c, MessageBox 0x98 →
  0xa0, SkillInfoBox 0x9c → 0xac, TutorialBox 0xa0 → 0xb0, StatusSummary
  0xa8 → 0xb8.
- **0x14 is CSWGui3DSceneView on KOTOR 2**, where KOTOR 1 has InGameCharacter.
  KOTOR 2 also files 3DSceneView at two further slots. Check what KOTOR 2's
  character sheet actually is before mapping `InGameCharacter` at all.
- 0x48 is CSWGuiBlackenedLabel, where KOTOR 1 has DialogComputerCamera.

Still open: InGameMenu (its call at 0x007BEF23 does not follow the common
dataflow shape), one of the three MessageBox instances, GalaxyMap,
MainInterface, ControllerLossBox, DialogCinematicCopy, and the two
DialogMessages routing slots. The unresolved entries from the second creator
are duplicates of classes the first creator already settled, so they cost
nothing.

**The table is now IN the code** (engine_panels.cpp): 22 rows `Same`, 6 `Pick`,
10 `Todo`. `SlotTableLookup` runs on both games again — Todo rows poison to
`kUnportedOffset` and are skipped alongside the no-slot sentinel, so an
unported row costs its kind the slot-table route and falls through to the
structural / vtable detectors, never a fault that would abandon the walk.

#### The CGuiInGame pointer chain — also settled, from the same listing

The table is useless without a correct `CGuiInGame*`, and that chain fell out
of the creator's own callers. Two forwarders lead to it:

    0x0073F870:  MOV ECX,[this+0x04]  → call 0x0078C330
    0x0078C330:  MOV ECX,[this+0x40]  → call 0x007BE4C0   (the panel creator)

So `kClientExoAppInternalOffset` (0x4) and `kClientExoAppGuiInGameOff` (0x40)
are both confirmed identical on KOTOR 2, witnessed by a chain that provably
ends at the very object the slot table was read out of. `kClientExoApp-
InternalOffset` moved Todo → Same; the SERVER-side twin did NOT — same shape
is not evidence, and it has no witness yet.

**Unblocking a chain root unblocks its consumers, and that needs auditing.**
While `GetClientAppInternal()` returned null on KOTOR 2, everything downstream
failed safe for free. It no longer does. Most consumers stayed safe because
their own offsets are still `Todo` (kClientAppOptionsOffset and friends poison,
the read faults, SEH returns null) — but two did not, and both are now handled:

- `GetInputClass` reads a RAW `+0x9c` literal, not a marked constant, so it
  would have returned a plausible integer from the wrong field rather than
  failing. Now declines on KOTOR 2 until the field is resolved. Raw literals
  are invisible to `port_worklist.py`, which is why this needed reading rather
  than counting.
- `SetGuiInputClass` called its engine setter with no `acc::addr::Ok()` check
  (unlike its sibling `CloseInGameMenuToWorld`), so it would have faulted into
  its own SEH on every call instead of declining cheaply.

#### The four hook addresses — ALL IDENTIFIED (2026-08-01, fourth session)

Every Batch 2 hook target now has a KOTOR 2 address, each confirmed by
decompiling the KOTOR 2 candidate and the KOTOR 1 original in parallel Ghidra
rounds and matching structure landmark by landmark (sound-mode calls, script
names, the status switch, the ring-shift loop). Offline only — none of this
has run in game yet.

The route that worked, in order:

1. The `case 0xdf` prediction from last session was right in substance:
   scanning for `PUSH 7` immediately before a call through the `[this+0x40]`
   CGuiInGame chain found the Esc path inside one large 5-caller function —
   `CClientExoAppInternal::HandleInputEvent` = **0x007B12C0** (K1 0x00621210).
2. Caller-COUNT fingerprints then matched K1 to K2 before any decompile:
   K1 SwitchToSWInGameGui has 9 sites (1 in HandleInputEvent + 8 per-GUI-id
   trampolines); exactly one K2 candidate has 9 sites in the same pattern
   (8 id-wrappers at 0x757A50..0x757BA0 pushing ids 0-7). Same logic paired
   Show (3 vs 4 sites, two inside the dispatcher in both games).
3. One Ghidra round per game (11 K2 functions + 6 K1 references, run in
   parallel) settled every identity. The decompile pairs read like the same
   source compiled twice.

**The four hooks (KOTOR 1 → KOTOR 2), with byte-confirmed cut points.** All
cuts are frame-relative with no relative operands; at every cut ECX still
holds `this` and the params sit at [EBP+8]/[EBP+0xC]/[EBP+0x10], so the
Batch 1 frame-unpacking-wrapper pattern (ECX + EBP sources) applies directly.

- **SwitchToSWInGameGui** — 0x0062cf10 → **0x007CA550**. Cut at **0x007CA575**,
  7 bytes `89 4d e0 83 7d 08 00` (`MOV [EBP-0x20],ECX` + `CMP [EBP+8],0`).
  Stops before the `JL`; the trampoline replays the CMP right before the JL,
  the same flags-across-handler shape as Batch 1's HandleFocusChange. Like the
  K1 hook, this fires pre-guard (K1's 0x0062cf2d cut also precedes the range
  checks). `this`=ECX, GUI_id=[EBP+8].
- **HideSWInGameGui** — 0x0062cba0 → **0x007CA060**. Cut at **0x007CA066**,
  6 bytes `89 4d d4 8b 45 d4`. `this`=ECX, param_1=[EBP+8].
  CORRECTION: last session's forwarder note characterised 0x007CA060 as a
  "status getter" because its return is tested. It is Hide — the tested
  return is K1's own `if (HideSWInGameGui(0)) SetInputClass(0,1)` pattern.
- **SetSWGuiStatus** — 0x0062aa00 → **0x007C9C40**. Cut at **0x007C9C46**,
  6 bytes `89 4d fc 8b 45 08`. `this`=ECX, status=[EBP+8], p2=[EBP+0xC].
  The status machine is byte-identical to K1's (cases 1-4, values 1/2/3);
  **sw_gui_status lives at +0x34 on KOTOR 2.**
- **AppendToMsgBuffer** — 0x0062b5c0 → a PAIR: **0x007BE090** (67 call sites)
  and **0x007BE1B0** (23 call sites). KOTOR 2 split KOTOR 1's single message
  ring (78 callers) into two category rings — same body otherwise: empty-string
  guard, 0x40-capacity ring, 16-byte stride, shift-down loop, then store of
  (CExoString msg, dword type at +8, byte color at +0xC) and count++. Same
  `(CExoString*, ulong, byte)` signature, `ret 0xc`. Ring A: buffer ptr at
  gui+0x110, count at +0x11C. Ring B: ptr +0x118, count +0x124 (K1: +0xF8 /
  +0x100 — the +0x20 shift is why the K1-offset fingerprint scan failed).
  **Hook BOTH with the same handler** to reproduce K1 coverage; the dense
  sequential caller block at 0x82E000-0x830000 feeding ring A is the K2 twin
  of K1's 0x653000-0x665400 feedback-builder block. Cut for both at entry+3:
  **0x007BE093** / **0x007BE1B3**, 6 bytes `83 ec 10 89 4d f0` each.
  msg=[EBP+8], type=[EBP+0xC], color=[EBP+0x10].

**Identified alongside, needed by the same handlers:**

- `ShowSWInGameGui` — 0x0062c9b0 → **0x007C9DF0**. Confirmed by
  SetSoundMode(4), `"k_sup_guiopen"`, SetSWGuiStatus(3,1), the CanLevelUp
  default-panel branch — every K1 landmark in order.
- `PrevSWInGameGui` — 0x0062cdf0 → **0x007CA3C0** (decrements last_gui_panel,
  wraps -1→7). Its twin **0x007CA230** is NextSWInGameGui (wraps 8→0). Our
  Switch handler calls Prev; do not swap them.
- `CClientExoAppInternal::SetInputClass` = **0x007B3050**, and the input-class
  field is confirmed at **+0x9C** — which unblocks the two guards noted below
  (`GetInputClass`'s decline, `SetGuiInputGuiClass`'s missing Ok() check).
- `CClientExoApp::GetInGameGui` = **0x0073F750** (632 call sites — the
  app-wide accessor; body is exactly the documented `[this+4]` → `[+0x40]`
  chain). `GetSWGuiManager` = **0x0073FEA0**.
- `UpdateCreatedInGameGUI` = **0x007D0760** — last session's unexplained
  "smaller second creator" is this; both Show and Switch call it with
  (old_id, new_id).
- CSWGuiManager methods on KOTOR 2: AddPanel **0x00410530**, RemovePanel
  **0x00410670**, SendPanelToBack **0x00410780**, PanelExists **0x00410800**,
  PlayGuiSound **0x004122A0**. `CExoSoundInternal::SetSoundMode` =
  **0x0070BC60** (ExoSound global at 0xA1B494).
- CGuiInGame KOTOR 2 fields witnessed in the decompiles: in_game_menu +0x8,
  panel slot table from +0xC (as recovered), last_gui_panel +0x2C, gui-open
  flag +0x30, sw_gui_status +0x34, manager +0x38, in_game_pause +0x7C,
  **main_interface +0x98** (a slot the table had open — witnessed by
  SetSWGuiStatus adding/removing it on status 1), message rings
  +0x110/+0x118 with counts +0x11C/+0x124, initialized +0x128, and the twins
  of K1's +0xB38/+0xB3C pause-mode pair at **+0xF18/+0xF1C**.
  The InGameMenu slot is +0x8 on both games (witnessed by Show/Hide panel
  adds), closing another open row.
- Ruled out while searching: 0x007CBB40 is the fade starter (fade panel slot
  +0x6C, 20 callers), 0x007D0AF0 is a hide-request refcounter at +0xF0
  (17 callers), 0x007CE740 is a 16-byte item-notification setter at +0x100C.

The dispatcher's identity is triple-witnessed: 5 callers (K1's is called from
ProcessInput/PlayBackInputEvents), the case-0xdf Esc path with the in-world
guard and Show(7), and the hotkey triple (same-id → Hide via helper,
different-id → Switch, in-world → Show) at 0x7B1F70-0x7B2010 matching K1's
case 0xd1-0xd8 line for line.

The scan tooling from this session is promoted to
`tools/re-scripts/k2_caller_trace.py` (call-site census + forwarder shapes +
pre-call context windows for a target list; the census halves are what turned
caller COUNTS into a fingerprint usable before any decompile).

The seven still-`Todo` slots (InGameCharacter, GalaxyMap, PartySelection,
ControllerLossBox, DialogCinematicCopy, DialogComputerCamera, the
DialogMessages pair) can follow, or wait for their subsystem's batch.

**Batch 3 — World, area, transitions.** `OnSetMoveToModuleString`, `OnDoorOpen`,
`OnShowObject`; gates in `transitions.cpp`, `door_announce.cpp`,
`passive_narrate.cpp`. Largest offset surface — `engine_area.h` alone holds ~60.

*Started 2026-08-01 (fifth session). OFFSET FOUNDATION LANDED, gates NOT yet
cleared — not testable yet.* The closure worklist went **17→80 resolved of 137**
(`port_worklist.py` over the 43-file Batch 3 closure). Everything the area /
object / door / waypoint / trigger / placeable / path-graph read paths touch is
now resolved from decompiled load/save/ctor witnesses, not derivation:

- **CSWSArea** (loader 0x00523870, dtor 0x0052b4e0, GetRoom 0x0054b1d0): the
  game-object list and rooms array consolidated on K2 — game_objects
  0x190→0x194 / count 0x194→0x198, rooms 0x230→0x254 with its count 0x268→0x250
  now *adjacent*, room_names 0x25c→0x280 (stride still 8), name 0x150→0x154,
  tag 0x158→0x15c. Path graph: points count/ptr 0x238/0x23c→0x25c/0x260,
  connections 0x240/0x244→0x264/0x268 (per-point layout unchanged). GetRoom
  address 0x004BB600→0x0054b1d0.
- **Objects**: tag 0x18 Same; script_var_table 0x100→0x104, fixed CSWVarTable
  0x110→0x114 (both witnessed at the object serializer thunk 0x00540660, one
  slot apart). Door band shifted large (LocName 0x39c→0x3ec, GenericType
  0x2a1→0x2e1, Locked 0x2c4→0x304, OpenState 0x2cc→0x31c, Static 0x3c0→0x410,
  TransitionDest 0x3c8→0x418). Trigger / waypoint / placeable bands took a
  uniform **+0x40** (LocName 0x228→0x268, waypoint map-note 0x228/0x22c/0x230
  →0x268/0x26c/0x270, placeable Useable 0x328→0x380 / HasInventory 0x324→0x37c
  / ItemRepo 0x36c→0x3c4, trigger geometry 0x284/0x288→0x2c4/0x2c8, IsTrap
  0x2bc→0x2fc). CreatureStats FirstName 0x14→0x34.
- **The whole walkmesh mesh block is `Same`** — witnessed byte-for-byte in K2's
  BWM writer (0x005ea490) and CSWSRoom ctor (0x005ff440): surface mesh +0x3c,
  verts +0x54, face_count +0x58, faces +0x60, materials +0x64, adjacencies
  +0x88, stride 0xc. Base-engine walkmesh code KOTOR 2 did not touch.
- **CSWPartyTable** (SaveTableInfo 0x005fb1a0): server-internal→table
  0x1b770→0x1f0b4, member ids 0x4→0x8 (the +4 slot became num_puppets),
  solomode 0x190→0x238.
- **CSWSScriptVarTable API** (var-table cluster 0x005e6580..): GetInt
  0x0059a530→0x005e67d0, GetString →0x005e6850, SetInt →0x005e6a00, SetString
  →0x005e6ce0; CExoString dtor →0x00733780. Area map fog grid `Same`
  (+0x8/0xc/0x18/0x1c), module→areamap 0x218→0x238.

New offline RE tools this session (all in `tools/re-scripts/`):
`string_xref_stores.py` (GFF field-name → struct store: the workhorse for
load/save offsets on K2's unoptimised build) and `call_sites.py` (caller-side
`add ecx, <offset>` census — how the party-table root and object var-table
offsets fell out). The three hook cut points are byte-confirmed off the exe
(see below) but NOT yet written to `kotor2.hooks.toml`.

**THE ADDRESS ROUND IS DONE (2026-08-01, sixth session).** Every blocker from
the three tiers below resolved offline, each with an independent witness:

1. **Camera tier — RESOLVED.** The vtable slot map (`k2-vtable-slots.csv`)
   pairs `Gob::GetPosition`/`GetOrientation` and `Camera::GetPosition`/
   `GetOrientation` across the games; disassembling the K2 accessors read the
   fields straight off: Gob position `+0xa4`, quaternion `+0xb0` (K1 `+0x78`/
   `+0x84`), Gob still embedded at `Camera+0x4`. `kCameraGobPositionOffset` =
   Pick(0x7c, 0xa8), `kCameraOrientationOffset` = Pick(0x88, 0xb4). The
   chain roots got witnesses too: `kClientInternalModuleOffset` Same(0x18)
   (K2 GetModule facade internal 0x00726F80), `kCSWCModuleCameraOffset`
   Same(0x40) (GetModuleCamera internal 0x00781810), and
   `kServerExoAppInternalOffset` Same(0x4) (SetMoveToModuleString reads
   [this+4]). The pitch/yaw block near `+0x204` from last session's note was
   a red herring — the quaternion is the yaw source, as on KOTOR 1.
2. **Walls / map tier — RESOLVED where reachable.**
   `CSWCollisionMesh::LocalToWorld` → **0x005EE7B0** (found via K2
   ShowObject's renderDEV door path making K1's exact 18-call pattern;
   body verified: world_coords identity head, position +0x2c, quaternion
   +0x38). `CSWSObject::GetArea` → **0x005453C0** (calls the two
   already-banked K2 twins — GetObjectArray facade 0x0051C080 +
   CGameObjectArray::GetGameObject 0x0053DFB0 on [this+0x90] — and sits
   before GetGender exactly as K1's does). Client `GetGameObject` →
   **0x0073F4D0** (facade-cluster alignment, confirmed by its body calling
   the banked 0x0053DFB0 on [internal+0x14]). `GetObjectName` →
   **0x0073F0E0** (19-facade walk-back, every intermediate body matching its
   K1 role). Map-pin / fog accessors stay `R()` — map_ui_cursor remains
   K1-gated (its own offsets are still Todo), and every touch point is
   SEH-guarded.
3. **Party-roster tier — RESOLVED.** `CSWPartyTable::GetNPCObject` →
   **0x005FAAF0**, decompile-matched line for line (avail check, cached id
   at table+0x1c+slot*4, template-load with CSWSCreature(0x7f000000,0), the
   +0x9c dead-check + resurrection, <0xc bounds for K2's 12-NPC roster).
   CAUTION: its sibling **0x005FAD70 is the K2-only PUPPET variant** (cache
   at +0x14c, 3 slots) — same shape, do not confuse them.
   `GetIsNPCAvailable` → **0x005FA960** (avail array table+0x4c).
   `GetNPCSelectability` has NO confirmed twin — 0x005FA9C0 (array +0x11c)
   lacks K1's avail gate / 0xff default and may be K2's influence accessor;
   it stays `R()` and PartyTableIsNPCSelectable declines under SEH.
   Bonus: `kAddrCClientExoAppInternalHandleInputEvent` became
   Pick(0x00621210, 0x007B12C0) — the Batch 2 dispatcher — so the Q/E
   synthetic-retry path is live.

**What landed in code (sixth session):**

- `kotor2.hooks.toml`: three new entries — SetMoveToModuleString @0x0051BFD9
  (7-byte cut), DoorOpen @0x00619DAA (6 bytes), ShowObject @**0x00798D98**
  (6 bytes; ShowObject pinned to **0x00798D70** this session by decompile:
  SetMainInterfaceTarget head, LookAt null path, hostile-hilite array). All
  three cuts byte-verified off the exe; all frame-relative, EBP-only params.
- K2 wrappers `OnSetMoveToModuleStringK2` (transitions.cpp),
  `OnDoorOpenK2` (door_announce.cpp), `OnShowObjectK2` (passive_narrate.cpp)
  — EBP frame-unpacking, exported. The ShowObject wrapper computes the
  handle itself (obj at [EBP+8], id at obj+0x4 — byte-witnessed in
  0x00796B50); K1's cut had it precomputed in EAX. SetMoveToModuleString's
  K2 param is a clean frame VALUE — the K1 LEA-double-deref does not apply.
- Gates cleared: the three handlers lost `HandlerEnabled()`, and Dispatch()
  now runs `passive_narrate`, `camera_announce`, `door_announce`,
  `spatial.change_detector`, `transitions` on BOTH games (order preserved;
  camera_orient + camera_spin_guard stay K1 — the spin guard belongs to the
  ACTIVE edge-turn driver, unported; locked_recall / discovery / view_mode /
  map_ui_cursor / trap_watch stay K1).
- `handler_chain_audit.py` over the whole Batch 3 closure: 3 flagged lines,
  all the TrapDetectedByAnyOf offset ASSIGNMENTS whose reads are SEH-guarded
  two lines later — the degrade-by-design path, no action.
- `port_worklist.py` over the five now-live tick TUs: **0 unresolved**.
- `k2_hook_status.py`: **12 of 25 READY** (+ its shim table now knows the
  three Batch 3 wrapper names).

**Deliberately NOT resolved, with reasons:** GetNPCSelectability (identity
unproven, party-select screen only); the map-pin/fog cluster (map_ui_cursor
stays gated); `MaybeDrivePassiveSelection`'s IsGlobalFading /
DoPassiveSelection (K1-story fade workaround; its own local Todo chain
poisons first, so it declines safely on K2). Suspected but unbanked: K2
DoPassiveSelection ≈ 0x0079A4C0 and SelectNearestObject ≈ 0x0079B700 — the
only two functions calling K2 ShowObject (7 and 2 sites), matching K1's
caller pair by size and role; confirm before use.

**The Batch 3 test round (KOTOR 2):**

1. `kdev apply --game k2`, launch, load into the world.
2. Module transition: walk through an area-transition door. Expect the
   pre-load destination announce (`Transition` channel, the
   OnSetMoveToModuleStringK2 first fire) and the post-load area announce
   from transitions::Tick.
3. Room topology: walk between rooms; expect room announces (wall cache +
   GetRoom + path-graph offsets all landed last session; LocalToWorld now
   live for the wall scan).
4. Door facing: open a door as the leader; expect the facing readout
   (`DoorAnnounce` channel — proves OnDoorOpenK2 + camera yaw).
5. Q/E targeting: cycle targets in and out of combat; expect spoken target
   names (proves OnShowObjectK2 + GetNPCObject + GetObjectName + the
   party-roster filter).
6. Watch the log for `probe faulted` / SEH-decline lines naming anything
   still unported that got reached.
7. KOTOR 1 regression pass afterwards (Dispatch restructured, four shared
   constants became Pick, three handlers ungated): menus, one in-world
   area with room/door announces, Q/E, one module transition.

**Byte-confirmed K2 hook cut points (design record; write these when the gates
above are cleared):**

- **OnSetMoveToModuleString** — `CServerExoApp::SetMoveToModuleString`
  0x004aecd0 → **0x0051bfd0** (writes `internal+0x1008c`, 7 callers incl. the
  door-transition EventHandler; K1 had 6). Unoptimised prologue; hook after it
  at **0x0051bfd9**, cut `c7 45 fc 00 00 00 00` (7 bytes, `mov [ebp-4],0`).
  `this`=[EBP-0xc] (facade, already stored), dest CExoString*=[EBP+8] — a clean
  frame param, so **no LEA-vs-MOV double-deref** unlike K1. Needs an in-game
  first-fire check: the setter identity is caller-count-inferred, not yet
  landmark-confirmed.
- **OnDoorOpen** — `CSWSDoor::OpenDoor` 0x00589ceb → **0x00619d00**. The opener
  stamp is `mov [eax+0x36c],ecx` at 0x00619db0 (K1 stamped `[esi+0x31c]`). Hook
  at **0x00619daa**, cut `8b 45 c4 8b 4d 08` (6 bytes, `mov eax,[ebp-0x3c]` +
  `mov ecx,[ebp+8]`) — both frame-relative. `this`=[EBP-0x3c], opener id=[EBP+8].
- **OnShowObject** — `CClientExoAppInternal::ShowObject` 0x005f9c60 → **not yet
  pinned**. K2's `SetMainInterfaceTarget` is **0x007ce710** (reads `[this+0x98]`
  main_interface, matching the Batch 2 `main_interface +0x98` finding); its head
  wrapper 0x00796b50 computes `param_1 ? param_1->id : 0x7f000000` exactly like
  K1's ShowObject head. The K2 ShowObject that calls it through the
  `internal+0x40 → CGuiInGame` chain is the hook target — decompile 0x00796b50's
  callers (0x007401f0, 0x00798d70, 0x007b3050) to find it. K1 hooks mid-function
  at 0x005f9c8e reading EBX=obj / EAX=id; K2's unoptimised frame will expose
  both as `[EBP±x]`.

The offset conversions are safe on both games (`Pick` returns the K1 value on
KOTOR 1, identical to the prior `Todo(k1)`; KOTOR 2 handlers stay gated), so this
session's build changes nothing observable — it is pure offline groundwork that
turns Batch 3's remaining cost into one address round plus the gate-clear.

**Batch 3b — Dialog (ADDED 2026-08-01; user decision: this and 3c come BEFORE
the Batch 3 test round, since in-game testing is not meaningful while
conversations and interaction are silent).** The original plan cut batches
along HOOK lines and dialog/interaction are poll-driven, so they never got a
number — that was a planning gap, not a judgment that they come later.

*IMPLEMENTED 2026-08-01 (same session, one offline round — two parallel
Ghidra rounds + capstone/scan work, no test rounds). All 12 constants in the
dialog_speech closure resolved, each with an engine witness:*

- **Panel layouts from K2 constructors** (vtable_xrefs.py → ctor → tag
  wiring): DialogCinematic ctor 0x008BBA80 wires "LB_REPLIES" EMBEDDED at
  panel+**0x2760** and "LBL_MESSAGE" at +**0x2A50** (K1 0x19c4/0x1ca4;
  embed-not-pointer confirmed by the ctor's virtual call through the
  embedded control's own vtable). DialogComputer ctor 0x008BC620 wires
  "LB_MESSAGE" at +**0x35FC** (K1 0x2cfc) and repeats LB_REPLIES at 0x2760,
  confirming the shared CSWGuiDialog base layout on K2.
- **Reply block: the collision trap confirmed and resolved.** K2 SetReplyData
  = **0x007C0C70**, found by its unique 19-param `ret 0x4C` signature (one
  hit in the whole GUI range); body is K1's sixteen parallel arrays, types
  and order identical, all +0x20: count **gui+0x134**, text array
  **gui+0x138** (K1 +0x114/+0x118 — which on K2 are message-ring fields, as
  predicted).
- **Speaker block +0x20 too**: K2 HandleDialogEntry = **0x007CBF60** (unique
  `ret 0x5C`; identity by fade latch → fade starter 0x007CBB40, the
  SetReplyData loop, TLK gender dance, camera dispatch — which uses behavior
  id 0x106D where K1 uses 0x106A, noted for the camera batch). Speaker
  **+0x190**, listener +0x194, previous +0x198/+0x19C, latch +0x1A4.
- **CSWSObject.dialog_owner = Same(0x54)** — K2 setter twin 0x00546DB0 is
  K1's one-line store, called 3× from the K2 server dialog cluster
  (0x006C5xxx, located via the "EndConverAbort" GFF label). So the
  CSWSObject insertion sits ABOVE +0x54.
- **CreatureStats: Race 0xdc→0xe0, Appearance_Type 0x186→0x194**, each
  double-witnessed in the K2 stats GFF loader (0x006AFED0) and saver
  (0x006B3D10) via string_xref_stores.
- **BarkBubble object id 0x1c0→0x1CC** — K2 Draw (0x008BE740, vtable-slot
  paired) guards it against 0x7f000000, resolves through the client
  GetGameObject facade 0x0073F4D0 (Batch 3's find, mutually confirming) and
  runs the `< 36.0` six-metre-squared cull, K1's exact shape.
- **Slot rows closed**: DialogCinematicCopy = **Same(0x3c)** (it is the
  ACTIVE-dialog-panel pointer, not creator-built; witnessed by helper
  0x007CB750). DialogComputerCamera = **Same(0x48)** — the creator stores
  the 0x008BD910 ctor result at [gui+0x48]; **Batch 2's "0x48 is
  BlackenedLabel" note was a slot-table-tool misattribution** (the
  BlackenedLabel allocation follows immediately). The DialogMessagesAux/
  DialogMessages rows (K1 0xf8/0xfc) stay Todo deliberately: no consumer
  logic exists, and unresolved rows fall through to the vtable detector.
- Gates: no per-file declines existed; Dispatch's dialog_speech phase now
  runs on both games. One poison-pointer conversion applied along the way
  (the chargen-feats description-listbox finder in menus_listbox.cpp formed
  a raw base+Todo pointer and the feats panel can classify via RTTI on K2).
- No new hooks, as scoped. `port_worklist.py dialog_speech.cpp`: **12/12,
  0 unresolved**; chain audit over the dialog files: clean after the Ptr
  conversion.

The Batch 3b test items (fold into the combined round): talk to an NPC —
entry text + replies spoken, reply arrow-navigation and selection; a
computer/droid terminal (LB_MESSAGE path + the copy-slot alias); an
overheard NPC bark (bark-bubble path + speaker classification).

**Batch 3c — Interaction: walk-to-target, Enter-interact, action surfaces
(ADDED 2026-08-01, same decision).** The big one — `port_worklist.py` over
`interact_dispatch.cpp, input_poll_router.cpp, guidance_approach/autowalk/
beacon/description/pathfind.cpp, narrated_target.cpp, cycle_input.cpp,
engine_picker.cpp, engine_actionbar.cpp, engine_player_inputlock.cpp`
reports **46 constants, 37 unresolved**, concentrated in:
- `engine_picker.cpp` (16): the internal action-descriptor table
  (stride/id/label/icon/fn/target), hover/last-clicked/last-target fields,
  plus R() addresses: GetDefaultActions, HandleMouseClickInWorld,
  ActionInitiateDialog, PopulateMenus (SetMainInterfaceTarget's K2 twin
  0x007CE710 is already known from Batch 2/3).
- `engine_actionbar.cpp` (9): MainInterface personal-action lists + strides,
  DoPersonalAction, RePopulateMainInterface.
- Action-queue primitives (all R()): CSWSCreatureActionManager,
  AddMoveToPointAction, ForceMoveToPoint, AddUseObjectAction,
  ClearAllActions — the autowalk/approach backbone.
- Player-control: kClientAppPlayerControlOffset + SetEnabled.
- The action-node list fields (kObjectActionNodesOffset + linked-list
  internals) shared with combat diagnostics.
Gates: the Dispatch `if (k1)` phases interact / guidance.approach /
guidance.cancel / guidance.beacon / engine.inputRestore / cycle_input /
announce_degrees, and cycle_input's own Batch-1 decline. May also need the
OnClientHandleInputEvent hook on K2 (target already known: the Batch 2
dispatcher 0x007B12C0) — decide when the chain enumeration says who consumes
it. Several sessions, Batch-3-sized. Port it WHOLE per THE METHOD — the
unified action menu, narrated-target slot and native walk-then-talk dispatch
carry several fossilised workarounds (see the memory notes on the unified
action menu and distant-NPC dialogue).

**Batch 3c — IMPLEMENTED 2026-08-01 (seventh session, one sitting).** The
scope grew to include engine_radial.cpp (the picker's live path calls
ResolveTargetActionMenu / LogStateWide / LogTargetDiag, and the doc mandates
porting the unified action menu whole): final worklist over the 14-file
closure = **92 constants, 91 resolved**; the survivor is
kClientObjectServerObjectOffset, whose Todo is DESIGNED (KOTOR 2 consumers
call the engine's own GetServerCreature resolver instead — do not "fix" it).
Chain audit: 1 unresolved use, guarded. Build green (112 TUs). Uncommitted,
untested.

Address-resolution method notes (all offline; eight kotor2-project Ghidra
rounds, two kotor1 rounds, plus capstone byte scans):
- **Server action queue:** AddUseObjectAction found via a
  `push 0xffff; push 0x28` byte scan (which also yielded K2's AddAction at
  0x0053F7F0); AddMoveToPointAction via AddAction's caller census (id-1
  shape + path-find resets); ForceMoveToPoint via its unique NINE
  AddActionNodeParameter call sites (7 point + 2 object); ClearAllActions
  (0x00541080) via TWO independent caller shapes (ActionInitiateDialog's
  GetServerObject→Clear(1), DoPersonalAction's GetServerCreature→Clear(0));
  ActionManager (0x00563C90) as the 134-byte double-Clear caller comparing
  its param against 1/2/8/4 (the K1 `<10` byte check is refactored into
  helper 0x00571980; the byte field moved +0x9f2→+0x1116).
  kObjectActionNodesOffset 0xfc→0x100 (AddAction's enqueue loads
  `ecx = this + 0x100`); list internals {head+0, tail+4, count+8}, node
  {prev+0, next+4, data+8} — count offset Same.
- **Picker internals:** GetDefaultActions (0x007B40F0) found by icon-resref
  string xrefs (only function referencing all six of i_noaction/i_dialog/
  i_opendoor/i_useplace/i_disablemine/i_recovermine); HandleMouseClickInWorld
  (0x007B3CA0) by the click-gate displacement cluster + source order (sits
  right before GetDefaultActions, as in K1). Internal offsets: last_target
  Same(0x2b4), last_clicked Same(0x2b8), hover 0x4a4→0x4b0 (pos vector
  0x4b4..bc), descriptor array 0x4c8→0x4cc, count 0x4cc→0x4d0. The
  descriptor struct kept every field position (label 0, id 8, fn 0xc, target
  0x1c, icon 0x20, flags 0x30) and grew its stride 0x38→**0x3C** (tail
  growth). K2 kept K1's action ids and GUI strrefs verbatim.
- **Verb functions** (from the fn-pointer immediates K2 GetDefaultActions
  stores): ActionInitiateDialog 0x0077CF70, ToggleDoorState 0x0087BB20,
  door MenuActionBash 0x0087BD10, UsePlaceable 0x00841CE0, BashPlaceable
  0x00841D40, DisableMine 0x0087F2D0, RecoverMine 0x0087F330,
  ActionMenuAttack 0x0077CE00. K2-only additions observed: saber-cut door
  row (id 0x407, i_doorsaber), corpse-container "UseSearch" label variant.
- **GUI surfaces:** SetMainInterfaceTarget 0x007CE710 (ret 8 confirmed,
  guards CGuiInGame+0x98); CSWGuiMainInterface::SetTarget 0x0074CFB0
  (target 0x64→0x68, refresh-hint 0x5cb0→0x595c); PopulateMenus
  **0x0074CFE0 — grew an int arg, every engine caller passes 1** (per-game
  typedef in engine_picker); RePopulateMainInterface 0x007CEB50 (argless
  forwarder, passes the 1 itself — the safest K2 entry); DoPersonalAction
  0x00751750 (ret 8 both games; found with DoTargetAction 0x007445D0 via
  the six-strref "can't do that" switch, disambiguated by layout reads);
  SelectNext/PrevAction 0x00744260/0x00744410 (via the TAM target-type-byte
  displacement scan, disambiguated by wrap direction). MainInterface:
  personal lists 0x74→0x78 (SEVEN columns on K2 — kColumnCount stays 6,
  the extra column is additive), selected-id array 0x1bac→0x1c7c, TAM
  embed 0xBC→0xCC, column UI array 0x771c→0x733c stride 0x71C→0x750 (the
  GetColumnActionButton constexpr base now goes through Pick too). TAM:
  lists Same(0/4/0xc), selected 2-D Same(0x24), target-type 0x1AEA→0x1BAA,
  target_actions Same(0x54) stride 0x750, name label 0x15CC→0x1668, row
  sub-controls 0/0x1D0/0x3A0/0x570, row flags 0x710/0x714→0x740/0x744
  (witnessed in the refactored setter 0x00741780), shown-flag byte at
  +0x74d (diag constant anchored at 0x74c to keep the 4-byte read in-row).
- **Doors:** client-door deltas are PIECEWISE with both brackets witnessed:
  cannot_bash Same(0x104), can_use_actions 0x108→0x10c, field17
  0x138→0x13c; is_hostile/state (diag-only) fixed at +4 by the bracketed
  single-insertion model. GetIsOpen is anim-based in BOTH games (current
  anim vs 0x2742/0x2743) — no mechanism change. Server-door security gate
  0x2D8→0x328 (witnessed in K2 CSWCDoor::GetTargetActions 0x0087BEC0,
  found via the GetCanUseSkill(6) caller census; note the KeyRequired GFF
  string is placeable-only on K2, the door loader differs).
  GetCanUseSkill 0x00845730; lvl-up stats 0x2F8→0x310 (listing-witnessed).
- **Player control:** SetEnabled 0x00865250 (decompile + listing: K1's body
  line for line); player_control slot Same(0x2a0) on the INTERNAL
  (witnessed inside K2's real SetInputClass 0x007B3050, the +0x9c writer).
- **vtable-As slots:** Door Same(0x14), Creature 0x28→0x2c, Trigger
  0x38→0x3c, Placeable 0x48→0x4c (one virtual inserted below +0x14).

**Three calling-convention divergences, all byte-verified and coded:**
1. AddUseObjectAction dropped its unused second arg on K2 (ret 8 → ret 4)
   — per-game typedef in guidance_autowalk.cpp UseObject.
2. PopulateMenus takes an int on K2 (engine callers pass 1) — shared
   CallPopulateMenus helper in engine_picker.cpp used by Drive and
   ReanchorRadial.
3. Everything else kept K1's ret sizes (audited: AddMoveToPointAction 0x44,
   ActionInitiateDialog 8, DoPersonalAction 8, DoTargetAction 8,
   SetMainInterfaceTarget 8, Select* 4, ClearAllActions 4, ActionManager 4,
   SetEnabled 4, HandleMouseClickInWorld/RePopulate 0).
CSWSForcedAction's layout matches K2's reads field for field — no change.

**The two new hooks (kotor2.hooks.toml, cuts byte-confirmed):**
- OnClientHandleInputEventK2 @0x007B12EE — the in-world dispatcher
  (0x007B12C0), hooked at the return-slot init after the `this` store; the
  7-byte cut never touches EAX so the consume TEST works without
  skip_original_bytes; consumed exit enters the single epilogue AFTER its
  return-value reload (0x007B2E5E) with eax excluded from restore, and the
  epilogue's `MOV ESP,EBP` + FS-unregister make it stack-depth-proof.
  Params ecx + ebp; the wrapper hands [ebp+8]/[ebp+0xc] slot ADDRESSES to
  the shared K1 handler, preserving its caller-eip read at [ebp+4].
- OnProcessInput @0x007B0EAB — the frame tick (0x007B0E90), cut on the
  6-byte `this` reload; the replay re-establishes ECX for the resume.
The dispatcher's case map was decompiled and is IDENTICAL to KOTOR 1's —
Q/E 0xcc/0xcd, Esc 0xdf, bare keys 0xe2..0xee with the same
logical-code→column mapping the restamp logic uses, R 0xef — so the
modifier-space reservation, overlay-Esc consume and bare-key prep transfer
unchanged. Combat-queue attribution inside the handler
(ReportPrePressDepth / ArmUserQueueAdd / LogPreFire) stays KOTOR-1-gated
inline until Batch 4 resolves combat internals.

**Gates cleared:** Dispatch phases cycle_input, announce_degrees,
guidance.beacon, engine.inputRestore, guidance.approach, guidance.cancel,
engine.actionQueueDiag, interact; cycle_input's TryHandleEvent decline;
OnClientHandleInputEvent + OnProcessInput HandlerEnabled gates. Kept
KOTOR-1-gated: map_user_markers, the probe pollers, view_mode.poll,
levelup_pause, engine.inputReassert (its chain is the deliberately-deferred
SetPauseState/SetSoundMode pair from Batch 2 — CAUTION: K2's SetSoundMode
takes TWO args).

**The Batch 3c test items (fold into the combined round):** walk-to-point
(Shift+minus), Enter-interact on a door / a container / an NPC (walk-then-
act with input left enabled — watch the approach tracker's "way blocked"
path), Q/E cycling + comma/period discovery cycling, the unified action
menu (Shift+numbers) including variant cycling and bare 1..7 re-stamp, a
modifier combo of an engine-bound key (reservation consume — check
Diag.ModShadow), Esc while a mod overlay is open (no pause menu), and one
KOTOR 1 regression pass over the same list (Dispatch was restructured a
FOURTH time, the input handlers lost their gates, and UseObject/
PopulateMenus calls went per-game).

**Batch 3d — Sub-screen internals (ADDED 2026-08-02, user-proposed after the
second KOTOR 2 round, and the recommended next batch).** The two in-world
rounds showed the SPINE is healthy on KOTOR 2 — panels identify, titles
announce, focus walks, listboxes read — while the per-screen readers behind
each sub-screen are still KOTOR-1-shaped. That is the gap the user heard as
"screens announce correctly but do not all read correctly", and the
stability risk they flagged: each unported per-screen reader walks
KOTOR 1 offsets on KOTOR 2 panels, which is precisely the situation that
produced defects 4 and 5.

Scope, in the order the evidence justifies:
1. **Guard sweep first** (cheap, prevents the fifth crash of the same
   class): audit every control/panel read reachable from the menu monitors
   for a missing `__try`, not just the ones a crash has already found.
   `handler_chain_audit.py` finds unresolved-constant reads; this is the
   complementary case — RESOLVED offsets read from a possibly-dead pointer.
2. The five remaining CGuiInGame slot rows (PartySelection, InGameGalaxyMap,
   ControllerLossBox, DialogMessagesAux, DialogMessages).
3. Per-screen field maps for the readers that matter most in play:
   character sheet (menus_charsheet), equipment, inventory, abilities,
   journal — each needs its own KOTOR 2 panel-constructor decompile, the
   same way Batch 3b did the dialog panels.
4. The chargen editbox / typing surface, still unported and already known
   to be silent.

Method note that made Batch 3c cheap and applies here: mine each panel's
control ids from the game's OWN gui.bif (`unkeybif e data/gui.bif
chitin.key` + `gff2xml`), and key readers by control id rather than array
position — both games assign the same ids, while their engines build
controls[] in different orders. That is how the in-game menu icons were
fixed, and it removes a whole class of per-game guessing.

**Batch 3d — IMPLEMENTED 2026-08-02 (eighth session, together with 4+5).**
The witness ledger:

- **Guard sweep:** four unguarded engine reads fixed in menus_monitors.cpp
  (control-id fallback read, two panel vtable reads, the StatusSummary
  bit-flags read, the dialog-reply selection short) and two in
  menus_internal.cpp (FindControlById's whole walk, IsSaveLoadPanel's
  vtable read). New SEH leaf helpers `TryReadU32/U16/Ptr` in engine_reads.
- **Slot rows, all five closed** from a full decompile of the creator
  0x007BE4C0: PartySelection **Pick(0x78, 0x1c)** — it SWAPPED slots with
  InGameMessages; InGameGalaxyMap **Same(0x80)**; ControllerLossBox
  **Pick(0xa4, 0xb4)** (ctor inlined in the creator; K2's 0xa4 is a second
  MessageBox instance); DialogMessagesAux/DialogMessages **Kotor1Only** (no
  K2 store past +0xb8 — those kinds fall to the vtable detector). The
  creator also re-confirmed every existing Pick row independently.
- **Per-screen field maps** from one ctor round (charsheet 0x0084C3A0,
  abilities 0x008A25C0, equip 0x008A92D0, inventory 0x008A6170, journal
  0x007FAE60, messages 0x00757C40, name-chargen 0x00918B10, save-name
  0x008586D0) plus targeted method decompiles: charsheet fully Pick'd
  (SetStats twin 0x0084E6F0 witnessed saves-into-_STAT, FP behind IsJedi
  with the Same(0x2) shown bit, HP cur/max Same(0x4c/0x4e) behind
  clientCreature+0x310); abilities fully Pick'd including the button trio
  0x008A5790/57D0/5810 (tab byte at CGuiInGame+0xfc0), OnEnterSkill/Feat/
  Power twins, DisplayPowers 0x008A5620, UpdateView 0x008A3C60,
  SetSelectedSkill 0x0089C070 and the re-derived chart internals (first
  column 0x60, stride 0xb4, featId +0xa8, status +0xac); equip labels from
  the stat-writer 0x008AD930 (HP row Kotor1Only — no vitality label on K2);
  journal (desc listbox Pick(0x1a4,0x1b0) settled by member-ctor order,
  OnControlEntered 0x007FC390, Populate 0x007FC880, three sort buttons
  replace BTN_SORT); inventory (Populate 0x008A7BA0, CheckFilter 0x008A8DF0
  keying the filter byte at gui+0xfc1, SEVEN direct filter buttons matched
  as a range). Editbox surface cleared (both panels Pick'd; internals were
  already resolved). Sub-screen closure worklist: **100/100.**

**Batch 4 — Combat. IMPLEMENTED 2026-08-02 (same sitting).** The four
CombatRound hooks plus `OnSetPauseState`; gates in `combat_diag.cpp` (3)
and `combat_queue_hooks.cpp`. What landed:

- **The action struct is byte-identical on K2** (ClearData twin 0x0058C810
  vs K1's 0x004D1D50 — same sentinels/init at every offset; loader allocs
  the same 0x88), so ALL kCombatRoundAction* went Same.
- **Round block moved +0x150** (GFF saver 0x00592310: Timer 0xa94, Length
  0xa9c, CurrentAttack 0xabc, Engaged 0xb08; loader 0x00592840 appends
  SchedActionList through [round+0xb00]); current-action byte at **0xb24**
  (setter twin 0x005908A0); creature→round at **+0x10dc** by a 35-site
  caller census (the +0x724 derivation would have said 0x10ec — witness
  beat arithmetic again). List internals all Same.
- **Hooks (cuts byte-confirmed):** AddAction 0x00590270 @0x00590276,
  RemoveAllActions 0x00590430 @0x00590436, RemoveLastAction 0x005904C0
  @0x005904C6, SetCurrentAction 0x005908A0 @0x005908A4, SetPauseState
  internal 0x00538ED0 @0x00538ED9 (its body carries K1's pause fields at
  K1's own offsets — bits +0x10078, timers +0x10048/4c/50). K2 wrappers
  pass frame-slot ADDRESSES to preserve the esp+N LEA contract.
- **Accessors:** SetPauseState facade Pick(0x004ae9a0, 0x0051C760);
  client GetPauseState Pick(→0x0073F680, field 0x1cc identical),
  GetAutoPaused Pick(→0x00740960, K1's [internal+0x384]&1 byte for byte),
  GetCombatMode Pick(→0x00740860, field 0x320 identical); combat-mode bit
  Pick(0x440, 0x458) from the DoTargetAction twin; effects list
  Pick(0x124, 0x148) + effect Type Same(0x8) from the EffectList
  serializers; HP Same(0xe0); weapon slots Same via the K2 slot mapper
  0x006D0670; GetFeat/GetSpell/rules-spells (0x104) from the OnEnter
  twins. Gates cleared: all combat Dispatch phases + special_watch + the
  input-pipeline queue attribution + engine.inputReassert.
- Deliberately unresolved (guarded degrades, listed for a later pass):
  effect-icon trio (K2 routes the array through a passed-in list — no
  creature-relative witness), faction id, stats feats list, stealth mode,
  GetDamageLevel/GetLevel/GetBlind/Get{Feat,Spell}NameText accessors
  (examine-view extras; CallIntThis guards them).

**Batch 5 — Audio. IMPLEMENTED 2026-08-02 (same sitting).**
`OnSetListenerPosition` + `OnCalculatePitchVarianceFrequency` live;
`OnPlayFootstep` was deferred here and ported 2026-08-02 (eleventh
session) after the documented candidate was refuted — see the footstep
ledger in WHERE TO RESUME.

- **The whole CExoSoundSource facade cluster paired by capstone** (no
  Ghidra needed for the setters): ctor 0x0070AAB0, ctorWithResRef
  0x0070AB90, dtor 0x0070AB60 (deleting on BOTH games), SetPriorityGroup
  0x0070ACC0, Set3D 0x0070ACF0, Play 0x0070AD50, SetVolume 0x0070AD80,
  SetPitchVariance 0x0070ADD0, SetLooping 0x0070AE30, SetPosition
  0x0070AE90, GetLooping 0x0070AE60 ([internal+0x24] both games),
  SetDistance 0x0070AEE0, SetResRef 0x0070AF20, Stop 0x0070AF80 (absent
  from the function catalogue; between SetResRef and SetFixedVariance
  0x0070AFA0). ExoSound global PickGlobal(0x007a39ec, 0x00A1B494) — NOTE
  K1's holds the internal, K2's the facade object; all uses go through the
  matching game's own functions.
- **One-shots:** PlayOneShotSound facade 0x0070BA40 and Play3DOneShotSound
  0x0070BA90 — IDENTICAL signatures to K1 including the z+z_offset fadd.
  (0x0070B170 is NOT it — that's the stream/VO facade into 0x0070C0C0.)
  This makes cue playback live and closes in-world defect 3.
- **Priority groups:** table ptr Pick(0x4c, 0x50), stride/fields Same
  (getter twin 0x00709010, volume-byte use in 0x007101F0).
- **Pitch internals:** the K2 jitter twin is **0x00710550** (the only
  rand() caller in the sound TU): base freq [internal+0x50], applied rate
  [internal+0x5c], variance +0x7c; voice3d Same(0x3c), handle Same(0x4).
  K2 moved to the newer Miles sample API — the IAT went
  Pick(0x0073D4E8, 0x00986508 = _AIL_set_sample_playback_rate@8), same
  (handle, rate) shape. Hook at the twin's entry, consume exits to its
  bare RET — on K2 consuming alone IS the neutralise (callers read the
  field, not EAX).
- **Listener:** facade 0x0070B940 (single caller — the K2 UpdateSoundEngine
  path), internal 0x007082D0 → _AIL_set_listener_3D_position. Hooked at
  entry with the same (ecx, esp+4) contract as K1; the shared handler runs
  unmodified.
- **SetSoundMode signature split handled** at both call sites
  (engine_subscreen + tutorial_popup): K1 internal takes (mode), K2's
  facade 0x0070BC60 takes (mode, flag=0) ret 8 — per-game typedefs.

**Batch 6 — Minigames and leftovers**, AFTER a triage pass deciding what KOTOR 2
even has. `OnTurretBulletHit`, `OnPlayerFire`, `OnRulesInit`. Do not spend
constant work here before triage: several KOTOR 1 modules are story- or
minigame-specific and may have no KOTOR 2 counterpart at all, the way
CSWGuiQuestItem and CSWGuiScriptSelect turned out not to.

Scale, stated plainly so no batch is under-estimated: ~400 offsets and ~200
addresses remain overall, and each of the 24 unhooked handlers needs its own
KOTOR 2 listing read. The big batches are several sessions each.

## Target

Steam / GOG KOTOR 2, Aspyr's 2015 rebuild. PE link timestamp
2015-09-23 19:41:17Z (`0x5603005D`). The installed Steam copy's SHA-256 matches
the `kotor2_steam_aspyr` entry in KPatchManager's `AddressDatabases/`, so the
framework and this patch agree on which binary this is.

The exe is **not** SteamStub-encrypted (no `.bind` section), so unlike KOTOR 1
it can be read straight off disk — no `kdev dump-text` step to get byte
reference material.

## Architecture decision — one binary, runtime dispatch

**Decided 2026-07-31.** A single DLL serves both games, selecting addresses and
struct offsets at runtime from the detected game.

The alternative considered and rejected was a second build target (per-game
compile-time constants, two `.kpatch` artifacts). It was rejected on
maintenance grounds, and the argument is worth keeping: a few hundred files
here encode behaviour that is *identical* between the games. With two binaries
every KOTOR 1 fix must be consciously re-applied to KOTOR 2, forever, and the
failure mode is silent — a fix that simply never arrives in the other game.
With one binary, divergence is explicit and local (`if (acc::game::IsKotor2())`
at the handful of places that genuinely differ) and everything else is shared
by construction.

The cost objection to the single binary turned out to be much smaller than it
first looked. Converting `constexpr size_t kFoo = 0x90;` to a runtime variable
leaves **every call site unchanged** — same name, same expression. Only the
declaration and a per-game table move. A codebase-wide check of the 243
constants in `engine_offsets_*.h` found **zero** used where C++ requires a
compile-time constant:

- array bounds: 0
- `static_assert`: 0
- `case` labels: 0
- template arguments: 0
- feeding another `constexpr`: 0

The conversion is mechanical.

## What the port actually costs

**Not the vtables.** The K2 exe ships full RTTI — 389 type descriptors with
class names identical to K1's. `tools/re-scripts/rtti_scan.py` walks
type descriptor → complete object locator → vtable and recovers **392 named
vtables**; output is checked in at `docs/llm-docs/re/k2/k2-vtables.csv`. All 19
of our vtable-identity constants resolve by name automatically. Beyond the
constants themselves, this gives named anchors for decompiling everything else,
which is the part the feasibility doc could not have priced.

Regenerate with:

    python tools/re-scripts/rtti_scan.py <path-to-swkotor2.exe> > out.csv

**Struct offsets shift by a constant delta per class, not randomly.** Diffing
the 21 offsets that both seeded databases share:

- Base/shallow classes are **identical**: `CAppManager`, `CExoString`,
  `CGameObject`, `CSWBaseItem`.
- Derived classes shift **uniformly**: every `CSWSObject` field by 4, every
  `CSWSCreatureStats` field by 4, both `CSWSCreature` fields by the same larger
  delta.

That is field insertion near the top of a base class, not a redesign. Verifying
two fields per class carries the rest — but confirm, don't assume: the delta is
piecewise, identity below the insertion point and constant above it.

**The offsets surface is about double what the headers suggest.** Codebase-wide
there are ~494 offset-shaped constants; only ~half live in `engine_offsets_*.h`.
The other ~258 are scattered across ~40 files — `engine_area.h` alone holds 60,
then `engine_radial`, the minigames, `engine_picker`, `engine_actionbar`. The
Phase-2 consolidation captured part of this, not all of it. Consolidating the
strays is part of the offsets step, and would have been needed under either
architecture.

**`kdev sigscan` contributes nothing here** and that has not changed. It finds
the same compiled bytes relocated; K2 is a recompile, so functions were
re-emitted rather than moved. 0 of 213. Its value stays confined to KOTOR 1
build variants.

## Delivery mechanism — already supported

Nothing new is needed to ship one mod into two games:

- `hooks.toml` already carries `[metadata] target_versions` keyed by executable
  SHA-256, and we already ship a second hooks file for K1's 2004 relink
  (`relink2004.hooks.toml`). K2 gets `kotor2.hooks.toml` the same way.
- `manifest.toml` `[patch.supported_versions]` takes the two K2 hashes.
- The installer already has the K2 half: game-version selection, K2 path
  detection, TSLRCM / K2CP / Tweak Pack flows, and it already applies two K2
  `.kpatch` files (4GB-aware, borderless) against known Steam and GOG hashes.
- `swkotor2.exe` imports `dinput8.dll`, so the proxy loader works unchanged.

## The lesson from the first working slice: addresses port, WORKAROUNDS do not

The first KOTOR 2 feature (menu focus announce) works. Getting there cost four
attempts, and none of them was an address being wrong — every offset, vtable
and function address derived for this port read correctly on first contact with
the running game. The whole cost was in engine BEHAVIOUR.

What went wrong: keyboard navigation only ever reached two menu entries, because
the mouse cursor was parked on a button and the engine kept handing it focus via
HandleFocusChange, overriding the arrow keys. KOTOR 1 has the identical
conflict. Our KOTOR 1 mod has solved it since forever, by warping the engine's
cursor onto the focused control with MoveMouseToPosition.

Why that was not obvious: in the KOTOR 1 codebase, that warp reads as
housekeeping. It is documented as "cursor move + hit-test + hover +
active-control update", with a note that "active control lags behind the cursor
unless we explicitly set it". Both true. Neither says *omit this and keyboard
navigation is unusable*. So when deciding what a minimal KOTOR 2 slice needed,
it did not look load-bearing.

**Every KOTOR 1 workaround is a fossilised bug fix, and fossils do not announce
what they are for.** The cursor warp, the click-simulation activation path, the
overlay-Esc latch, the DirectInput reacquisition fixes — each encodes a
behaviour fought once already. On KOTOR 2 the bug gets rediscovered BEFORE it is
recognised that the fix is already owned. Budget the port accordingly: the
addresses are the cheap half.

## THE METHOD: port whole subsystems, verify every constant first

**Decided 2026-07-31, after the minimal-slice approach cost four test rounds to
rediscover a countermeasure KOTOR 1 already had.** This supersedes the
"start minimal and grow" instinct. Do not repeat it.

**Do NOT build reduced KOTOR 2 paths that omit KOTOR 1 logic.** Omitting a
workaround does not defer its cost, it multiplies it: the bug gets rediscovered
from symptoms, debugged blind, and fixed again — and every cycle costs a test
round with the user at the keyboard.

The procedure for each subsystem:

1. Take the KOTOR 1 implementation **whole**, workarounds included.
2. Enumerate every address and offset its full call graph touches.
3. Verify each against the KOTOR 2 binary offline — vtable-slot correspondence,
   RTTI, decompiling both sides and comparing structure.
4. Port the entire subsystem, gate cleared, and test **once**.

The reasoning is about which resource is scarce. Analysis is cheap, offline, and
repeatable; test rounds need the user and are the real bottleneck. A minimal
slice optimises the developer's debugging convenience at the user's expense.
KOTOR 1's logic is known-good — the only genuine unknowns are the KOTOR 2
constants, and those are exactly what step 3 settles before anything runs.

Corollary: when a KOTOR 1 module contains something whose purpose is unclear,
**port it anyway**. The cursor warp looked like housekeeping and was
load-bearing. Assume every line earned its place until proven otherwise, rather
than the reverse.

### The hook gate is not the unit of work — the SYSTEM is

**Learned 2026-07-31, by shipping a half-system and spending a test round on
it.** This is the most expensive version of the minimal-slice mistake so far,
because it survived the whole constant-resolution effort and then reappeared at
the gate level.

With the menu constants done, one gate was cleared — the focus handler — and
that was tested. KOTOR 2 navigated correctly and spoke nothing. There was no
defect: `AnnounceNewFocusedControl` does not speak, it WRITES the pending-announce
slot, and `DrainPendingAnnounce` speaks it from the per-frame tick
(`TickGeneralMonitors` ← `acc::tick::Dispatch()` ← the `CSWGuiManager::Update`
hook). KOTOR 2 had no Update hook and its `OnUpdate` was still gated, so every
announcement was queued and discarded. Panel titles were audible only because
`SpeakPanelTitleOnFirstSight` speaks directly.

Half of a well-tested mechanism behaves like a bug. That is worse than the
feature being fully off, because it invites debugging code that is fine.

**The rule: frontload everything offline; take the first real test only when the
FULL system exists** — not the full constant set, the full system, meaning every
hook it needs installed and every gate it needs cleared.

Before proposing any KOTOR 2 test, trace the feature end to end — signal in,
state written, tick that reads the state, speech out — and confirm every hop has
BOTH a hook and a cleared gate. That is an offline question with an offline
answer. Discovering it from silence in game is pure waste.

Specifically for anything announce-shaped, the producer/consumer split is the
trap: the announce paths are documented as three producers and two dedups, and
the consumer is always the tick. A hook set without the tick hook cannot speak.

### The counter-corollary: a KOTOR 2 workaround is a fossil too

**Learned the hard way 2026-07-31, by regressing working behaviour.**

KOTOR 2's cursor warp was written as an OS-level `SetCursorPos` because
`MoveMouseToPosition` had no KOTOR 2 address yet. When that address was found,
it looked obvious to swap the engine call back in — THE METHOD says prefer
KOTOR 1's known-good line over a reimplementation. That reasoning was wrong and
the swap regressed menu navigation to the exact bug the function exists to fix.

The address was right. The assumption was that the two functions do the same
thing, and they do not: **KOTOR 2's `CExoInput::SetMousePos` does not move the
OS cursor**, it writes only the engine's own copy, which the engine overwrites
from the true mouse on the next frame. KOTOR 1's moves the real pointer.

So the rule cuts both ways. A KOTOR 2 divergence that exists because someone
MEASURED a behavioural difference is itself a fossilised bug fix, and the fact
that the KOTOR 1 address later becomes available is not evidence that the
divergence is obsolete. Before replacing KOTOR 2-specific code with the KOTOR 1
line, ask what measurement produced it — and if the answer is in the comments,
believe them.

Practical test for telling the two cases apart:
- KOTOR 2 code written because an address was MISSING → replace it once the
  address exists.
- KOTOR 2 code written because a behaviour was MEASURED → the address changes
  nothing; leave it alone.

The cursor warp reads like the first and is the second. Its comments said so;
they were read as "temporary" when they were describing a KOTOR 2 fact.

One diagnostic note still worth keeping:

- **Identical failures mean the cause is untouched.** Three fixes failed in
  exactly the same way while hypotheses were refined (write the coordinates →
  also re-run the hover hit-test → convert the coordinate space). One log line
  showing the manager's cursor settled it immediately: the field read the same
  value regardless of what was written, because the engine re-derives it from
  the real mouse every frame. Different failures mean progress; identical ones
  mean measure instead of theorise.

## Hook status

The first hook (`CSWGuiPanel::SetActiveControl` → menu focus announce) is
installed and working. The rest are still gated off by
`acc::game::HandlerEnabled()`, for two reasons found by trying. Neither is a
reason to slow down — but each has to be cleared per hook, so "enable them all"
is not a single step.

**1. Handlers are not self-contained.** `OnSetActiveControl` calls
`IsLoadingSaveGame`, which dispatches through
`CServerExoApp::GetLoadFromSaveGame` — an address that resolves to **0** on
KOTOR 2. Installing that hook calls a null pointer on the first focus change.
With ~230 addresses and ~470 offsets still unresolved, most handlers have a
chain like that somewhere in them. This is what `acc::game::HandlerEnabled()`
now guards: every hook handler default-denies on KOTOR 2, and a handler is
cleared by replacing that call with an explicit branch once its whole
read/call chain is ported. `grep -c "HandlerEnabled()"` counts what is left.

**2. KOTOR 2's GUI code is compiled UNOPTIMISED, so hook designs do not
transfer.** Its `SetActiveControl` opens with a textbook frame — `PUSH EBP /
MOV EBP,ESP / SUB ESP,0x8 / MOV [EBP-8],ECX` — and every subsequent access
reloads `this` from `[EBP-8]`. KOTOR 1's equivalent is optimised and keeps
`this` in a register, which is why our hook takes it from EDI/ESI mid-function.

So a KOTOR 2 hook cannot reuse the KOTOR 1 cut point, cut length or register
sources. Each needs its own listing read (`PrintListing.java`) to pick a safe
cut of relocatable instructions and decide where its arguments actually live —
for `SetActiveControl` that is `panel = [EBP-8]`, `newControl = [EBP+8]` after
the prologue at `0x0040EC09`.

Note this also makes KOTOR 2 *easier* to read and *harder* to hook: unoptimised
code gives cleaner decompiles (which is why the offsets came out so fast) but
frame-relative arguments instead of register ones, and `esp+X` parameter
sources are the one KPatchManager feature with a known bug.

## Steps

1. **Game-identity seam.** *(done 2026-07-31)* `engine_game.{h,cpp}` owns
   "which game, which build", detected from the game image's PE link timestamp
   — safe from static init and DllMain, the same constraint that shaped
   `engine_rebase`. `engine_rebase` now consumes it rather than detecting
   separately. Logged as the first line of the startup snapshot
   (`Game.Identity title=… build=…`).
2. **Load-and-log on K2.** K2 hashes in the manifest, a minimal
   `kotor2.hooks.toml`, everything else gated off. Proves the framework
   end-to-end before any mass change.
3. **Offsets go runtime.** *(done 2026-07-31)* `engine_offsets_select.h`
   introduces `Same` / `Pick` / `Todo` / `Kotor1Only`; 246 constants in
   `engine_offsets_fields.h` plus 255 scattered across 43 other files were
   converted. The strays were marked **in place**, not relocated — a named
   constant next to the subsystem that reads it is good cohesion; what was
   missing was a marker saying "engine-version-dependent", and the marker is
   greppable wherever it lives.
4. **Populate K2 values.** *(in progress)* See the coverage table below.
5. **Feature-gate the K1-only modules**, then walk the pillars up.

## Coverage (2026-07-31, end of second port session)

Struct offsets — `acc::off`:
- `Todo` (K2 unknown): 402
- `Same` (verified identical): 17
- `Pick` (verified different): 78
- `Kotor1Only` (no K2 counterpart): 13

Addresses — `acc::addr`:
- `R` (K2 unknown, resolves to 0): 199
- `Pick` (.text/.rdata known): 65
- `PickGlobal` (.data known): 4
- `TodoGlobal` (.data unknown, resolves to 0): 10
- `Kotor1Only` (no K2 counterpart): 2

`grep -c "Todo("` and the `R(` count are the remaining-work counters.

Menu subsystem specifically (the `port_worklist.py` invocation in WHERE TO
RESUME): 115 constants used, **110 resolved, 5 unresolved** — down from 79 at
the start of the second session. None of the 5 blocks menu reading or
navigation; see "The last 5" below.

### The cross-check that makes the seeded database usable

On every field and pointer the two databases share, upstream's **KOTOR 1**
column matches ours exactly — offsets 0x4, 0x8, 0x8c, 0x90, 0x9c, 0xa2c, 0xa74,
and globals 0x7A39F4, 0x7A39FC, 0x7A3A08, 0x7A3A28. Two independent
reverse-engineering efforts agreeing on the column we can verify is what earns
trust in the column we cannot.

The structural predictions held exactly:
- `CGameObject` (shallow root) — unchanged
- `CSWSObject` — uniformly +4
- `CSWSCreature` — uniformly +0x724
- the whole `.data` globals block — uniformly +0x2A1AA8, i.e. relocated intact

## The GUI hook set — verified by decompiling both games

The three hooks all menu accessibility is built on now have KOTOR 2 addresses,
each confirmed by decompiling the KOTOR 1 and KOTOR 2 functions and comparing
structure — not inferred from a delta or a single witness.

- **`CSWGuiPanel::SetActiveControl`** (focus signal) — `0x0040A630` →
  `0x0040EC00`. Found by vtable slot: 71 classes inherit it, all in the same
  slot, all agreeing. Both decompiles show the same active-control comparison,
  the same focus-change virtual fired on old then new, the same gui-sound call.
- **`CSWGuiManager::HandleInputEvent`** (input dispatcher) — `0x0040C8E0` →
  `0x00410AA0`. Same `this->field_0x68 = code` write, same `value == 0`
  early-out, and a switch whose case values and axis-to-direction translations
  match byte for byte.
- **`CSWGuiManager::Update`** (per-frame tick) — `0x0040CE70` → `0x004113A0`.
  Both take a float and both read `panels.size` (+0x8c) as their first action,
  which is exactly what the KOTOR 1 hook point does.

Also identified: `CSWGuiManager::HitCheckMouse` → `0x00411030`.

### Panel-internal offsets: why they are the expensive part, and what cracked them

Panel-internal offsets — a named child control or a cached handle at a fixed
position inside one huge panel struct — follow no delta rule at all. Measured
witness: `CSWGuiPortraitCharGen::OnPanelAdded` stores the same
`rand()%300 * 0.01 + 1.0` float at +0x1230 in KOTOR 1 and **+0x1ce8** in
KOTOR 2, a shift of 0xAB8 inside one class. Panels embed hundreds of controls
and every one that grew pushes everything after it. Worse, the drift is not even
monotonic: KOTOR 2's map panel drops three controls, so its arrow buttons land
*lower* than KOTOR 1's. "K2 should be bigger" is not a valid sanity check.

**What works: the constructor, matched by .gui tag.**

A panel's constructor builds every embedded control in declaration order and
binds each to its layout tag — `InitControl(panel, &member, "BTN_ARRR")`. The
tag is the identity; the offset is whatever it is. One decompile per panel
yields its whole control layout, and no arithmetic is involved.

Constructors are not virtual, so the slot map does not reach them. Find them
with `tools/re-scripts/vtable_xrefs.py`, which scans .text for the class's
vtable address as an immediate: a constructor stores it into `[this]`, and
apart from the destructor almost nothing else mentions it. Two hits per class,
constructor first.

Signs the reading is right, all of which held: the label stride is 0x140 in
KOTOR 1 and 0x148 in KOTOR 2, the button stride 0x1c4 and 0x1d0, the listbox
0x2e0 and 0x2f0 — that last one matching the +0x10 `CSWGuiListBox` growth
measured independently from `HandleInputEvent`. Consecutive controls should sit
exactly one stride apart in both games.

**Constructors also settle plain fields**, not just controls, because they
initialise them — and distinctive initialisers are what pin the alignment.
`CSWGuiKeyMapButton` fell to a nine-field write sequence containing two -1s;
`CSWGuiInGameItemEntry` to a 0x7f000000 "empty" sentinel; `CSWGuiInGameEquip`'s
trailing block to the same sentinel in the same position within six writes.

**Routes tried and found weaker:**

- *The destructor*, to recover the embedded-member layout in one pass. Vtable
  slot 0 is the scalar-deleting-destructor **thunk** in both games, so the real
  destructor is one CALL further in, and it only covers members with non-trivial
  destructors — not the plain `ulong` handles.
- *Live observation* (`PanelProbe` dumps), which is how most of these were
  established for KOTOR 1. Accurate and fast per panel, but it spends test
  rounds, which is the resource THE METHOD exists to protect.

### The last 5 in the menu subsystem

None of these blocks menu READING or NAVIGATION — that path is complete. Each is
listed with why it is still open and where to resume.

- **`kCGuiInGameReplyCountOffset` / `kCGuiInGameReplyTextArrayOffset`** (KOTOR 1
  +0x114 / +0x118). `CGuiInGame` has NO vtable in KOTOR 2's RTTI, so neither the
  class map nor the slot map reaches it, and no `CGuiInGame` address is resolved
  for KOTOR 2 yet. The KOTOR 1 call chain is
  `CSWSDialog::SendDialogReplyNode` → `HandleDialogReplies` → `ShowDialogReplies`
  → `SetReplyData`, and `UpdateDialog` is reached from `CSWGuiDialog::SelectReply`
  — none of them virtual under their own name. `CSWSDialog` DOES have a KOTOR 2
  vtable (0x0099A080), so `vtable_xrefs.py` gives its constructor as a foothold.
  Note the slot map's rows for `CSWGuiDialog::SelectReply` map all four dialog
  classes to one KOTOR 2 address while KOTOR 1 has two distinct ones — that
  alignment is suspect; do not trust those rows without checking.
  These belong to the DIALOG pillar rather than menus, so they can also simply
  wait for that subsystem's turn.
- **`kPartyPortraitNpcSlotOffset`.** KOTOR 2's element has four trailing dwords
  where KOTOR 1 has three, so position alone cannot pick between 0x470 and
  0x474, and neither game's `OnPanelAdded` writes it. Deliberately parked: its
  only consumer resolves through `kCompanionNamesBySlot`, a table of KOTOR 1
  story characters KOTOR 2 shares none of, so the offset buys nothing until that
  path has a KOTOR 2 name source. See also the 12-vs-9 roster note at
  `kPartyRosterSlotCount`.
- **`kAddrManagerLMouseDown` / `LMouseUp`.** Activation only, so nothing that
  reads or announces needs them. Each has exactly ONE caller in KOTOR 1 —
  `CClientExoAppInternal::PerformLButtonDownAction` / `...UpAction`, called from
  adjacent sites in `CClientExoAppInternal::HandleInputEvent`. That class is
  absent from the RTTI slot map, so the forwards-from-callers method that
  cracked `MoveMouseToPosition` needs one more hop up (`HandleInputEvent`'s own
  callers are `PlayBackInputEvents` and `ProcessInput`).
  **Do not retry this**: a structural search for "two functions called
  adjacently, each making one GuiManager-mediated call" was written and does NOT
  find them, because only ONE of the KOTOR 1 pair reaches the manager through
  the global.

### Divergences that are CODE, not constants

Found while resolving the above. These will not show up in any worklist count,
and each will silently misbehave on KOTOR 2 if the KOTOR 1 logic is reused:

- **The upgrade slot-type table index.** Stride is 0xc in both, but KOTOR 1
  packs FOUR slot types per category and indexes
  `((custom_value - 4) + category * 4) * 0xc`, while KOTOR 2 packs SIX and
  indexes `slot * 0xc + (category - 1) * 0x48`. Swapping only the base address
  reads the wrong entry. Needs an `acc::game::IsKotor2()` branch at the call
  site.
- **The party roster is 12 slots on KOTOR 2, 9 on KOTOR 1.**
  `kPartyRosterSlotCount` must stay `constexpr` (it sizes a real array), so this
  is documented at the declaration rather than converted. The current bound
  truncates on KOTOR 2 rather than reading out of range, which is the safe
  direction, but KOTOR 2 needs its own roster/name table.
- **Panels lose members.** KOTOR 2's map panel drops the compass label,
  BTN_RETURN and BTN_PRTYSLCT; its journal drops the quest-items button; its
  equip panel drops both party-portrait buttons and moves the prev/next arrows
  to the end of the struct. Marked `Kotor1Only` where a constant existed, but
  any code that ASSUMES those controls are present needs a look.

**`MoveMouseToPosition` is NOT yet found.** Its KOTOR 1 body is four
statements (store x/y, `CExoInput::SetMousePos`, `HandleMouseMove`), but none
of the three KOTOR 2 callers of the apparent `HandleMouseMove` matches its
shape, so KOTOR 2 reaches the hit-test by a different route. It is needed for
click-simulation (activation), not for reading and announcing, so it can wait —
but do not guess it: activation through a wrong address is how the
`SetActiveControl` crash class happened on KOTOR 1.

### What the decompiles said about layout

Worth more than the addresses themselves, because it constrains everything else:

- **`CSWGuiManager` did not grow.** Its input-code field is at +0x68 in both
  games, and `HitCheckMouse` reads the panel arrays at +0x88/+0x8c and
  +0x94/+0x98 — identical to KOTOR 1. So foreground-panel resolution, which the
  whole navigation chain depends on, carries over unchanged.
- **`CSWGuiPanel` and `CSWGuiControl` both shift +4** in that region:
  `active_control` 0x1c→0x20, `manager` 0x18→0x1c, the control's gui-sound byte
  0x55→0x59.
- **The engine input codes are identical.** `HandleInputEvent`'s switch uses the
  same case values and produces the same translated direction codes in both
  games, so `engine_input.h`'s InputIndex constants should carry over as-is.

### Panel identity: all of it, from RTTI class names (2026-07-31)

Every vtable-identity constant in the codebase now has a KOTOR 2 value except
two, and none of it needed a decompile. The route is:

1. Look the KOTOR 1 address up in Lane's Ghidra XML — vtables carry a
   `<Class>_vtable` SYMBOL, so the address yields a class NAME.
2. Look that name up in `docs/llm-docs/re/k2/k2-vtables.csv`.

23 of 25 resolved that way, including the nine title-screen Options sub-screens
(whose KOTOR 1 values had been captured from a live probe and carried no name
until this lookup gave them one). The convention was checked before trusting it:
for each candidate, `vtable_va - 4` holds the complete-object locator and slot 0
points into `.text`, which is what makes `vtable_va` the pointer an object
actually stores.

Two panels are **absent from KOTOR 2**, and this is measured rather than
assumed: diffing the 110 `CSWGui*` classes Lane's KOTOR 1 database names against
the 122 in KOTOR 2's RTTI leaves exactly two on the KOTOR 1 side —
`CSWGuiQuestItem` (the journal's quest-items sub-screen) and `CSWGuiScriptSelect`
(the character sheet's combat-behaviour picker). KOTOR 2's exe contains no
`questitem` or `scriptselect` string either. They are marked with a new
`acc::addr::Kotor1Only()`, mirroring `acc::off::Kotor1Only()`: same run-time
behaviour as a bare `R()`, but it keeps the remaining-work counter honest.

KOTOR 2 *adds* fourteen GUI classes — the workbench item-creation screens, a
death display, a legal screen, the three iOS gamepad panels, a tutorial box.
Those are surfaces the KOTOR 1 code has nothing to say about yet.

### The deltas ACCUMULATE through embedded sub-objects

The single most important structural finding so far, because it invalidates the
obvious shortcut.

The seeded database suggested "a constant delta per class". That is true within
a flat class, but KOTOR 1's GUI classes are built by embedding whole
sub-objects, and each one that grows shifts everything after it. Observed in
`CSWGuiLabelHilight::Draw`, decompiled in both games:

- `CSWGuiControl` base: 0x5c → **0x60** (+4)
- the embedded border: 0x74 → **0x78** (+4)
- so `CSWGuiLabel`'s text sub-object: 0xd0 → **0xd8** (+8, not +4)

Both borders confirm it: the label's own border moved 0x5c → 0x60, and the
hilight's second border sits at +0x148.

So **do not extrapolate +4 down a class**. The correct model is: sum the growth
of every base and embedded member that precedes the field. Anything at or below
+0x14 in `CSWGuiControl` is unshifted (the extent is proof); past the insertion
point the delta is +4 per grown object crossed, not +4 total.

Left as `Todo` because of this: `kLabelTextOffset` / `kLabelStrRefOffset` /
`kButtonTextOffset` / `kButtonStrRefOffset` and the `kTextObject*` internals.
The arithmetic says 0xe8 → 0xf0 and 0xf0 → 0xf8 for the label, but that assumes
`CSWGuiText`'s own layout did not change, which has not been checked. These
feed every spoken string, so they want observing rather than deriving.

### The shared control classes are done (2026-07-31)

Everything the whole GUI is built out of — `CSWGuiControl`, `CSWGuiListBox`,
`CSWGuiSlider`, `CSWGuiButtonToggle` — now has observed KOTOR 2 offsets. The
method that produced them scales, and is worth stating because it is cheaper
than it looks:

`k2-vtable-slots.csv` maps KOTOR 1 virtual methods to KOTOR 2 addresses **by
name**. So for any offset, find a KOTOR 1 virtual that touches the field, look
its KOTOR 2 twin up in that file, decompile both, and read the offset off the
matching statement. No searching, no guessing which function is which.

What that produced:

- `CSWGuiControl` parent / tooltip strref / tooltip string: **+4** each
  (0x14→0x18, 0x24→0x28, 0x28→0x2c), from `DisplayToolTip` (slot 36), with the
  parent corroborated independently by `Load` (slot 18) storing its
  `Obj_ParentID` lookup at this+0x18. The insertion point is now pinned between
  +0x10 and +0x14: the extent at 0x4..0x10 is unshifted, +0x14 is not.
- `CSWGuiListBox` controls / bit_flags / items_per_page / selection_index /
  top_visible_index: **+0x10** each, from `HandleInputEvent` (slot 15). Every
  landmark of the KOTOR 1 version reappears at exactly +0x10 — the -1 test on
  selection_index, the 0x40 / 0x200 / bit-12 flag tests, `size -
  items_per_page`, and the closing `controls.data[selection_index]` dispatch.
- `CSWGuiSlider` max / cur: **+4**, from `HandleInputEvent` (slot 15).
- `CSWGuiButtonToggle` state: **+0xc**, from `Load` (slot 18) masking the
  "ISSELECTED" .gui byte into bit 0.
- `CSWGuiListBox::SetSelectedControl` (an address, not an offset) fell out of
  the same listbox decompile: KOTOR 2 calls it at all five of the places
  KOTOR 1 does, with the same `(index, playSound)` pair.

Note the three different deltas — +4, +0xc, +0x10 — in four classes that all
derive from the same base. That is the accumulation rule below, in evidence.

Two incidental cross-checks worth recording, because they cost nothing and
constrain a lot: KOTOR 2's slider reads its extent width/height at +0xc/+0x10,
exactly as KOTOR 1 does (so `kControlExtentOffset` really is `Same`), and its
`CSWGuiManager::HandleInputEvent` reads the same +0x64 / +0x68 / +0x72 /
+0x88 / +0x8c / +0x94 / +0x98 the KOTOR 1 one does (so the manager really did
not grow).

### Values that are derived rather than verified

Flagged in the code, listed here so they are not forgotten:
- `kWaypointPositionOffset` — `CSWSObject.Position` read on a waypoint, so it
  should inherit the +4 shift, but the database says nothing about waypoints.
- `kScriptVarTableOffset` (+0x100) — left `Todo`. `CSWSObject`'s shallow fields
  shift +4, but +0x100 is far enough down the class that the same shift cannot
  be assumed. Save-persistent mod state on KOTOR 2 depends on getting this
  right.

Controller support comes after the mod runs on K2 at all — see below.

## The pass-through hazard (fixed, worth understanding)

Before step 1, `acc::addr::R()` returned the *reference value* for any
unrecognised build. On KOTOR 2 that would have handed out 267 KOTOR 1 addresses
pointing into unrelated K2 code — silent jumps into the middle of other
functions, which is precisely the failure mode `engine_rebase.h` warns about
for stale addresses.

`R()` now returns **0** for KOTOR 2. That faults recognisably at address 0, and
the ~12 call sites already wrapped in `acc::addr::Ok()` degrade gracefully with
no changes. But `Ok()` is not what makes K2 safe — it covers 12 of 267 sites.
**Engine-touching code must be gated on `acc::game::IsKotor1()` before it
runs.** That gating is step 5, and until it exists K2 must not be given a hook
set beyond the minimum.

## Controller support

KOTOR 2's native pad support came from Aspyr's iOS/Android port, and the RTTI
names show it plainly: `CSWGamepadMenuIos`, `CSWGuiActionMenuIos`,
`CSWGuiHelpPanel` (all nested in `CSWGuiMainInterface`), plus
`CSWGuiControllerLossBox` and a `CExoInputeventDesc2ButtonAxis` input
descriptor. It is a **parallel UI**, not a remapping of the keyboard one — so
it is not something the K1 nav-chain work ports into directly.

Two facts that will shape the design:

- The exe does **not** import XInput. The pad goes through DirectInput. If we
  poll the pad ourselves we would be a second reader of the same device, and
  both we and the engine would see every press — the same double-fire problem
  documented for keyboard polling in `controller-mod-techniques.md` §4.
- Our mod adds many keyboard-only affordances (discovery cycling, the unified
  action menu, interact hotkeys). These need pad bindings that do not collide
  with what K2 already binds.

`docs/controller-mod-techniques.md` is the existing hand-off note on our input
pipelines and is the right starting point, but it describes K1 surfaces.

## The overlay pause was dead on KOTOR 2 (found + fixed 2026-08-08)

`CClientExoApp::SetPausedByCombat` had no K2 address. It was written as the K1
address through plain `R()`, which returns 0 on K2, so **every**
`BeginOverlayPause` / `EndOverlayPause` on K2 called through a null pointer and
the site's own `__except` swallowed it. The log said
`fault in overlay-pause SetPausedByCombat` — which reads as "the engine
refused", not "we never found the function", and that is why it sat unnoticed
through a whole test round.

Everything that asks the world to freeze was affected — the examine view, the
combat queue and the unified action menu. It surfaced through the action menu
because the pad's left trigger made that menu the pad's main surface: with the
world never actually paused, `WorldIsPaused()` stayed false and every fire took
the "world running, out of combat" branch and closed the menu, so A could not
queue a second action.

**Resolved:**

- facade `CClientExoApp::SetPausedByCombat` — **0x00740350** (K1 0x005edc20)
- internal `CClientExoAppInternal::SetPausedByCombat` — **0x0079BF40**
  (K1 0x005f2e10)

Method, for the next address of this shape: the internal is the only small
function in the K2 binary that writes K1's three fields at the *same* offsets
(`0x37c |= 4`, source byte at `0x388`, requested state at `0x380`), gates on
`gui_in_game`'s own flag, and forks into PauseRumble + `SetSoundMode(paused)`
versus UnpauseRumble + `SetSoundMode(running)`. It was reached by listing the
K2 functions that call the already-known `SetSoundMode` (0x0070BC60) **twice** —
five candidates out of 44 callers — and reading the shortest. `ret 0xc` on both
sides confirms the `(int, byte, int)` tail. The facade is its 37-byte forwarder
through `this->internal` at +0x4, the same shape K1's has.

Two K2-only additions in the internal, neither affecting us (we pass source 4):
an early accept when source == 0x0d, and a skip of the rumble/sound work when
source == 3.

**The general lesson:** a `Pick`-less address is not a safe default on K2, it is
a null call, and wrapping the call in `__try` converts a missing address into a
plausible-looking engine refusal. Both `SetPausedByCombat` call sites now check
`acc::addr::Ok()` first and log "unresolved on this build — the world will NOT
pause". Worth doing at every K2 call site whose address might still be a bare
`R()`.

## Sources

- `kotor2-port-feasibility.md` — the original measurement (sigscan 0/213) and
  why it will not improve.
- `docs/llm-docs/CLAUDE.md` — which modules are K1-story-only, which minigames
  do not carry.
- `docs/controller-mod-techniques.md` — input pipelines, nav chain, activation.
- `archiev/refactoring/END-REPORT.md` — what the pre-K2 refactoring bought.
