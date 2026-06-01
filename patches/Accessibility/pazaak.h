// Pazaak minigame accessibility (CSWGuiPazaakGame board).
//
// Per-tick driver: locate the live Pazaak board panel, announce state
// deltas (draws, plays, stands, set/match results) and poll keyboard play.
// No-op when no board is active. Wired into core_tick::Dispatch ahead of
// the in-world / menu pollers so it can Consume() the shared keys
// (Tab / Enter / arrows / Esc) before those pollers sample them.
//
// See docs/pazaak-investigation.md for the full reverse-engineering.

#pragma once

#include <cstddef>

namespace acc::pazaak {

// Card-label context for FormatCardLabel:
//   Committed  — a board card with a decided sign (flip cards show their face).
//   Hand       — a hand card: both faces + the current one
//                ("plus or minus 3, currently plus 3").
//   Collection — a deck-builder card: both faces, no current sign yet
//                ("plus or minus 3") — the sign is chosen when played in-game.
enum class CardContext { Committed, Hand, Collection };

// Synthesize a localized card label from the card index (see §5 of
// docs/pazaak-investigation.md) into `out`. Shared by the board game and the
// side-deck builder (menus_pazaakdeck).
void FormatCardLabel(int index, int flip, CardContext ctx, char* out, size_t n);

// True iff the Pazaak board is the current foreground panel (updated each
// Tick). The manager input hook uses this to swallow arrow keys so the engine
// doesn't move focus onto its own Weiter/Halten buttons during board play.
bool IsBoardForeground();

// Manager-hook input handler for the board's arrow-zone navigator. Returns
// true (and sets rv) when it consumes the key. Called from menus.cpp's
// CSWGuiManager::HandleInputEvent hook ahead of the generic chain.
bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv);

void Tick();

}  // namespace acc::pazaak
