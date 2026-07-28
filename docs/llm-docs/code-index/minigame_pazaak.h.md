# pazaak.h (59 lines)

Public surface for the Pazaak board driver. Per-tick: locate the live board
panel, announce state deltas, poll keyboard play; no-op when no board is
active. Wired ahead of the in-world/menu pollers in `core_tick::Dispatch` so
it can `Consume()` shared keys first.

## Declarations (in source order)

- L23 — `enum class CardContext { Committed, Hand, Collection }`
  note: controls how a +/- flip card's sign is spoken (decided/undecided/current).
- L28 — `void FormatCardLabel(int index, int flip, CardContext ctx, char* out, size_t n)`
  note: shared by the board game and the side-deck builder (menus_pazaakdeck).
- L33 — `bool IsBoardForeground()`
- L38 — `bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv)`
  note: called from menus.cpp's CSWGuiManager::HandleInputEvent hook ahead of the generic chain.
- L46-47 — `constexpr int kWagerLessCode = 0x2f; kWagerMoreCode = 0x30`
- L54 — `void DispatchWagerInput(void* panel, int code)`
- L56 — `void Tick()`
