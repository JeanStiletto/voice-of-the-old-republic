# menus_pazaakdeck.cpp (326 lines)

Pazaak side-deck builder (CSWGuiPazaakStart) accessibility: labels the 28 card
widgets from the panel's card model (`card_counts[18]` + `sidedeck[10]`, not
from the widgets themselves), filters decorative overlay labels out of the
generic chain, and implements a dedicated 3-row arrow navigator (collection /
deck slots / controls) with deferred Add/Remove/Play actions. Talks to
`pazaak.h` (FormatCardLabel/CardContext), `menus_pending` (QueueActivate for
Play), `engine_manager` (IsPanelInManager), and `menus_chain`'s decorative
filter/extractor ladder via the exported `ExtractCardLabel`/`IsChainDecorative`.

## Declarations (in source order)

- L30-L40 — vtable constants (`kVtablePazaakStart`, `kVtablePazaakCard`, `kVtableCSWGuiLabel`) and panel layout offsets (`kAllCardsOff`, `kSidedeckGuiOff`, `kCardCountsOff`, `kSidedeckOff`, `kCardStride=0x31C`, `kNumTypes=18`, `kNumSlots=10`)
  note: offsets verified against live panel dump + struct header (see .h)
- L42-L50 — `VtableIs`, `ReadIntAt`: SEH-guarded raw reads
- L55-L71 — `Classify(panel, control, outAvailable, outIdx)`: classify a card widget by array-membership arithmetic
- L75-L77 — `bool IsDeckPanel(void* panel)`
- L79-L111 — `bool ExtractCardLabel(...)`: chain-ladder extractor, available-grid vs chosen-slot label synthesis
- L113-L128 — `bool IsChainDecorative(...)`: drops all CSWGuiLabel overlays and zero-owned available cards
- L134-L147 — nav statics: `PFN_AddChosenCard`/`PFN_RemoveChosenCard` addrs, `kControlPlayId=78`, `Op` enum, `g_row`/`g_col`/`g_navPanel`, deferred `g_op`/`g_opArg`/`g_opPanel`
- L152-L166 — `BuildCollection`: owned-card-type row (owned = count>0 OR placed in a deck slot)
- L168-L179 — `DeckFilledCount`, `RowLength`
- L181-L196 — `FindControlById(panel, id)`: linear scan of panel's CExoArrayList controls, capped at 256
- L198-L234 — `Speak`, `AnnounceFocus`: per-row speech synthesis
- L238-L282 — `bool TryHandleInput(void* panel, int param_1, int param_2, int& rv)`: 3-row arrow navigator; Enter stages a deferred op, does not call engine directly
  note: consumes both press and release of nav/enter keys (rv=1) so the 1-D chain never double-handles
- L284-L323 — `void Tick()`: drains the staged Add/Remove/Play op off the input-hook stack, calls `AddChosenCard`/`RemoveChosenCard` via SEH-guarded thiscall, or forwards Play to `menus::pending::QueueActivate`
