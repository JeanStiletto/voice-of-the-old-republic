# menus_charsheet.h (71 lines)

Public surface for the character-sheet (Charakterblatt) opener + virtual
stat-row chain entries. Drives both ends of the virtual chain: RebindChain
(menus_chain.cpp) inserts text-only entries at the anchor labels'
synthetic y-positions, and FromControl (menus_extract.cpp) routes through
`ExtractStatRow` so the user hears a composed phrase ("Stärke 14, +2")
rather than the bare label text.

## Declarations (in source order)

- L20 — `namespace acc::menus::charsheet`
- L27 — `void MaybeAnnounce(void* panel)`
  note: self-gates on PanelKind::InGameCharacter
- L48 — `bool ExtractStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
  note: re-reads live label text every call — reflects character-cycling or level-up changes immediately
- L55 — `bool IsStatRowAnchor(void* panel, void* labelControl)`
- L65 — `void ForEachStatRowAnchor(void* panel, callback, userData)`
  note: anchors emitted in spec-table order; RebindChain uses sortCy to force logical reading order
