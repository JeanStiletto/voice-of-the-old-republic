# menus_pazaakdeck.h (50 lines)

Public surface for the Pazaak side-deck builder module. Documents the panel's
verified layout (`all_cards[18]` @ +0x1A4, `sidedeck_gui[10]` @ +0x501C,
`card_counts[18]` @ +0x755C, `sidedeck[10]` @ +0x75A4) and declares the four
entry points consumed by the menu chain and the manager input hook.

## Declarations (in source order)

- L22 — `bool IsDeckPanel(void* panel)` — vtable match for CSWGuiPazaakStart
- L29 — `bool ExtractCardLabel(void* panel, void* control, char* outBuf, size_t bufSize)`
  note: returns false untouched when not the deck panel/card widget — caller falls through to the standard extractor ladder
- L35 — `bool IsChainDecorative(void* panel, void* control)` — drop overlay labels + unaddable cards from chain nav
- L42 — `bool TryHandleInput(void* panel, int param_1, int param_2, int& rv)` — 3-row navigator, called before the generic 1-D chain
- L47 — `void Tick()` — drains the deferred add/remove/play action; call before the menu pending-op drain
