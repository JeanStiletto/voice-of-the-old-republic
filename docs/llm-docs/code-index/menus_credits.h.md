# menus_credits.h (56 lines)

Declares the virtual credits-row surface for Inventory/Store panels — same
shape as `menus_charsheet`'s stat block: a label the chain walker would
otherwise skip (not IsChainNavigable), surfaced as a text-only virtual chain
entry via `menus_chain.cpp`'s RebindChain and overridden in
`menus_extract.cpp`'s FromControl ladder.

## Declarations (in source order)

- L28 — `namespace acc::menus::credits`
- L34 — `bool IsCreditsRowAnchor(void* panel, void* labelControl)`
- L42 — `void ForEachCreditsRowAnchor(void* panel, callback, userData)`
  note: sortCy fixed at 1 so credits always sorts above real buttons
- L52 — `bool ExtractCreditsRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
  note: false also on not-yet-populated value (empty or .gui-load placeholder text)
