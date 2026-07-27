# menus_credits.cpp (109 lines)

Implements the virtual "Credits: N" chain row for Inventory and Store panels
— a spec table keyed by `PanelKind` mapping to the credits_value_label
struct offset, read via `ReadGuiString` (falling back to the inline
CExoString/strref path). The label isn't `IsChainNavigable`, so this is the
only way a keyboard user reaches the gold display.

## Declarations (in source order)

- L17 — `namespace acc::menus::credits`
- L25 — `constexpr size_t kInventoryCreditsValueLabelOffset = 0x424`
  note: derived from Lane's struct DB — panel base + 3 preceding CSWGuiLabels
- L30 — `constexpr size_t kStoreCreditsValueLabelOffsetLocal`
  note: aliases kStoreCreditsValueLabelOffset from engine_offsets.h
- L33 — `struct CreditsAnchorSpec { PanelKind kind; size_t valueOffset; }`
- L38 — `constexpr CreditsAnchorSpec k_anchors[]`
  note: registers InGameInventory + Store
- L45 — `const CreditsAnchorSpec* FindSpecForPanel(void* panel)`
- L56 — `bool IsCreditsRowAnchor(void* panel, void* labelControl)`
- L63 — `void ForEachCreditsRowAnchor(void* panel, callback, userData)`
  note: sortCy=1 forces the row above every real button in the chain's y-sort
- L79 — `bool ExtractCreditsRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
  note: treats the .gui-load placeholder text as empty (engine hasn't populated yet)
