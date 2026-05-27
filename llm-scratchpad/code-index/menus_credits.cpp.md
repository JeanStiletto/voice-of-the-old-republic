# menus_credits.cpp (108 lines)

Credits-screen virtual-row implementation. Maps label controls to multi-part row text for the chargen credits panel.

## Declarations (in source order)

- L33 — `struct CreditsAnchorSpec` — fields: panel vtable address, label control offset, sibling offsets array (anonymous ns)
- L38 — `constexpr CreditsAnchorSpec k_anchors[]` — one entry per credits panel type (anonymous ns)
- L45 — `const CreditsAnchorSpec* FindSpecForPanel(void* panel)` — vtable-matches panel against k_anchors (anonymous ns)
- L56 — `bool acc::menus::credits::IsCreditsRowAnchor(void* panel, void* labelControl)`
- L63 — `void acc::menus::credits::ForEachCreditsRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)`
- L79 — `bool acc::menus::credits::ExtractCreditsRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
