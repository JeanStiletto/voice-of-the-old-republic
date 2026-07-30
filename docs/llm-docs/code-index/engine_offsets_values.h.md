# engine_offsets_values.h (74 lines)

Part of the `engine_offsets.h` family (see that entry for the family map).
The smallest of the four: 18 constants that are neither executable addresses
nor struct field offsets.

## Why it is its own file

These have **no counterpart in the upstream AddressDatabase**, and that is the
point rather than an omission. TLK strrefs and `.gui` control IDs are
resource-derived: they follow `dialog.tlk` and the layout files, not the
executable. A re-pack of the same build can move them while every address stays
put, and a relinked build moves every address while these stay put. On a
KOTOR 2 port they need re-deriving from K2's own resources, independently of
any address or offset work.

## Contents

- **`kVtableAsLabel` / `AsLabelHilight` / `AsButton` / `AsButtonToggle`** —
  `GuiControlMethods` vtable slot indices (20-23) for RTTI-style downcasts.
  Each accessor returns `this` cast to the concrete subclass or nullptr; they
  are trivial and side-effect-free, so calling them from inside a hook is safe.
- **`kCloseButtonStrRef` (1582)** — the engine's reusable close/back caption,
  shared by BTN_EXIT / BTN_BACK / BTN_CANCEL across all 21 sub-screens. Matching
  the *resolved* text for this strref is locale-agnostic. Confirm/Yes-No popups
  deliberately use 1580/1581 instead, so matching 1582 never strips the only
  actionable button from a choice dialog.
- **`kInvalidObjectId` (0x7F000000)** — engine sentinel for "no object" in
  AI-queue object-id slots and dialog/bark speaker fields. Distinct from
  `CGameObjectArray`'s removed-handle sentinel (0xFFFFFFFF).
- **`kActionType*` (9 bytes)** — the inferred `CSWSCombatRoundAction.action_type`
  enum, ordered by the declaration order of `CSWSCombatRound::Add*`. Still
  marked inferred; validate per path before relying on a specific value.
- **`kAbilitiesPanelCode*`** — panel-internal input codes for
  `CSWGuiInGameAbilities::HandleInputEvent`: 0x29 runs the engine's smart tab
  cycle (auto-skipping an empty Powers tab), 0x31/0x32 step the feat-chain
  rows. Distinct from the manager-level `kInput*` codes.
