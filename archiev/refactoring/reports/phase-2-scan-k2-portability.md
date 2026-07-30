# Phase 2 scan: the KOTOR 2 portability lens

> **Stale line references (2026-07-29).** This is a point-in-time scan.
> C8 has since split `engine_offsets.h` into `engine_offsets_types.h` /
> `_addresses.h` / `_fields.h` / `_values.h`, so every `engine_offsets.h:NNN`
> citation below now points at the aggregator, which holds no constants.
> The constant names are all still valid — grep by name. The findings
> themselves stand; only the coordinates moved.

Scope: `patches/Accessibility/` (282 files). Question: map every K1-specific
dependency and propose where an engine-abstraction seam should go, and in
particular how `engine_offsets.h` should be partitioned for portability
rather than for tidiness.

## 0. Read this first — prior art already on record

Two documents already answer part of this question and this report must not
contradict them; it extends them with the file-level inventory they didn't
attempt.

- `docs/kotor2-port-feasibility.md` (status: PARKED, 2026-07-25). Measured
  fact: `kdev sigscan`, which relocates K1 addresses onto a byte-identical
  relinked K1 build (the Allard Russian exe: 212/216 `.text` addresses
  resolved), gets **0 of 213** against Steam `swkotor2.exe`. Reason: K2 on
  Steam is the Aspyr 2015 rebuild, an 11-years-later recompile by a different
  studio, `.text` 70% larger. This is a property of the problem, not a tuning
  gap. The doc's conclusion: sigscan contributes nothing to a K2 port; the
  reusable seam is **name lookup** (`GameVersion::GetFunctionAddress` /
  `GetGlobalPointer` / `GetOffset`, selected by exe SHA-256), which
  KPatchManager already ships but our patch does not use (confirmed below,
  finding K14); a K2 address/offset database is fresh reverse-engineering,
  and struct offsets — not function addresses — will dominate that cost
  because offsets can't be signature-matched even in principle.
- `docs/kdev-design.md` (`kdev sigscan` entry, ~line 140) — same measurement,
  same numbers, plus: `engine_offsets.h` currently carries **280 address
  references / 264 distinct addresses**, 216 of them in `.text`.

This report's job is the layer beneath that: given those conclusions, what
exactly is in `engine_offsets.h` and the rest of the patch that would need to
move, and how should the file be cut.

## Inventory

### K1 — `acc::addr::R()` / `engine_rebase.h`: what it is and what it is not

`patches/Accessibility/engine_rebase.h:1-56`. `R(referenceVa)` maps a
reference-build (Steam/GoG 1.0.3) `.text` address onto whatever build is
actually running, via a generated table (`engine_rebase_table.inc`, built by
`kdev sigscan`). It is the identity function on the reference build.

This is a **within-K1** seam: it exists to carry the *same compiled bytes,
relocated* onto other K1 builds (currently the Allard Russian translation).
It is not a multi-game seam and was never meant to be one — nothing about its
design anticipates a structurally different binary. Concretely:

- It only rebases `.text`. Per its own header comment (`engine_rebase.h:16-19`):
  "`.data` addresses are byte-stable across these builds and are left alone."
  That assumption is true only because the Allard build is the *same source
  compiled with the same layout tool*, just re-linked. It does not hold for
  K2: a fresh recompile can and will move `.data` globals too. Two constants
  in `engine_offsets.h` already take the "`.data` needs no rebasing" shortcut
  — `kAddrRulesGlobal` (`engine_offsets.h:583`) and `kAddrTlkTablePtr`
  (`engine_offsets.h:810`) are bare `constexpr uintptr_t`, not passed through
  `R()`. Correct for K1 variants; silently wrong if this file were ever
  compiled against a K2 address table without re-auditing every bare
  constant.
- It resolves *function/vtable addresses only*. It has no notion of struct
  field offsets at all — those are separate bare `constexpr size_t` constants
  elsewhere in the same file (see K3/K4) with no indirection whatsoever.
- Cost/verdict: **not a sufficient seam for K2, and not designed to be one.**
  It is sufficient, and doing its job well, for what it was built for
  (K1 build variants). Nothing about it should change for that purpose.

### K2 — `hooks.toml` / `allard.hooks.toml`: the shape of the problem

