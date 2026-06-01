// Pazaak side-deck builder (CSWGuiPazaakStart) — the pre-game screen where
// the player picks 10 cards from their collection for the match's side deck.
//
// The screen's 28 card slots are CSWGuiPazaakCard widgets (vtable 0x007531c0)
// that carry no text of their own, so the generic chain reads them as blank
// "unlabeled buttons". This module labels them from the panel's card model
// (card_counts[18] + sidedeck[10]) and filters the decorative overlay labels.
//
// Layout (verified against the live panel dump + struct header):
//   all_cards[18]    @ +0x1A4   (available-card grid, stride 0x31C)
//   sidedeck_gui[10] @ +0x501C  (chosen-deck slot widgets, stride 0x31C)
//   card_counts[18]  @ +0x755C  (int — copies of each type still available)
//   sidedeck[10]     @ +0x75A4  (CPazaakCard — the chosen cards)

#pragma once

#include <cstddef>

namespace acc::menus::pazaakdeck {

// True iff `panel` is the CSWGuiPazaakStart side-deck builder (vtable match).
bool IsDeckPanel(void* panel);

// Synthesize speech for a CSWGuiPazaakCard widget on the deck panel:
//   available grid → "{card}, N available" / "{card}, none left"
//   chosen slot    → "Deck slot K: {card}" / "Deck slot K: empty"
// Returns false (outBuf untouched) when `panel` isn't the deck or `control`
// isn't one of its card widgets — caller falls through to the standard ladder.
bool ExtractCardLabel(void* panel, void* control, char* outBuf, size_t bufSize);

// Chain filter: drop the overlay value/count/section-title labels and the
// unaddable (zero-owned) available cards, so navigation steps cleanly through
// the addable cards, the 10 chosen slots, and the Play button. Returns false
// for non-deck panels (no filtering).
bool IsChainDecorative(void* panel, void* control);

// 3-row arrow navigator (row 0 = collection, row 1 = the 10 deck slots,
// row 2 = controls/Play). Called from the manager input hook before the
// generic chain: Up/Down switch rows, Left/Right move within a row, Enter
// adds / removes / plays. Returns true (and sets rv) when it consumes the key
// so the 1-D chain handlers don't also run.
bool TryHandleInput(void* panel, int param_1, int param_2, int& rv);

// Drains the add / remove / play action staged by TryHandleInput (deferred so
// deep engine calls stay off the input-dispatch stack). Call once per tick,
// before the menu pending-op drain.
void Tick();

}  // namespace acc::menus::pazaakdeck
