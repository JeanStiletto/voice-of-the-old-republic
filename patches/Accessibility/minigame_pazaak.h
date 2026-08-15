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
//
// `value` is the CPazaakCard's THIRD dword, which exists on KOTOR 2 only (its
// card grew 8 -> 12 bytes). For the four special cards whose face is not fixed
// by the index — Double, 2&4, 3&6 and the value card — it holds the value the
// engine resolved when the card was played, and 0 while the card is still
// undecided. Ignored entirely on KOTOR 1 and for every ordinary card, so
// callers that have no third field can leave it at 0.
void FormatCardLabel(int index, int flip, CardContext ctx, char* out, size_t n,
                     int value = 0);

// True iff `index` is one of KOTOR 2's ±-faced cards, i.e. one whose sign the
// player picks at play time: the six classic flip cards (12..17) and, on
// KOTOR 2, the ±1 tiebreaker (18). The board navigator asks before opening its
// sign chooser. False for every index on the other game's numbering.
bool IsSignChoiceCard(int index);

// True iff the Pazaak board is the current foreground panel (updated each
// Tick). The manager input hook uses this to swallow arrow keys so the engine
// doesn't move focus onto its own Weiter/Halten buttons during board play.
bool IsBoardForeground();

// Manager-hook input handler for the board's arrow-zone navigator. Returns
// true (and sets rv) when it consumes the key. Called from menus.cpp's
// CSWGuiManager::HandleInputEvent hook ahead of the generic chain.
bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv);

// Engine event codes the wager popup's HandleInputEvent switch maps to a
// less/more step. The BTN_LESS / BTN_MORE speed buttons don't act on these
// directly — their "pushed" callbacks (CSWGuiSkillsCharGen::OnMinus/
// OnPlusButtonPushed, reused by the popup) re-dispatch them to the panel's
// own HandleInputEvent, which clamps to [1, max], plays the click sound, and
// repaints the value label.
// Both games map the same two codes: KOTOR 2's CSWGuiWagerPopup::HandleInputEvent
// @0x00887B10 biases the code by 0x27 and jumps through a 26-entry table in
// which 0x2f and 0x30 land on the two arms that clamp-and-step the wager field
// (`cmp [this+0xe30], 1` then `sub 1`; `cmp [this+0xe30], [this+0xe34]` then
// `add 1`).
constexpr int kWagerLessCode = 0x2f;  // decrement
constexpr int kWagerMoreCode = 0x30;  // increment

// pazaakwager .gui control ids, which KOTOR 2 renumbers wholesale
// (pazaakwager.gui vs pazaakwager_p.gui): LBL_MAXIMUM 3 -> 2, BTN_LESS 4 -> 6,
// BTN_MORE 5 -> 7. Three TUs need them — the chain filter that masks the two
// speed buttons, the Enter router that re-dispatches them, and the extractor
// that builds the virtual wager row — so they live here rather than being
// re-derived per call site.
int WagerLessButtonGuiId();
int WagerMoreButtonGuiId();
int WagerMaxLabelGuiId();

// Drive a wager-popup less/more step by calling CSWGuiWagerPopup::
// HandleInputEvent(panel, code, 1) directly — the same path the engine's own
// button-push callbacks take. `code` is kWagerLessCode / kWagerMoreCode.
// SEH-guarded; no-op on a stale/non-popup pointer. The per-tick wager
// observer (ObserveWager) announces the resulting amount.
void DispatchWagerInput(void* panel, int code);

void Tick();

}  // namespace acc::pazaak