`patches/Accessibility/hooks.toml:1271` lines, `allard.hooks.toml:443` lines.
Per constraints, no address or byte-pattern content is discussed below beyond
its shape.

Each `[[hooks]]` entry pins an `address`, an `original_bytes` signature (the
patcher's `Trampoline::VerifyBytes` refuses to write if the live bytes don't
match — this is the framework's existing fail-safe, not something to touch),
and is gated by `[metadata].target_versions` — a list of exe SHA-256 hashes
(`hooks.toml:7-12`). `allard.hooks.toml` is a **second, sibling hooks file**
carrying the same detours rebased for the Allard exe hash, selected instead
of `hooks.toml` purely because its SHA doesn't match `hooks.toml`'s
`target_versions` list.

This is good news structurally: **the file-selection mechanism a K2 port
would need already exists and is already exercised**, by exactly this
Allard/reference split. A future `k2.hooks.toml` with K2 SHAs and K2
addresses/byte-patterns is the same pattern one more time — no framework
change needed at the hook-selection layer. What a K2 ports needs instead is
entirely fresh content for that file (new addresses, new byte signatures,
possibly a different set of hookable functions if the K2 recompile inlined or
restructured things K1 didn't) — RE work, not architecture work.

Cost: zero to prepare for now (the mechanism is already generic). All cost is
in the eventual K2 RE pass itself.

### K3 — `engine_offsets.h` quantified

1820 lines, included by **86** other files in `patches/Accessibility/`
(grep-verified; this is the file whose partition this report was asked to
resolve). Breakdown by construct:

- **103** constants routed through `acc::addr::R()` — function addresses and
  vtable addresses. These already have a build-selection seam (K1); porting
  to K2 means replacing the *values*, not the *mechanism*.
- **214** `constexpr size_t k*Offset` struct-field-offset constants — bare
  numeric literals, no indirection of any kind. Every call site does
  `*(T*)(base + kFooOffset)` directly against the constant.
- **2** bare `.data` pointer constants bypassing `R()` on purpose (K1).
- A handful of `struct` definitions (`CExoString`, `Vector`, `CExoArrayList`)
  and `typedef`s for `__thiscall` function-pointer signatures.

This confirms the feasibility doc's prediction directly: **struct offsets
(214), not addresses (103), are the bulk of the file**, and offsets are
exactly the category that cannot be signature-matched even against a
byte-identical K1 rebuild, let alone a K2 recompile.

### K4 — struct field offsets: which class survives, which doesn't

All 214 are numeric offsets into engine structs (`CSWGuiButton`,
`CSWSCreature`, `CSWGuiListBox`, `CSWGuiInGameEquip`, `CSWSItem`, etc.),
verified against Lane's Ghidra SARIF for the K1 binary — see the file's own
comments, e.g. `engine_offsets.h:674-696` (`CExoArrayList` container shape,
`CSWGuiPanel.controls` at `+0x20`) or `engine_offsets.h:1149` (attribute
totals at `+0x34`).

None of these offsets are protected by any porting seam today — a struct
gaining or losing one field anywhere before the field we read shifts every
subsequent offset in that struct. Concretely, per finding class:

- **Near-certain to differ**: anything on a struct Obsidian is known to have
  extended for K2 gameplay — inventory/equip slots (K2 added the "hide" and
  belt slot changes and reordered UI), creature stats (K2 has prestige
  classes, Influence, a different feat/power catalogue size), party UI
  (K2's larger roster and swappable companions), and by extension every
  panel struct listed in `engine_offsets.h:200-1820` (CSWGuiNameChargen,
  CSWGuiAbilitiesCharGen, CSWGuiFeatsCharGen, CSWGuiPowersLevelUp,
  CSWGuiInGameEquip, CSWGuiStore, CSWGuiInGameJournal, …) since K2's GUI
  screens are known to be reworked, not just re-skinned (new prestige-class
  flow, Influence sub-screen, HK-47/T3 unique interactions).
- **Plausibly stable, worth checking first before assuming they moved**:
  low-level engine primitives that are format-level truths of the whole
  Aurora family rather than K1-build facts — `CExoString { char*, uint32 }`
  (`engine_offsets.h:802-805`), `CResRef` = 16 bytes
  (`kResRefSize`, `engine_offsets.h:326`), `CExoArrayList<T>` layout
  (`data/size/capacity`, `engine_offsets.h:771-775`),
  `CExoLinkedList`/`CExoLinkedListInternal`/`CExoLinkedListNode`
  (`engine_offsets.h:1039-1069`). These are container/primitive shapes NWN
  also uses; a K2 RE pass should verify them first since a hit here is much
  cheaper than re-deriving them from scratch, but "plausible" is not
  "verified" — they must still be checked against a K2 SARIF/decompile, not
  assumed.
- **Speculative even on K1** and therefore doubly unsafe to carry forward:
  the combat action-type byte enum (`kActionTypeAttack` etc.,
  `engine_offsets.h:1078-1089`) is explicitly commented "Inferred... matches
  typical enum-by-declaration patterns... Validate via DumpBytes" — this was
  never confirmed even for K1, so it is not a K2-porting risk so much as a
  standing K1 TODO.

Cost: this is the expensive part of any future K2 port, exactly as
`docs/kotor2-port-feasibility.md` already concluded — "budget it as fresh
reverse-engineering." Nothing here is cheaper to do now; there is no K2
binary or SARIF to check offsets against yet.

### K5 — vtable indices for downcasts: the one plausibly-portable vtable fact

`engine_offsets.h:11-22`: `kVtableAsLabel = 20`, `kVtableAsLabelHilight = 21`,
`kVtableAsButton = 22`, `kVtableAsButtonToggle = 23` — indices into
`GuiControlMethods`, the base virtual-method table shared by every
`CSWGuiControl` subclass, used for RTTI-style downcasts. Unlike the vtable
*addresses* in K6, these are ordinal positions in a base class's method
table, not link-time addresses — if K2 didn't insert new virtual methods
ahead of these four in `CSWGuiControl`'s hierarchy, the indices could hold
unchanged even though every other GUI fact in the file needs re-deriving.

This is worth flagging specifically as a **cheap, high-value first check**
whenever K2 RE starts: verifying 4 vtable-slot indices against a K2 SARIF is
minutes of work and, if it holds, removes one whole class of re-derivation
from the critical path. Not verifiable now (no K2 database in the repo) —
recorded here so it's the first thing tried, not re-discovered.

### K6 — vtable addresses and identity-by-vtable checks

Distinct from K5: full link-time vtable *addresses*, used for object-kind
identification where no downcast accessor exists. Examples:
`kVtableSlider` (`engine_offsets.h:121`), `kVtableListBox` (`:131`),
`kVtableCSWGuiButton` (`:142`), `kVtableKeyMapButton` (`:152`),
`kVtableEditbox`/`kVtableSaveGameEditbox` (`:187,194`), plus a long tail of
per-panel vtables (`kVtableCSWGuiNameChargen`, `kVtableCSWGuiClassSelection`,
`kVtableCSWGuiPortraitCharGen`, `kVtableCSWGuiFeatsCharGen`,
`kVtableCSWGuiPowersLevelUp`, `kVtableCSWGuiStore`,
`kVtableCSWGuiStoreItemEntry`, `kVtableCSWGuiInGameItemEntry`,
`kVtableCSWGuiJournalItemEntry`) and the nine title-screen Options
sub-screen vtables in `kOptionsSubScreenVtables`
(`patches/Accessibility/engine_panels.cpp:377-387`).

These are all `acc::addr::R()`-wrapped (K1-variant-safe already) but are
100% link-time facts of the K1 binary — every single one needs a fresh value
for K2, and there is no shortcut: a relinked/recompiled binary's vtable
addresses bear no relationship to K1's. Grep across `.cpp` files finds
**92 vtable-identity comparisons across 21 files** (`menus_chain.cpp`,
`engine_panels.cpp` (27 of the 92), `engine_reads.cpp`, `menus_extract.cpp`,
`menus_pazaakdeck.cpp`, `menus_listbox.cpp`, etc.) — every one of those call
sites keeps working unchanged once the constant it references is repointed,
which is the right shape (comparisons are already centralized on named
constants, not scattered literals).

Cost: fresh RE per constant, same bucket as K4. Zero cost now since the
call-site pattern (compare against a named constant) is already correct and
needs no refactor.

### K7 — `PanelKind` / CGuiInGame slot table

`patches/Accessibility/engine_panels.h:15-160` (the `PanelKind` enum) and
`patches/Accessibility/engine_panels.cpp:428-473` (`kPanelKindOffsets[]`,
mapping each enum value to a byte offset inside `CGuiInGame`). The whole
table is a description of **one struct's layout in one binary** — every
offset (`0x08` for `InGameMenu`, `0x84` for `Store`, `0xa8` for
`StatusSummary`, …) is a K1 `CGuiInGame` fact. A K2 `CGuiInGame` almost
certainly has a different slot count and order (K2 added screens K1 doesn't
have — e.g. Influence — and the enum here would need new members for them,
not just renumbered offsets for the existing ones).

The `PanelKind` enum itself, and the functions built on top of it
(`IsModalPopupPanel`, `IsForegroundUiBlocking`, `HasActiveDialogPanel`, etc.,
`engine_panels.h:194-296`) are **shape-portable** — the concept "classify a
raw panel pointer into a semantic kind, then reason about kinds" carries over
to any Odyssey-family game. Only the offset table and the vtable list feeding
`IdentifyPanel` are K1-specific data behind that shape.

Cost: same bucket as K4/K6 (fresh RE), but the *code* built on top
(`engine_panels.h`'s public API) needs no redesign — it is already the right
abstraction, just backed by K1-only data today.

### K8 — `.gui`-time control IDs

Scattered `constexpr int` tables assigning numeric child-control IDs, e.g.
`menus_internal.h:179-194` (`kEquipBtnHeadId = 7`, ..., `kEquipBtnBackId = 36`
— each carries a `// TLK NNNNN` comment cross-referencing the button's
caption strref), `menus_internal.cpp:37-40` (SaveLoad), `menus.cpp:367-370`
and `menus_listbox.cpp:55-61` (Container), `menus_keymap.cpp:48-50`,
`menus_powers_levelup.cpp:37-39`, `menus_chargen_feats.cpp:32-34`,
`menus_listbox.cpp:972-1051,1312-1313` (Workbench Items/Upgrade,
ScriptSelect).

These IDs are assigned by whoever authored the `.gui` layout resource, not by
the compiler — they are a fact about K1's `equip.gui` / `saveload.gui` /
`container.gui` / etc., completely independent of the executable. A K2 port
needs its own ID table sourced from K2's `.gui` files (which may reuse some
IDs by coincidence, drop others, or renumber wholesale if the screen was
reworked — K2's equip/inventory/character screens are known to differ from
K1's, e.g. different slot sets and an Influence tab family that doesn't exist
in K1). The `// TLK NNNNN` comments riding along with several of these are
themselves K1 dialog.tlk facts (see K9) — a second, independent way these
same lines go stale for K2.

Cost: fresh RE against K2's `.gui` resources (extractable via `xoreos-tools`
/ KotOR Tool the same way K1's are), independent of any binary work. This is
content-file RE, not executable RE, and could in principle start before a K2
address database exists (K2's `.gui` files can be pulled from the install
today, no game running required) — flagged as a plausible **early, low-risk
K2 prep task** if a K2 port is ever scheduled, since it doesn't touch the
address/hook risk surface at all.

### K9 — TLK strref literals

- `kCloseButtonStrRef = 1582` (`engine_offsets.h:52`), used in
  `menus_chain.cpp:455,629` to identify the reusable "Close" button caption
  across 21 K1 panels by its *resolved, locale-agnostic* strref. This
  specific value is a K1 `dialog.tlk` row number.
- `locked_recall.h:5` references strref 1437 (the engine's generic "This
  object is locked" line) as design commentary — the mechanism (cache a
  story bark, replay once) is engine-generic; the strref this hangs off is a
  K1 dialog.tlk fact.
- `map_note_renames.h:6-9` is keyed on three literal K1 strrefs — 33413
  ("Südlicher Pfad"), 33425 ("Nordpfad"), 33461 ("Ausgang") — reused across
  multiple Dantooine modules in K1's table specifically.
- `tutorial_hints.cpp:36-56` is a ~20-row table of literal K1 Trask-dialogue
  strrefs (10326, 48330-48556) driving the Endar Spire tutorial popups —
  entirely K1 dialog.tlk content (see K10, this module is K1-only content
  too).
- `menus_internal.h:179-194` carries `// TLK NNNNN` cross-reference comments
  (31375-31383, 1580, 1582) next to the `.gui` control IDs from K8 — these
  are documentation, not executed logic, but confirm the same K1-dialog.tlk
  coupling rides along with the ID table.

None of this is protected by any indirection today — every strref is a bare
integer literal at its use site. A K2 port needs its own strref values from
K2's `dialog.tlk`, which is a different table (TSL restructured and greatly
extended K1's; string IDs are not guaranteed to carry over even for strings
that read identically, and must not be assumed to match without a diff).

Cost: same bucket as K8 — a content-file RE task (diffing/reading K2's
`dialog.tlk`, extractable via `xoreos-tools tlk2xml` the same way K1's is),
independent of the exe-address work, and startable early if ever scheduled.

### K10 — modules assuming K1-only content

These are not "K1 values that need K2 equivalents" — they are modules whose
entire *reason to exist* is one specific scripted moment in K1's story that
has no K2 analog, or a puzzle/room that simply is not in K2:

- `floor_puzzle.h:1-2` — the Rakatan Temple 3x3 floor-plate puzzle, module
  `unk_m44ab`, tags `kFloorPanel01..09`. K2 has no Unknown World temple; this
  puzzle does not exist there.
- `spectator_scene.h:3-10` — the Endar Spire "spectator battle" (module
  `END_M01AA`, tags `end_cut2_soldier*`) — K1's opening ship, which K2 does
  not have (K2 opens on the Harbinger/Peragus).
- `endar_softlock.h:1-16` — a softlock guard scoped to the same
  `END_M01AA` Endar Spire bridge sequence (`end_door16`).
- `tutorial_hints.h` / `tutorial_hints.cpp` — the Surface-1/Surface-2
  tutorial-popup system is keyed to Trask's specific dialogue tree on the
  Endar Spire (K9's strref table). K2's tutorial companion and flow (Kreia,
  Peragus, T3-M4) is completely different content.
- `map_shipped_hints.cpp:21-26` — a curated table of exact K1 module resrefs
  (`danm14ad` = Dantooine, `tar_m05aa` = Taris, `tat_m18ac` = Tatooine) and
  in-world coordinates for quest-critical positions. Every row is K1 module
  geography.

For all five, the *delivery mechanism* (module/tag match → speak a curated
line, or gate a door hint on engine feedback) is generic and would work in
K2 for K2's own equivalent moments — but the data tables are 100% K1 content
and produce zero matches (silently, harmlessly inert) if the mod is ever
loaded against K2 without new K2-specific tables. This is the "good news"
shape from the opposite direction: nothing here would misbehave on K2, it
would just never fire — but nothing here helps K2 either. A K2 port needs
new, separately-authored content modules for K2's equivalent moments (its
own tutorial system, its own quest-hint candidates, etc.), not adaptations
of these five.

Cost: N/A to "port" — these should be understood as K1-scenario content that
stays K1-only, with new K2 counterparts written from scratch after someone
has played/RE'd the K2 equivalents. Not urgent to change now; flagged so a
future K2 effort doesn't waste time trying to "generalize" what is
irreducibly K1 story content.

### K11 — minigames: turret / swoop / pazaak

All three exist in K2 too (confirmed by the task brief) but with content
changes (K2 added swoop upgrades, new tracks/bikes; Pazaak gained new decks
and characters). At the engine level all three read straight off
`engine_offsets.h`-style struct offsets:

- `minigame_turret.h:1-9` / `minigame_swoop_race.h:12-20` both poll
  `CSWCArea.mini_game` (`+0x264`) and discriminate by `CSWMiniGame.type`
  (`+0x80`: 1 = swoop, 2 = turret) — a single shared struct, offsets fully
  K1-specific, needs full re-verification for K2 (K2's swoop-upgrade system
  in particular suggests `CSWMiniGame`/`CSWMiniPlayer` likely gained fields).
- `minigame_pazaak.h:1-9` locates the live Pazaak board panel and drives it
  via the same vtable-identity + struct-offset pattern as every other panel
  (see K6/K7) — K2's Pazaak decks add card variants the K1 card-label logic
  in `pazaak.cpp`/`docs/pazaak-investigation.md` was never designed around
  (unverified how large that content delta is; flagged, not measured, here).

The *polling and announcement shape* — per-tick driver, entry/exit cue,
continuous state-delta narration — is engine-family-generic and does not
need to change. Only the offsets/vtables (K4/K6 bucket) and possibly the
card/obstacle content tables (K10-style bucket, unverified size) do.

### K12 — modules that are already engine-independent (the good news)

Files with no `#include "engine_*.h"` dependency **and** no bare hardcoded
address/offset of their own (spot-checked, see caveat): `strfmt.h` (pure
`vsnprintf`-based formatting, no includes beyond `<cstdarg>`/`<cstdio>`/
`<string>`), `announce_degrees.h`/`.cpp` (pure geometry → phrase
conversion, no includes at all beyond its own header), `strings.cpp` +
`strings_{de,en,es,fr,it,ru}.cpp` (the localization tables — pure string
data, `Get(Id)` lookup), `combat_strings.cpp`, `log.cpp`, `mod_settings_store.cpp`
(ini persistence), `hotkeys.cpp` (Win32 `VK_*` codes, not engine offsets —
persists via `mod_settings_store.h`), `intro_skip.cpp` (filesystem
rename-based movie toggle — see caveat), `prism.cpp` / `menus_speak.cpp`
(the speech backend), `core_settings.cpp` / `diag_settings.cpp`,
`update_checker_http.cpp`, `examine_view_effect_names.cpp`.

These port to K2 for free at the source level — no offsets, no addresses, no
K1-content assumptions, pure logic/formatting/persistence. This is the
concrete evidence behind the task brief's claim that some subsystems
"port for free."

**Caveat, found while checking this list**: naming convention is not a
perfectly reliable signal. `audio_bus.h` is *not* `engine_`-prefixed but
carries K1 addresses directly (`kAddrCExoSoundPtr`, `kAddrCExoSoundPlayOneShotSound`,
etc. — `audio_bus.h:97-138`), and `probe_priority_groups.cpp` pulls those in
transitively via `#include "audio_bus.h"` even though it doesn't include
anything named `engine_*`. `intro_skip.cpp`'s movie-filename check
(`Movies/biologo.bik`) is filesystem-based and needs no engine offsets, but
the filenames themselves are a K1/BioWare-logo fact unverified against K2's
actual intro movie set (Obsidian may ship different or additional logo
files) — likely fine, not confirmed. Treat the list above as strong
candidates verified by include-graph plus a spot read, not an exhaustively
certified "safe" set — a real K2 port should still grep each candidate file
for bare hex literals before assuming it needs zero changes.

### K13 — R()'s "only `.text` needs rebasing" assumption breaks for K2

Restated from K1 for emphasis because it directly affects how any K2 address
seam should be designed: `engine_rebase.h:16-19` documents that `.data`
addresses are left un-rebased because they're byte-stable **across K1
build variants specifically** (same source, relinked). That precondition
does not hold across different games. A K2 version of this mechanism cannot
inherit the "leave `.data` alone" shortcut — every address, `.text` and
`.data` alike, needs a K2-specific value. This matters for the two bare
constants noted in K3 (`kAddrRulesGlobal`, `kAddrTlkTablePtr`) and for the
general design of any future K2 address table: it must be a full table, not
a delta/rebase table.

### K14 — the multi-version seam already exists upstream, unused by us

`third_party/Kotor-Patch-Manager/docs/MULTI_VERSION_ARCHITECTURE.md`
describes (status "Implementation Phase") a `GameVersion` class
(`Patches/Common/GameAPI/GameVersion.h`/`.cpp` — these files exist, grep-
confirmed) that resolves `GetFunctionAddress(class, function)` /
`GetGlobalPointer(name)` / `GetOffset(class, member)` from a per-version
`addresses.toml`, selected at runtime by the exe's SHA-256 (the same
identifier `hooks.toml`'s `target_versions` already uses). The doc lists
this explicitly as the intended seam for supporting K1 *and* K2 from the
same compiled patch DLL, and even carries example K2 exe hashes (GoG Aspyr,
Steam Aspyr, Legacy 1.0/1.0b) in its appendix.

Grep-confirmed: **`patches/Accessibility` has zero references to
`GameVersion`, and zero references to `addresses.toml`.** Our patch bypasses
this entirely in favour of the bespoke `acc::addr::R()` (K1-build-variant-only,
per K1/K13) plus 214 completely unindirected struct-offset constants (K3/K4).
This matches `docs/kotor2-port-feasibility.md`'s finding verbatim ("We
currently use none of it — all 264 addresses are hardcoded") and extends it:
the same is true, worse, for the 214 struct offsets, which the upstream
`GameVersion::GetOffset` API was explicitly designed to also cover but which
our code never routes through anything.

## Deliverable: how to partition `engine_offsets.h`, and what layering is realistic

### The partition, chosen along the volatility axis (not subsystem/panel tidiness)

Three volatility classes came out of the inventory above, each with a
different *kind* of seam it would need, which is the real argument for
splitting along this axis rather than by GUI subsystem:

1. **Executable-derived, already indirected (K1/K6, 103 constants).**
   Function and vtable addresses. Already flow through `acc::addr::R()`.
   Porting to K2 means swapping the backing table, not changing call sites —
   *provided* the split keeps them together as "things that go through R()"
   rather than scattering them by which GUI panel happens to use them.
2. **Executable-derived, NOT indirected (K3/K4, 214 constants + the 2 bare
   `.data` pointers from K13).** Struct field offsets. This is the file's
   biggest content block and its biggest exposed risk: every one of these is
   a bare literal baked into pointer arithmetic at its call site, with no
   function-call seam at all — unlike the address bucket, there's nothing to
   redirect later without touching every offset constant's declaration (and,
   per `GameVersion::GetOffset`'s design in K14, ideally its call sites too,
   though that is a much larger change — see "what to defer" below).
3. **Resource-derived, not executable-derived at all (K7/K8/K9).**
   `.gui`-file control IDs, `PanelKind` CGuiInGame slot offsets, and TLK
   strrefs. These change with K1/K2's respective `.gui` and `dialog.tlk`
   content files, completely independently of which `swkotor*.exe` is
   running. They do not belong in a file whose job is "facts about the
   compiled executable" at all — mixing them into `engine_offsets.h` today
   is exactly the kind of thing that made the original tidiness-only 5-way
   split look plausible but would have hidden this distinction rather than
   surfacing it.

Concrete recommendation:

- **`engine_offsets.h` (or `engine_addresses.h`) keeps class 1** — every
  `acc::addr::R()`-wrapped `kAddr*`/`kVtable*` constant, plus the typedefs
  for the `__thiscall` signatures that go with them. This is the file a K2
  port would eventually clone wholesale and re-populate with K2 addresses,
  keeping the same names.
- **A new `engine_struct_offsets.h` takes class 2** — every bare
  `kXxxOffset`/`kXxxSize`/`kXxxCount` struct-shape constant, the
  `CExoArrayList`/`Vector`/`CExoString`/`CExoLinkedList*` primitive struct
  defs, and a comment banner flagging that these have **no indirection
  today** and are the highest-cost bucket for any future port (cites K4).
  This mechanical split has no behavioural effect (pure declaration move +
  include-fixups across the 86 includers) and is safe to do now.
- **GUI-resource IDs and TLK strrefs (class 3) move out of
  `engine_offsets.h` entirely**, to sit next to the code that already owns
  them content-wise: the `.gui`-ID tables already mostly live in
  `menus_internal.h`/`.cpp` and the various `menus_*.cpp` files (K8) — leave
  them there, just stop treating `kCloseButtonStrRef`
  (currently in `engine_offsets.h:52`) as an "engine offset"; it and any
  sibling strref constants belong in a strings/content-facing header (or
  simply annotated in place) since they are `dialog.tlk` facts, not
  executable facts. `PanelKind`'s slot-offset table (K7) already lives in
  `engine_panels.cpp`, correctly separate from `engine_offsets.h` — no move
  needed there, just confirmation that new panel-kind work keeps living
  there and not in the offsets file.
- Every constant in the new struct-offsets file, and every `.gui`-ID /
  strref constant, gets a one-line `// K1 <source>` provenance comment
  (SARIF struct name, or "TLK literal", or ".gui control id") if it doesn't
  already have one — most already do, from the inventory above. This is
  purely mechanical and turns a future K2 RE pass into "grep for constants
  lacking a K2 counterpart" instead of "re-read 1820 lines."

### What NOT to do now, and why

The task brief raises "k1/ vs shared/" and "interface-per-subsystem" as
options. Both are more than this codebase can responsibly absorb before a
K2 binary and `.gui`/`dialog.tlk` set have actually been RE'd:

- **Full `k1/` vs `shared/` directory split**: would require deciding, for
  86 includers of `engine_offsets.h` alone, which of every symbol is
  "shared" — a call that cannot be made correctly without a K2 SARIF to
  check against (K4's "plausibly stable" bucket is explicitly not yet
  verified). Splitting now would either guess (and be wrong in ways that
  cost more to unwind than to have avoided) or produce a `shared/` folder
  that's empty except for the K12 modules that were already
  engine-independent without needing a folder to prove it.
- **Interface-per-subsystem abstraction** (wrapper classes per engine class,
  à la `MULTI_VERSION_ARCHITECTURE.md` Phase 2/3): this is upstream
  KPatchManager's own stated design for exactly this problem, and it has sat
  at "Implementation Phase" with a documented pilot-migration plan that,
  per K14, has not actually been adopted by any patch including ours. If the
  framework author's own reference implementation hasn't completed this
  migration for any patch yet, committing our patch — which has roughly 2.5x
  the address/offset surface of the example in that doc (264+214 vs a few
  dozen) — to build and maintain it ourselves, ahead of having a second game
  to actually validate it against, is not proportionate. This is a detour-
  hook DLL against one exe today; a full wrapper-class layer is
  engine-reimplementation-shaped effort for a K2 port that isn't scheduled.

**Realistic recommendation**: keep the single-DLL-per-game model KPatchManager
itself uses (same shape as the existing `hooks.toml`/`allard.hooks.toml`
selection), and treat "adopt `GameVersion` name-lookup for the address bucket
only" as the actual future migration step — exactly what
`docs/kotor2-port-feasibility.md` already recommends ("Migrate to
`GameVersion` name lookup first... it is worth doing on its own merits and is
a hard prerequisite here"). That migration is still a real, non-trivial
effort (103+ call sites), so it should stay a deliberate future project, not
something folded into this refactor. What *is* worth doing now is the
volatility-axis header split above, because it makes that future migration
strictly easier (class-1 constants are already grouped, ready to swap for
`GameVersion` calls one file at a time) without committing to it yet.

### Cheaper-now-than-later summary

- **Do now** (mechanical, zero behaviour change, directly answers the
  deferred question): split `engine_offsets.h` into an addresses/vtables
  file and a struct-offsets file along the volatility line above; move the
  two stray TLK-strref/GUI-ID-flavoured constants (`kCloseButtonStrRef` at
  minimum) out of the offsets file to sit with their content-fact siblings;
  add "K1 <source>" provenance comments where missing.
- **Do now** (documentation-only, cheap, high future payoff): tag the five
  K10 K1-only-content modules (`floor_puzzle`, `spectator_scene`,
  `endar_softlock`, `tutorial_hints`, `map_shipped_hints`) with a header
  banner noting they are K1-scenario content that will not port and needs
  fresh K2 authoring, so a future contributor doesn't spend time trying to
  "generalize" irreducibly K1 story data.
- **Do later, only when a K2 port is actually scheduled** (needs a K2
  binary/SARIF, `.gui` set, and `dialog.tlk` in hand first): re-derive all
  214 struct offsets and 103+ addresses/vtables (K4/K6, the dominant cost,
  exactly as `docs/kotor2-port-feasibility.md` already flagged); re-derive
  the `.gui` control-ID tables (K8) and TLK strrefs (K9) — these two can
  start independently and earlier than the exe work, since K2's `.gui`/
  `dialog.tlk` are extractable without any RE risk; author new K2-specific
  content modules for K10's five K1-only scenarios; verify K11's minigame
  content deltas; and, as a distinct prerequisite project, migrate the
  address bucket onto `GameVersion` name lookup (K14) before populating a
  K2 address table, per the feasibility doc's own sequencing.
