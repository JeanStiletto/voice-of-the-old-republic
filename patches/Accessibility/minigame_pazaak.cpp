// Pazaak minigame accessibility — see pazaak.h + docs/pazaak-investigation.md.
//
// Design (no engine detour hook):
//   * Identify the live board panel structurally — the foreground modal
//     whose deep fields match the CSWPazaak model layout — then track it by
//     pointer until it leaves the manager. The panel's vtable is learned on
//     first sight and logged so it can be promoted to a hard constant later.
//   * Read the model each tick and announce deltas. Pure state observation,
//     so we poll rather than hook (hook-vs-poll principle).
//   * Drive play through the engine's own CSWGuiPazaakGame handlers
//     (HandlePlayHandCard / HandleStand / HandleContinue), which guard on
//     game_state internally — safe to call off our OnUpdate tick (the same
//     deferred callback site the menu pending-queue drains from).
//   * Input is an arrow-zone navigator (same model as the deck builder):
//     zone 0 your hand, 1 your board, 2 opponent board, 3 actions. Up/Down
//     switch zones, Left/Right move within, Enter plays/activates. Arrows +
//     Enter are handled through the manager input hook (TryHandleInput) and
//     consumed, so the generic chain can't also drive the board's buttons.
//     The letter shortcuts (s/e/c/t, Shift+C) are Win32-polled because the
//     engine drops those scancodes before the manager hook.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "minigame_pazaak.h"

#include "engine_game.h"      // IsKotor2 — Batch 1 decline
#include "engine_input.h"     // kInputNav* / kInputEnter* / kInputEsc*
#include "engine_manager.h"   // GetForegroundPanel, IsPanelInManager, kAddrGuiManagerPtr
#include "engine_panels.h"    // IdentifyPanel, PanelKind
#include "hotkeys.h"
#include "log.h"
#include "prism.h"
#include "strings.h"
#include "engine_rebase.h"
#include "engine_offsets_select.h"

namespace acc::pazaak {

namespace {

// ---- Engine surfaces (docs/pazaak-investigation.md §13) -------------------
// HandleContinue / HandleStand are __thiscall(this, CSWGuiControl* param_1) —
// param_1 is the originating button (null when invoked programmatically). Their
// first guard is `param_1 == 0 || param_1->is_active != 0`, so we MUST pass an
// explicit control; omitting it leaves param_1 as stack garbage that the guard
// dereferences, faulting (and silently no-op'ing the action).
typedef int  (__thiscall* PFN_GetTotal)(void* player);
typedef void (__thiscall* PFN_HandleCtrl)(void* panel, void* control);
typedef void (__thiscall* PFN_HandleInt)(void* panel, int index);

//
// KOTOR 2 twins, all four reached from its CSWGuiPazaakGame constructor
// @0x0088aac0 and its own ctor-registered handlers, then body-matched against
// KOTOR 1's:
//   * HandleContinue @0x0088F070 — registered on BTN_XTEXT (panel+0x7b10) for
//     event 0x27. Same guard chain (`param_1 == 0 || param_1->is_active`, player
//     not stood, state 3 or 4), same ShowHelp strrefs (0x96f1/0x7e4c/0x96e2),
//     same tail (state = 5, timer = 0.4f, RefreshDisplay).
//   * HandleStand @0x0088F180 — BTN_YTEXT (panel+0x7cf0). Distinguished from
//     HandleContinue by the one line that matters: it WRITES player.stand = 1.
//   * HandlePlayHandCard @0x0088F280 — not registered directly; the hand-card
//     entry's own event-0x27 handler @0x0088EE30 calls it with the card
//     control's custom_value (the slot) as its single int argument, exactly
//     KOTOR 1's shape. Same guards (state == 3, slot 0..3, CanUse...).
//   * GetTotal @0x008FE830 — witnessed at HandleContinue's call site, where the
//     `this` is formed as `model + 8` (i.e. CSWPazaak.player); the body walks
//     nine board slots at +0x30 with a 12-byte stride.
const uintptr_t kAddrGetTotal = acc::addr::Pick(0x006e4360, 0x008FE830); // CPazaakPlayer::GetTotal
const uintptr_t kAddrHandleContinue = acc::addr::Pick(0x0067ec20, 0x0088F070); // End Turn
const uintptr_t kAddrHandleStand = acc::addr::Pick(0x0067ed00, 0x0088F180); // Stand
const uintptr_t kAddrHandlePlayHandCard = acc::addr::Pick(0x0067ede0, 0x0088F280); // Play hand card (int slot)
const uintptr_t kAddrWagerHandleInput = acc::addr::Pick(0x0067e150, 0x00887B10); // CSWGuiWagerPopup::HandleInputEvent
typedef void (__thiscall* PFN_WagerHandleInput)(void* popup, int code, int state);

// ---- Struct offsets ------------------------------------------------------
// KOTOR 2's CSWGuiPazaakGame is the same shape with a bigger control block: the
// per-card GUI stride grows 0x31C -> 0x334 and the board gains four BTN_CHANGE
// buttons for its new dual-value cards, which pushes the whole model tail up by
// a uniform 0xF7C. Both fields are witnessed in K2's HandleContinue @0x0088F070:
// it dereferences [panel+0x964c] as the model (reading player.stand through it)
// and tests [panel+0x9650] against 3 and 4 — the same two states KOTOR 1's tests
// at +0x86d4. The 0.4f turn timer that follows lands at +0x9654, the same +4.
const size_t kPanelModelOffset = acc::off::Pick(0x86d0, 0x964C); // CSWGuiPazaakGame->pazaak (CSWPazaak*)
const size_t kPanelStateOffset = acc::off::Pick(0x86d4, 0x9650); // game_state

// "Tutorial active" gate. DoGameSequence is only pumped while the board is the
// topmost panel (Draw's IsOnTop gate, see docs/pazaak-investigation.md §7); the
// tutorial game's ShowHelp stacks an inaccessible help message-box ON TOP of the
// board (at the first draw, the opponent's turn, etc.), which drops the board
// off-top and freezes the turn engine until a sighted player dismisses it.
// ShowHelp and the End-Turn/Stand nags all no-op when this flag is 0, so we
// clear it on acquire to disable the visual tutorial outright — our narration
// replaces it.
//
// The two games spell the same gate differently, which is why this is a Pick of
// a whole address rather than one struct's field:
//   * KOTOR 1 reads it INSIDE the embedded CSWGuiTutorial — panel+0x7d20 is the
//     sub-panel, +0x994 its own active flag.
//   * KOTOR 2 hoists it to a plain panel field at +0x9630, set from the
//     constructor's third argument (showTutorial). Its HandleContinue /
//     HandleStand / HandlePlayHandCard each gate their ShowHelp on
//     `[panel+0x9630] != 0` before calling it on the CSWGuiTutorial that now
//     lives at panel+0x8c58. Clearing the field disables all three the same way.
const size_t kTutorialActiveOffset = acc::off::Pick(0x7d20 + 0x994, 0x9630);

// CSWGuiWagerPopup (the "Wie viel setzt du?" bet popup, a different panel from
// the board): current wager at +0xc94, maximum at +0xc98. The chain labels the
// less/more buttons but only re-reads on focus change, so we poll the amount
// and announce it when it changes (pure observation).
//
// KOTOR 2 = +0xE30 / +0xE34, and these are the least ambiguous constants in the
// whole batch: its HandleInputEvent @0x00887B10 clamps the decrement branch with
// `cmp [this+0xe30], 1` before `sub 1`, and the increment branch with
// `cmp [this+0xe30], [this+0xe34]` before `add 1`. Floor against current,
// current against ceiling — current is the low field in both games.
const size_t kWagerCurOffset = acc::off::Pick(0xc94, 0xE30);
const size_t kWagerMaxOffset = acc::off::Pick(0xc98, 0xE34);

// CPazaakPlayer is 0x70 bytes (hand[4]=0x20 + board[9]=0x48 + stand + score).
// CSWPazaak.player is at +0x08, so enemy follows at +0x78 (NOT +0x98), and the
// per-player stand/score sit at +0x68/+0x6c — see swkotor.exe.h structs
// CPazaakPlayer / CSWPazaak. (Earlier 0x98/0x88/0x8c read dead space + deck
// cards, so every opponent read came back zero/garbage.)
//
// KOTOR 2 keeps this model exactly, with ONE change that cascades through all of
// it: CPazaakCard grew from 8 bytes to 12. The third dword is the resolved value
// of K2's new dual/double cards (see FormatCardLabel), and it moves every array
// stride and every field above one. CPazaakPlayer is therefore 0xa4 rather than
// 0x70 (hand 4*0xc = 0x30, board 9*0xc = 0x6c, then stand and score), and the
// deck 40*0xc.
//
// Witness for each, all in the K2 twins:
//   * player Same(+0x8) — HandleContinue forms GetTotal's `this` as model + 8.
//   * enemy +0xac — ClearGameBoard @0x008FEB00 clears the player's nine board
//     slots at +0x38 (= 0x8 + 0x30) AND the enemy's at +0xdc (= 0xac + 0x30),
//     then zeroes both stands at +0xa4 and +0x148 (= 0x8/0xac + 0x9c).
//   * cards[40] +0x150 and remaining +0x330 — ShuffleDeck @0x008FEBB0 writes
//     forty 12-byte cards from +0x150 and finishes with `[this+0x330] = 39`;
//     DrawCard @0x008FEDA0 indexes `+0x150 + n*0xc` off that same counter.
//   * hand +0x0 / board +0x30 — ChooseSidedeck @0x008FE8A0 fills four cards
//     from `this + i*0xc`; GetTotal @0x008FE830 walks nine from `this + 0x30`.
//   * stand +0x9c — read as model+0xa4 in HandleContinue and written there by
//     HandleStand; score +0xa0 follows it, which the 0xa4 player size closes.
const size_t kModelPlayerOffset = acc::off::Same(0x08);        // CSWPazaak.player (CPazaakPlayer)
const size_t kModelEnemyOffset  = acc::off::Pick(0x78, 0xAC);  // CSWPazaak.enemy
const size_t kModelRemainOffset = acc::off::Pick(0x228, 0x330); // remaining_card_count

const size_t kPlayerHandOffset  = acc::off::Same(0x00);        // CPazaakCard hand_cards[4]
const size_t kPlayerBoardOffset = acc::off::Pick(0x20, 0x30);  // CPazaakCard board_cards[9]
const size_t kPlayerStandOffset = acc::off::Pick(0x68, 0x9C);  // int stand
const size_t kPlayerScoreOffset = acc::off::Pick(0x6c, 0xA0);  // int score (sets won)

const size_t kCardIndexOffset = acc::off::Same(0x00);
const size_t kCardFlipOffset  = acc::off::Same(0x04);
// The resolved face value of K2's Double / 2&4 / 3&6 / value cards. Read only
// under IsKotor2() — on KOTOR 1 there is no such field and the offset poisons.
const size_t kCardValueOffset = acc::off::Kotor2Only(0x08);
const size_t kCardStride      = acc::off::Pick(0x08, 0x0C);

constexpr int kHandSlots  = 4;
constexpr int kBoardSlots = 9;

// KOTOR 2's five extra side-deck card indices (see FormatCardLabel). Inert on
// KOTOR 1, where 18 is already the first main-deck card — every consumer either
// gates on IsKotor2() or is only reachable from a K2 model read.
constexpr int kCardTiebreaker = 18;  // ±1 that also wins ties  ("+1T"/"-1T")
constexpr int kCardDouble     = 19;  // doubles the previous card ("D")
constexpr int kCardTwoFour    = 20;  // "2&4"
constexpr int kCardThreeSix   = 21;  // "3&6"
constexpr int kCardValue      = 22;  // value card, sign from is_flipped

// True for the one card whose FACE the player picks as well as its sign.
bool IsValueChoiceCard(int index) {
    return acc::game::IsKotor2() && index == kCardValue;
}

// First main-deck index: 18 on KOTOR 1, 23 on KOTOR 2 (whose five special
// side-deck cards take 18..22).
int FirstMainDeckIndex() { return acc::game::IsKotor2() ? 23 : 18; }
bool IsMainDeckCard(int index) { return index >= FirstMainDeckIndex(); }

// game_state values (DoGameSequence) we branch on.
constexpr int kStatePlayerInteractive = 3;  // can play a hand card + stand/end
constexpr int kStatePlayerPlayed      = 4;  // already played; stand/end only
constexpr int kStateResult            = 9;  // result sound queued
constexpr int kStateResultWait        = 10; // waiting on result message box

// ---- SEH-guarded primitive reads -----------------------------------------
bool ReadIntAt(void* base, size_t off, int* out) {
    __try {
        *out = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(base) + off);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadPtrAt(void* base, size_t off, void** out) {
    __try {
        *out = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(base) + off);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void WriteIntAt(void* base, size_t off, int v) {
    __try {
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(base) + off) = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void* GetModel(void* panel) {
    void* m = nullptr;
    return ReadPtrAt(panel, kPanelModelOffset, &m) ? m : nullptr;
}
int GetState(void* panel) {
    int s = -1;
    return ReadIntAt(panel, kPanelStateOffset, &s) ? s : -1;
}
void* PlayerOf(void* model) {
    return reinterpret_cast<unsigned char*>(model) + kModelPlayerOffset;
}
void* EnemyOf(void* model) {
    return reinterpret_cast<unsigned char*>(model) + kModelEnemyOffset;
}

// `value` is CPazaakCard's third dword — KOTOR 2 only (its card is 12 bytes,
// KOTOR 1's is 8). Left at 0 on KOTOR 1, which is exactly what FormatCardLabel
// treats as "no resolved value".
struct CardView { int index; int flip; int value; };

CardView ReadCard(void* player, size_t baseOff, int slot) {
    CardView c{ -1, 0, 0 };
    void* cardPtr = reinterpret_cast<unsigned char*>(player) + baseOff + (size_t)slot * kCardStride;
    ReadIntAt(cardPtr, kCardIndexOffset, &c.index);
    ReadIntAt(cardPtr, kCardFlipOffset, &c.flip);
    if (acc::game::IsKotor2()) ReadIntAt(cardPtr, kCardValueOffset, &c.value);
    return c;
}

int CallGetTotal(void* player) {
    __try {
        auto fn = reinterpret_cast<PFN_GetTotal>(kAddrGetTotal);
        return fn(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int BoardCount(void* player) {
    for (int i = 0; i < kBoardSlots; ++i) {
        if (ReadCard(player, kPlayerBoardOffset, i).index == -1) return i;
    }
    return kBoardSlots;
}
CardView BoardLast(void* player, int count) {
    if (count <= 0) return CardView{ -1, 0 };
    return ReadCard(player, kPlayerBoardOffset, count - 1);
}

// CSWGuiPazaakGame's own vtable. KOTOR 1's came out of Lane's symbol export;
// KOTOR 2's out of its RTTI class map. The board is a heap-allocated modal with
// no CGuiInGame slot, so vtable equality is the cleanest identifier there is —
// this is the same shape as every other single-instance panel in engine_panels.
// The structural probe below stays as the fallback: it is what found the board
// before the vtable was known, it costs nothing while untracked, and it keeps
// working if a build relocates the table.
const uintptr_t kVtableCSWGuiPazaakGame = acc::addr::Pick(0x00753358, 0x009A6404);

// Highest card index the engine can put in a hand slot. KOTOR 1 tops out at 27
// (main deck 18..27); KOTOR 2 inserts five special cards at 18..22 and pushes
// its main deck to 23..32 — see FormatCardLabel.
constexpr int kMaxCardIndexK1 = 27;
constexpr int kMaxCardIndexK2 = 32;

// Structural identity probe — true iff `panel` looks like a CSWGuiPazaakGame.
// All reads SEH-guarded: a smaller panel faults on the deep field reads and
// returns false. The nested model-field range checks make a false positive
// from adjacent heap effectively impossible.
bool LooksLikePazaak(void* panel) {
    const int maxCard =
        acc::game::IsKotor2() ? kMaxCardIndexK2 : kMaxCardIndexK1;
    __try {
        if (*reinterpret_cast<uintptr_t*>(panel) == kVtableCSWGuiPazaakGame) {
            return true;
        }
        auto* p = reinterpret_cast<unsigned char*>(panel);
        void* model = *reinterpret_cast<void**>(p + kPanelModelOffset);
        if (!model) return false;
        int state = *reinterpret_cast<int*>(p + kPanelStateOffset);
        if (state < 0 || state > 0xc) return false;
        auto* m = reinterpret_cast<unsigned char*>(model);
        int pScore = *reinterpret_cast<int*>(m + kModelPlayerOffset + kPlayerScoreOffset);
        int eScore = *reinterpret_cast<int*>(m + kModelEnemyOffset  + kPlayerScoreOffset);
        int remain = *reinterpret_cast<int*>(m + kModelRemainOffset);
        int hand0  = *reinterpret_cast<int*>(m + kModelPlayerOffset + kPlayerHandOffset);
        if (pScore < 0 || pScore > 3) return false;
        if (eScore < 0 || eScore > 3) return false;
        if (remain < -1 || remain > 40) return false;
        if (hand0 < -2 || hand0 > maxCard) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- Card label synthesis (docs/pazaak-investigation.md §5) ---------------
// Thin wrapper over the shared synthesizer (acc::pazaak::FormatCardLabel,
// defined below the anonymous namespace and declared in pazaak.h). inHand
// selects the Hand context (both faces + current); board cards are Committed.
void FormatCard(const CardView& c, bool inHand, char* out, size_t n) {
    FormatCardLabel(c.index, c.flip,
                    inHand ? CardContext::Hand : CardContext::Committed, out, n,
                    c.value);
}

void Say(const char* s, bool interrupt = false) {
    if (s && s[0]) prism::Speak(s, interrupt);
}

// ---- On-demand readouts (c / t) ------------------------------------------
void BuildBoard(void* player, char* out, size_t n) {
    using namespace acc::strings;
    out[0] = '\0';
    int cnt = BoardCount(player);
    for (int i = 0; i < cnt; ++i) {
        CardView c = ReadCard(player, kPlayerBoardOffset, i);
        char card[96];
        FormatCard(c, false, card, sizeof(card));
        if (out[0]) strncat(out, ", ", n - strlen(out) - 1);
        strncat(out, card, n - strlen(out) - 1);
    }
    if (!out[0]) snprintf(out, n, "%s", Get(Id::PazaakBoardEmpty));
}

void SpeakHand(void* player) {
    using namespace acc::strings;
    char line[400]; line[0] = '\0';
    int count = 0;
    for (int i = 0; i < kHandSlots; ++i) {
        CardView c = ReadCard(player, kPlayerHandOffset, i);
        if (c.index < 0) continue;
        char card[96];
        FormatCard(c, true, card, sizeof(card));
        if (line[0]) strncat(line, ", ", sizeof(line) - strlen(line) - 1);
        strncat(line, card, sizeof(line) - strlen(line) - 1);
        ++count;
    }
    if (count == 0) { Say(Get(Id::PazaakHandEmpty), true); return; }
    char msg[480];
    snprintf(msg, sizeof(msg), Get(Id::PazaakFmtHand), line);
    Say(msg, true);
}

void SpeakTable(void* model) {
    using namespace acc::strings;
    void* p = PlayerOf(model);
    void* e = EnemyOf(model);
    char pb[400], eb[400], msg[480];
    BuildBoard(p, pb, sizeof(pb));
    BuildBoard(e, eb, sizeof(eb));
    snprintf(msg, sizeof(msg), Get(Id::PazaakFmtYourBoard), pb, CallGetTotal(p));
    Say(msg, true);
    snprintf(msg, sizeof(msg), Get(Id::PazaakFmtOppBoard), eb, CallGetTotal(e));
    Say(msg);
}

// ---- Snapshot + delta announcements --------------------------------------
struct Snap {
    bool valid = false;
    int  state = -1;
    int  pBoard = 0, eBoard = 0;
    int  pStand = 0, eStand = 0;
    int  pScore = 0, eScore = 0;
    int  pTotal = 0, eTotal = 0;
};

Snap      g_prev;
void*     g_panel = nullptr;
uintptr_t g_learnedVtable = 0;
bool      g_started = false;
bool      g_resultAnnounced = false;
bool      g_boardForeground = false;  // board is the top foreground panel (for the input hook)

// Cursor / card-options sub-zone state. Arrow-zone model (same as the deck
// builder): zone 0 = your hand, 1 = your board, 2 = opponent board,
// 3 = actions (Stand / End turn). Up/Down switch zones, Left/Right move within.
int  g_zone = 0;
int  g_col  = 0;
bool g_optMode   = false;
int  g_optSlot   = -1;
int  g_optSign   = 0;   // 0 = plus, 1 = minus
// Chosen magnitude for KOTOR 2's value card (index 22), whose face the player
// picks as well as its sign. The engine's own BTN_CHANGE handler @0x0088F5A0
// toggles the card's third dword between 1 and 2 and repaints, so those are the
// only two faces it has. Unused for every other card.
int  g_optValue  = 1;

// Magnitude of a ±-faced card, for the sign chooser's spoken preview.
// 12..17 are ±1..±6; KOTOR 2's tiebreaker (18) is a ±1; its value card (22)
// carries whatever face the player has dialled in. Only ever called for indices
// IsSignChoiceCard() accepted.
int SignCardMagnitude(int index) {
    if (index >= 12 && index <= 17) return index - 11;
    if (index == kCardValue) return g_optValue;
    return 1;
}

// Speak the sign chooser's current face ("plus 3" / "minus 1" / "plus 2").
// interrupt=false when it follows the chooser's own opening line, so the two
// don't cut each other off.
void SpeakSignOption(int index, bool interrupt) {
    char opt[48];
    snprintf(opt, sizeof(opt),
             acc::strings::Get(g_optSign ? acc::strings::Id::PazaakFmtMinus
                                         : acc::strings::Id::PazaakFmtPlus),
             SignCardMagnitude(index));
    Say(opt, interrupt);
}

Snap ReadSnap(void* panel) {
    Snap s;
    void* model = GetModel(panel);
    if (!model) return s;
    void* p = PlayerOf(model);
    void* e = EnemyOf(model);
    s.state  = GetState(panel);
    s.pBoard = BoardCount(p);
    s.eBoard = BoardCount(e);
    ReadIntAt(p, kPlayerStandOffset, &s.pStand);
    ReadIntAt(e, kPlayerStandOffset, &s.eStand);
    ReadIntAt(p, kPlayerScoreOffset, &s.pScore);
    ReadIntAt(e, kPlayerScoreOffset, &s.eScore);
    s.pTotal = CallGetTotal(p);
    s.eTotal = CallGetTotal(e);
    s.valid = true;
    return s;
}

void AnnounceDeltas(void* panel, const Snap& cur) {
    using namespace acc::strings;
    if (!g_prev.valid) return;  // first valid snapshot is baseline only
    void* model = GetModel(panel);
    if (!model) return;
    void* p = PlayerOf(model);
    void* e = EnemyOf(model);
    char card[96], msg[200];

    // Player auto-drew a main-deck card. Side cards reach the board via our
    // Play handler, which speaks its own confirmation — so gate the draw line
    // to main-deck indices (18+ on KOTOR 1, 23+ on KOTOR 2, whose five extra
    // side-deck cards occupy 18..22 — see IsMainDeckCard).
    if (cur.pBoard > g_prev.pBoard) {
        CardView lc = BoardLast(p, cur.pBoard);
        if (IsMainDeckCard(lc.index)) {
            FormatCard(lc, false, card, sizeof(card));
            snprintf(msg, sizeof(msg), Get(Id::PazaakFmtYouDrew), card, cur.pTotal);
            Say(msg);
            if (cur.pTotal > 20) Say(Get(Id::PazaakOverTwenty));
        }
    }

    // Opponent board grew: drew a main-deck card, or played a side card.
    if (cur.eBoard > g_prev.eBoard) {
        CardView lc = BoardLast(e, cur.eBoard);
        if (IsMainDeckCard(lc.index)) {
            // Main-deck draws are face-up/public, so name the card like the
            // player's draw line (not just the running total).
            FormatCard(lc, false, card, sizeof(card));
            snprintf(msg, sizeof(msg), Get(Id::PazaakFmtOppDrew), card, cur.eTotal);
            Say(msg);
        } else if (lc.index >= 0) {
            FormatCard(lc, false, card, sizeof(card));
            snprintf(msg, sizeof(msg), Get(Id::PazaakFmtOppPlayed), card, cur.eTotal);
            Say(msg);
        }
    }

    // Stands.
    if (cur.eStand && !g_prev.eStand) {
        snprintf(msg, sizeof(msg), Get(Id::PazaakFmtOppStands), cur.eTotal);
        Say(msg);
    }
    if (cur.pStand && !g_prev.pStand) {
        snprintf(msg, sizeof(msg), Get(Id::PazaakFmtYouStand), cur.pTotal);
        Say(msg);
    }

    // Turn start — entered the interactive player state.
    if (cur.state == kStatePlayerInteractive && g_prev.state != kStatePlayerInteractive) {
        Say(Get(Id::PazaakYourTurn));
    }

    // Set / match results — latched once per set (cleared on board reset).
    if (!g_resultAnnounced) {
        if (cur.pScore > g_prev.pScore) {
            if (cur.pScore >= 3) Say(Get(Id::PazaakWinMatch), true);
            else {
                snprintf(msg, sizeof(msg), Get(Id::PazaakFmtWinSet), cur.pScore, cur.eScore);
                Say(msg);
            }
            g_resultAnnounced = true;
        } else if (cur.eScore > g_prev.eScore) {
            if (cur.eScore >= 3) Say(Get(Id::PazaakLoseMatch), true);
            else {
                snprintf(msg, sizeof(msg), Get(Id::PazaakFmtLoseSet), cur.pScore, cur.eScore);
                Say(msg);
            }
            g_resultAnnounced = true;
        } else if ((cur.state == kStateResult || cur.state == kStateResultWait) &&
                   g_prev.state != kStateResult && g_prev.state != kStateResultWait) {
            // Reached the result phase with no score change → a tie set.
            Say(Get(Id::PazaakTieReplay), true);
            g_resultAnnounced = true;
        }
    }
}

// ---- Engine action dispatch (thiscall, this = panel) ----------------------
void DoPlay(void* panel, int slot) {
    __try { reinterpret_cast<PFN_HandleInt>(kAddrHandlePlayHandCard)(panel, slot); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void DoStand(void* panel) {
    __try { reinterpret_cast<PFN_HandleCtrl>(kAddrHandleStand)(panel, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void DoEndTurn(void* panel) {
    __try { reinterpret_cast<PFN_HandleCtrl>(kAddrHandleContinue)(panel, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Play the focused hand card and speak the confirmation. For a +/- card the
// caller sets the hand card's is_flipped first (mirrors the engine's own
// HandleFlipHandCard-then-play flow); UsePlayerSidedeckCard copies the
// 8-byte CPazaakCard, sign included, onto the board.
void PlayAndAnnounce(void* panel, void* player, int slot) {
    using namespace acc::strings;
    CardView c = ReadCard(player, kPlayerHandOffset, slot);
    char card[96];
    FormatCard(c, false, card, sizeof(card)); // committed sign
    DoPlay(panel, slot);
    char msg[200];
    snprintf(msg, sizeof(msg), Get(Id::PazaakFmtYouPlayed), card, CallGetTotal(player));
    Say(msg, true);
    acclog::Write("Pazaak", "play slot=%d index=%d flip=%d", slot, c.index, c.flip);
}

// ---- Arrow-zone navigation helpers ---------------------------------------
int ZoneLen(void* model, int zone) {
    if (zone == 0) return kHandSlots;                  // your hand (4 slots)
    if (zone == 1) return BoardCount(PlayerOf(model)); // your played cards
    if (zone == 2) return BoardCount(EnemyOf(model));  // opponent played cards
    return 2;                                          // actions: Stand, End turn
}

// Hand slots can be empty (index < 0) when a card has been played or the hand
// holds fewer than four cards. Left/Right and zone entry skip those slots so
// the user only ever lands on a playable card.
bool HandSlotFilled(void* player, int slot) {
    if (slot < 0 || slot >= kHandSlots) return false;
    return ReadCard(player, kPlayerHandOffset, slot).index >= 0;
}
int FirstHandCol(void* player) {
    for (int i = 0; i < kHandSlots; ++i)
        if (HandSlotFilled(player, i)) return i;
    return 0;  // hand empty — stay at slot 0 (entry summary says "hand empty")
}
// Next filled slot in `dir` (+1 right / -1 left); returns `from` if none (no wrap).
int NextHandCol(void* player, int from, int dir) {
    for (int c = from + dir; c >= 0 && c < kHandSlots; c += dir)
        if (HandSlotFilled(player, c)) return c;
    return from;
}

// Announce one board side as a whole ("Your board: ..., total N").
void SpeakBoardSide(void* model, bool playerSide) {
    using namespace acc::strings;
    void* pl = playerSide ? PlayerOf(model) : EnemyOf(model);
    char b[400], msg[480];
    BuildBoard(pl, b, sizeof(b));
    snprintf(msg, sizeof(msg),
             Get(playerSide ? Id::PazaakFmtYourBoard : Id::PazaakFmtOppBoard),
             b, CallGetTotal(pl));
    Say(msg, true);
}

// Up/Down entered a zone — read its summary so the user gets oriented.
void AnnounceZoneEntry(void* model, int zone) {
    using namespace acc::strings;
    if (zone == 0)      SpeakHand(PlayerOf(model));
    else if (zone == 1) SpeakBoardSide(model, true);
    else if (zone == 2) SpeakBoardSide(model, false);
    else Say(Get(g_col == 0 ? Id::PazaakStandLabel : Id::PazaakEndTurnLabel), true);
}

// Left/Right moved within a zone — read the single focused element.
void AnnounceZoneElement(void* model, int zone, int col) {
    using namespace acc::strings;
    char card[96];
    if (zone == 0) {
        CardView c = ReadCard(PlayerOf(model), kPlayerHandOffset, col);
        FormatCard(c, true, card, sizeof(card));
        Say(card, true);
    } else if (zone == 1 || zone == 2) {
        void* pl = (zone == 1) ? PlayerOf(model) : EnemyOf(model);
        int cnt = BoardCount(pl);
        if (cnt == 0) { Say(Get(Id::PazaakBoardEmpty), true); return; }
        if (col >= cnt) col = cnt - 1;
        CardView c = ReadCard(pl, kPlayerBoardOffset, col);
        FormatCard(c, false, card, sizeof(card));
        Say(card, true);
    } else {
        Say(Get(col == 0 ? Id::PazaakStandLabel : Id::PazaakEndTurnLabel), true);
    }
}

// Suppress the in-world / menu actions that share our physical keys so their
// pollers (which run later in Dispatch) don't act on the same press. Per the
// Claim-vs-Consume rule, Consume works here because we run inside the same
// BeginTick/EndTick window as those pollers.
void ConsumeConflicts() {
    using namespace acc::hotkeys;
    Consume(Action::PartyLeaderAnnounce);  // Tab
    Consume(Action::InteractTarget);       // Enter
    Consume(Action::InteractForceRadial);  // Shift+Enter
    Consume(Action::NavUp);
    Consume(Action::NavDown);
    Consume(Action::NavLeft);
    Consume(Action::NavRight);
    Consume(Action::SubmenuEsc);           // Esc
    Consume(Action::ContainerGiveMode);    // Q/E
    Consume(Action::StoreModeToggle);      // Q/E
}

// Win32-polled shortcuts — letter scancodes the engine drops before the
// manager hook, so we read them directly (mirrors the in-world cycle keys).
// Arrow + Enter navigation goes through TryHandleInput (the manager hook).
void PollShortcuts(void* panel) {
    using namespace acc::hotkeys;
    using namespace acc::strings;
    void* model = GetModel(panel);
    if (!model) return;
    void* player = PlayerOf(model);
    int state = GetState(panel);

    if (Pressed(Action::PazaakReviewHand))  SpeakHand(player);
    if (Pressed(Action::PazaakReviewTable)) SpeakTable(model);

    // Shift+C — opponent's remaining hand cards (count only — the same public
    // info a sighted player reads off the face-down cards, never the values).
    if (Pressed(Action::PazaakOppHand)) {
        void* e = EnemyOf(model);
        int n = 0;
        for (int i = 0; i < kHandSlots; ++i)
            if (ReadCard(e, kPlayerHandOffset, i).index >= 0) ++n;
        char msg[96];
        snprintf(msg, sizeof(msg), Get(Id::PazaakFmtOppHand), n);
        Say(msg, true);
    }

    // s / e quick shortcuts for Stand / End turn (also reachable in the
    // actions zone via arrows + Enter).
    if (Pressed(Action::PazaakStand)) {
        if (state == kStatePlayerInteractive || state == kStatePlayerPlayed) DoStand(panel);
        else Say(Get(Id::PazaakNotYourTurn), true);
    }
    if (Pressed(Action::PazaakEndTurn)) {
        if (state == kStatePlayerInteractive || state == kStatePlayerPlayed) {
            DoEndTurn(panel); Say(Get(Id::PazaakTurnEnded), true);
        } else {
            Say(Get(Id::PazaakNotYourTurn), true);
        }
    }
}

// Enter on the focused zone element (driven from TryHandleInput).
void HandleEnter(void* panel, void* model, int state) {
    using namespace acc::strings;
    if (g_zone == 0) {  // your hand
        if (state != kStatePlayerInteractive) {
            Say(Get(state == kStatePlayerPlayed ? Id::PazaakNoPlayable
                                                : Id::PazaakNotYourTurn), true);
            return;
        }
        void* player = PlayerOf(model);
        CardView c = ReadCard(player, kPlayerHandOffset, g_col);
        if (c.index < 0) { Say(Get(Id::PazaakNoPlayable), true); return; }
        if (IsSignChoiceCard(c.index)) {
            // +/- card → sign sub-zone, default to the plus face. KOTOR 2's
            // value card also carries a magnitude the player picks; adopt
            // whatever face it currently shows so the first Up/Down is a real
            // change rather than a no-op re-announcement.
            g_optMode = true; g_optSlot = g_col; g_optSign = 0;
            g_optValue = (c.value == 2) ? 2 : 1;
            Say(Get(IsValueChoiceCard(c.index) ? Id::PazaakChooseSignAndValue
                                               : Id::PazaakChooseSign), true);
            SpeakSignOption(c.index, /*interrupt=*/false);
        } else {
            PlayAndAnnounce(panel, player, g_col);
        }
    } else if (g_zone == 3) {  // actions
        if (state == kStatePlayerInteractive || state == kStatePlayerPlayed) {
            if (g_col == 0) DoStand(panel);
            else { DoEndTurn(panel); Say(Get(Id::PazaakTurnEnded), true); }
        } else {
            Say(Get(Id::PazaakNotYourTurn), true);
        }
    }
    // zones 1 / 2 (boards) are read-only — Enter does nothing.
}

// ---- Wager popup: observe value + Left/Right hold-to-repeat stepper -------
void* g_wagerPanel = nullptr;
int   g_wagerLast  = -1;

// The engine's wager only ever changes by ±1 per input event (the less/more
// CSWGuiSpeedButtons just call HandleInputEvent 0x2f/0x30 — see decomp); a
// sighted player holds the mouse and the SpeedButton auto-repeats. We mask
// those buttons out of the chain (menus_chain RebindChain) and drive the wager
// with Left/Right instead, polled here so we can mirror the hold-to-repeat:
// a tap is one credit, holding accelerates toward the cap/floor. Per-step
// click feedback comes free (HandleInputEvent plays the wager-click sound);
// the spoken value is suppressed during the race and announced once on
// release (Option A).
int      g_wagerHeldDir   = 0;  // -1 decrease, +1 increase, 0 idle
unsigned g_wagerHoldStart = 0;  // GetTickCount at key-down
unsigned g_wagerNextStep  = 0;  // earliest tick for the next auto-repeat step
bool     g_wagerRepeating = false;  // crossed from single tap into auto-repeat

constexpr unsigned kWagerHoldDelayMs  = 350;   // grace before auto-repeat starts
constexpr unsigned kWagerRepeatSlowMs = 140;   // first repeat interval (~7/s)
constexpr unsigned kWagerRepeatFastMs = 30;    // terminal interval (~33/s)
constexpr unsigned kWagerRampMs       = 1200;  // accel span from slow to fast

void StepWager(void* fg, int dir) {
    DispatchWagerInput(fg, dir < 0 ? kWagerLessCode : kWagerMoreCode);
}

// Run every tick while the wager popup may be foreground (from Tick). Owns the
// wager Left/Right stepper and announces the amount when it settles.
void ServiceWagerPopup(void* fg) {
    using namespace acc::strings;
    if (!fg || acc::engine::IdentifyPanel(fg) != acc::engine::PanelKind::PazaakWager) {
        g_wagerPanel     = nullptr;
        g_wagerLast      = -1;
        g_wagerHeldDir   = 0;
        g_wagerRepeating = false;
        return;
    }

    int cur = -1, max = -1;
    ReadIntAt(fg, kWagerCurOffset, &cur);
    ReadIntAt(fg, kWagerMaxOffset, &max);
    bool firstSight = (fg != g_wagerPanel);

    bool leftDown  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
    bool rightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    int  dir = (leftDown == rightDown) ? 0 : (leftDown ? -1 : +1);
    unsigned now = GetTickCount();

    if (firstSight) {
        // Adopt the current key state without firing — a key held at popup-open
        // (left over from navigation) shouldn't auto-step.
        g_wagerHeldDir   = dir;
        g_wagerRepeating = false;
    } else if (dir != 0 && dir != g_wagerHeldDir) {
        // Rising edge / direction flip → one immediate step. The single-step
        // value is spoken by the change-announce below.
        StepWager(fg, dir);
        g_wagerHeldDir   = dir;
        g_wagerHoldStart = now;
        g_wagerNextStep  = now + kWagerHoldDelayMs;
        g_wagerRepeating = false;
    } else if (dir != 0) {  // same direction still held → auto-repeat
        if ((int)(now - g_wagerNextStep) >= 0) {
            StepWager(fg, dir);
            g_wagerRepeating = true;
            unsigned t = now - g_wagerHoldStart - kWagerHoldDelayMs;
            unsigned interval = (t >= kWagerRampMs)
                ? kWagerRepeatFastMs
                : kWagerRepeatSlowMs -
                      (kWagerRepeatSlowMs - kWagerRepeatFastMs) * t / kWagerRampMs;
            g_wagerNextStep = now + interval;
        }
    } else {  // no direction held
        if (g_wagerRepeating) {
            // Option A: silent during the race, announce the final value on
            // release. Stamp g_wagerLast so the change-announce stays quiet.
            char msg[96];
            snprintf(msg, sizeof(msg), Get(Id::PazaakFmtWager), cur, max);
            Say(msg, true);
            g_wagerLast = cur;
        }
        g_wagerHeldDir   = 0;
        g_wagerRepeating = false;
    }

    // Value-change announce for taps (and any non-key change). Suppressed
    // mid-race; the release branch above owns that announcement.
    if (!firstSight && !g_wagerRepeating && cur != g_wagerLast) {
        char msg[96];
        snprintf(msg, sizeof(msg), Get(Id::PazaakFmtWager), cur, max);
        Say(msg, true);
    }
    g_wagerPanel = fg;
    g_wagerLast  = cur;
}

void ResetState() {
    g_panel = nullptr;
    g_prev = Snap{};
    g_zone = 0;
    g_col = 0;
    g_optMode = false;
    g_optSlot = -1;
    g_optSign = 0;
    g_optValue = 1;
    g_started = false;
    g_resultAnnounced = false;
    g_boardForeground = false;
}

}  // namespace

bool IsSignChoiceCard(int index) {
    if (index >= 12 && index <= 17) return true;                 // ±1..±6, both games
    return acc::game::IsKotor2() && index == kCardTiebreaker;    // ±1 T, KOTOR 2 only
}

// Render one of the six classic ±N flip faces into `out`.
void FormatFlipFaces(int mag, int flip, CardContext ctx, char* out, size_t n) {
    using namespace acc::strings;
    if (ctx == CardContext::Hand) {
        char face[48], both[48];
        snprintf(face, sizeof(face), Get(flip ? Id::PazaakFmtMinus : Id::PazaakFmtPlus), mag);
        snprintf(both, sizeof(both), Get(Id::PazaakFmtFlipBoth), mag);
        snprintf(out, n, Get(Id::PazaakFmtFlipCurrently), both, face);
    } else if (ctx == CardContext::Collection) {
        // Deck builder: a flip card's sign isn't decided until it's played.
        snprintf(out, n, Get(Id::PazaakFmtFlipBoth), mag);
    } else {  // Committed
        snprintf(out, n, Get(flip ? Id::PazaakFmtMinus : Id::PazaakFmtPlus), mag);
    }
}

// Shared card-label synthesizer — see pazaak.h. Used by the board game (via
// the FormatCard wrapper above) and the side-deck builder (menus_pazaakdeck).
//
// The two games number their cards identically up to 17 and diverge after:
//
//   0..5    +1..+6                     both
//   6..11   -1..-6                     both
//   12..17  ±1..±6 (sign at play time)  both
//   18..22  ±1 T / D / 2&4 / 3&6 / value   KOTOR 2 ONLY
//   18..27  main deck 1..10            KOTOR 1
//   23..32  main deck 1..10            KOTOR 2
//
// That is not a guess: KOTOR 2's CSWGuiPazaakCard::SetCard @0x00885FE0 switches
// on the index to choose the card art — PCARDS_POS_P for 0..5, PCARDS_NEG_P for
// 6..11, PCARDS_DBLPOS_P / DBLNEG_P for 12..17, PCARDS_GOLD_P for 0x12..0x16 and
// PCARDS_GENERIC_P for 0x17..0x20 — and prints the glyphs "+1T"/"-1T", "D",
// "2&4", "3&6" for the gold five. The deck shuffler @0x008FEBB0 independently
// fills all forty main-deck cards with `i % 10 + 0x17`.
void FormatCardLabel(int index, int flip, CardContext ctx, char* out, size_t n,
                     int value) {
    using namespace acc::strings;
    if (!out || n == 0) return;
    out[0] = '\0';
    if (index < 0) {
        snprintf(out, n, "%s", Get(index == -2 ? Id::PazaakFaceDown : Id::PazaakEmpty));
        return;
    }
    if (index <= 5)  { snprintf(out, n, Get(Id::PazaakFmtPlus),  index + 1); return; }
    if (index <= 11) { snprintf(out, n, Get(Id::PazaakFmtMinus), index - 5); return; }
    if (index <= 17) { FormatFlipFaces(index - 11, flip, ctx, out, n); return; }

    if (!acc::game::IsKotor2()) {
        // 18..27 main deck, printed as its face value 1..10.
        snprintf(out, n, Get(Id::PazaakFmtPlain), index - FirstMainDeckIndex() + 1);
        return;
    }

    // ---- KOTOR 2 special cards (18..22) ----
    switch (index) {
        case kCardTiebreaker: {
            // A ±1 that also wins ties. Say it as a ±1 first — that is the part
            // that changes the arithmetic — then name what makes it special.
            char base[96];
            FormatFlipFaces(1, flip, ctx, base, sizeof(base));
            snprintf(out, n, Get(Id::PazaakFmtCardTiebreaker), base);
            return;
        }
        case kCardDouble:
        case kCardTwoFour:
        case kCardThreeSix:
        case kCardValue: {
            const Id nameId = (index == kCardDouble)  ? Id::PazaakCardDouble
                            : (index == kCardTwoFour) ? Id::PazaakCardTwoFour
                            : (index == kCardThreeSix) ? Id::PazaakCardThreeSix
                                                       : Id::PazaakCardValue;
            // Undecided (in hand or in the collection) — just the card's name.
            // Once the engine has resolved a value into the card's third dword,
            // append it, signed for the value card the same way a played flip
            // card reads.
            if (value == 0) { snprintf(out, n, "%s", Get(nameId)); return; }
            char face[48];
            const bool negative = (index == kCardValue) && flip;
            snprintf(face, sizeof(face),
                     Get(negative ? Id::PazaakFmtMinus : Id::PazaakFmtPlus), value);
            snprintf(out, n, Get(Id::PazaakFmtFlipCurrently), Get(nameId), face);
            return;
        }
        default:
            break;
    }
    // 23..32 main deck, printed as its face value 1..10.
    snprintf(out, n, Get(Id::PazaakFmtPlain), index - FirstMainDeckIndex() + 1);
}

// Arrow + Enter navigator, driven from the manager input hook so the generic
// chain can't also act on the board's controls. Returns true (+ rv) when it
// consumes the key. Letters fall through to PollShortcuts; Esc (outside the
// sub-zone) falls through to the engine's quit-confirm.
bool TryHandleInput(void* /*activePanel*/, int param_1, int param_2, int& rv) {
    // Both games since the minigame port (2026-08-15). g_boardForeground is set
    // by Tick(), which only arms once it has acquired a real board panel, so
    // this stays inert everywhere else on its own.
    if (!g_boardForeground || !g_panel) return false;
    void* panel = g_panel;

    const bool isNav   = (param_1 == kInputNavUp   || param_1 == kInputNavDown ||
                          param_1 == kInputNavLeft || param_1 == kInputNavRight);
    const bool isEnter = (param_1 == kInputEnter1  || param_1 == kInputEnter2);
    const bool isEsc   = (param_1 == kInputEsc1    || param_1 == kInputEsc2);

    // Card-options sub-zone (a +/- card mid-play) owns the arrows, Enter, Esc.
    // Left/Right pick the sign; on KOTOR 2's value card Up/Down also pick the
    // face (1 or 2), mirroring what its BTN_CHANGE does.
    if (g_optMode) {
        if (!isNav && !isEnter && !isEsc) return false;
        rv = 1;
        if (param_2 == 0) return true;
        void* model  = GetModel(panel);
        void* player = model ? PlayerOf(model) : nullptr;
        if (param_1 == kInputNavLeft || param_1 == kInputNavRight) {
            g_optSign ^= 1;
            if (player) {
                SpeakSignOption(ReadCard(player, kPlayerHandOffset, g_optSlot).index,
                                /*interrupt=*/true);
            }
        } else if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
            if (player) {
                CardView c = ReadCard(player, kPlayerHandOffset, g_optSlot);
                // Only the value card has a second face; on every other card
                // this is a deliberate no-op that still re-reads the current
                // one, so Up/Down never lands on silence.
                if (IsValueChoiceCard(c.index)) g_optValue = (g_optValue == 1) ? 2 : 1;
                SpeakSignOption(c.index, /*interrupt=*/true);
            }
        } else if (isEnter) {
            if (player) {
                auto* card = reinterpret_cast<unsigned char*>(player) +
                             kPlayerHandOffset + (size_t)g_optSlot * kCardStride;
                CardView c = ReadCard(player, kPlayerHandOffset, g_optSlot);
                WriteIntAt(card, kCardFlipOffset, g_optSign);
                // The value card's face lives in the third dword — the same
                // field the engine's own BTN_CHANGE writes.
                if (IsValueChoiceCard(c.index)) {
                    WriteIntAt(card, kCardValueOffset, g_optValue);
                }
                int slot = g_optSlot;
                g_optMode = false; g_optSlot = -1;
                PlayAndAnnounce(panel, player, slot);
            } else {
                g_optMode = false; g_optSlot = -1;
            }
        } else {  // Esc cancels the sign chooser without playing.
            g_optMode = false; g_optSlot = -1;
            Say(acc::strings::Get(acc::strings::Id::PazaakCancelled), true);
        }
        return true;
    }

    if (!isNav && !isEnter) return false;
    rv = 1;
    if (param_2 == 0) return true;

    void* model = GetModel(panel);
    if (!model) return true;
    int state = GetState(panel);

    if (isNav) {
        if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
            const int dir = (param_1 == kInputNavUp) ? -1 : 1;
            if ((dir < 0 && g_zone > 0) || (dir > 0 && g_zone < 3)) {
                g_zone += dir;
                // Land on the first filled hand slot when entering the hand;
                // other zones have no gaps, so just clamp.
                if (g_zone == 0)
                    g_col = FirstHandCol(PlayerOf(model));
                else if (g_col >= ZoneLen(model, g_zone))
                    g_col = 0;
                AnnounceZoneEntry(model, g_zone);
            }
        } else if (param_1 == kInputNavLeft || param_1 == kInputNavRight) {
            const int dir = (param_1 == kInputNavLeft) ? -1 : 1;
            int next;
            if (g_zone == 0) {
                next = NextHandCol(PlayerOf(model), g_col, dir);  // skip empty slots
            } else {
                next = g_col + dir;
                const int len = ZoneLen(model, g_zone);
                if (next < 0 || next >= len) next = g_col;
            }
            if (next != g_col) { g_col = next; AnnounceZoneElement(model, g_zone, g_col); }
        }
        return true;
    }

    HandleEnter(panel, model, state);  // Enter
    return true;
}

bool IsBoardForeground() { return g_boardForeground; }

void DispatchWagerInput(void* panel, int code) {
    if (!panel) return;
    __try {
        auto fn = reinterpret_cast<PFN_WagerHandleInput>(kAddrWagerHandleInput);
        fn(panel, code, 1);
        acclog::Write("Pazaak", "wager HandleInputEvent panel=%p code=0x%x",
                      panel, code);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Pazaak", "wager HandleInputEvent SEH panel=%p code=0x%x",
                      panel, code);
    }
}

void Tick() {
    void* mgr = nullptr;
    __try { mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!mgr) { if (g_panel) ResetState(); return; }

    void* fg = acc::engine::GetForegroundPanel(mgr);

    // The wager popup precedes (and is a different panel from) the board.
    ServiceWagerPopup(fg);

    // Drop the tracked panel once it leaves the manager (game ended / panel
    // destroyed). IsPanelInManager is deref-free, so it's safe on a stale
    // pointer.
    if (g_panel && !acc::engine::IsPanelInManager(g_panel)) {
        acclog::Write("Pazaak", "board panel %p left manager; game ended", g_panel);
        ResetState();
    }

    // Acquire the board panel: an unknown-kind foreground modal whose deep
    // fields match the model layout. Only probed while untracked, so the
    // structural read isn't paid during normal play.
    if (!g_panel && fg &&
        acc::engine::IdentifyPanel(fg) == acc::engine::PanelKind::Unknown &&
        LooksLikePazaak(fg)) {
        g_panel = fg;
        __try { g_learnedVtable = *reinterpret_cast<uintptr_t*>(fg); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_learnedVtable = 0; }
        // Disable the visual tutorial so its help popups can't stack on top of
        // the board and freeze the turn engine (see kTutorialActiveOffset).
        WriteIntAt(fg, kTutorialActiveOffset, 0);
        g_prev = Snap{};
        g_started = false;
        g_resultAnnounced = false;
        g_zone = 0;
        g_col = 0;
        acclog::Write("Pazaak", "acquired board panel=%p vtable=0x%08x",
                      fg, (unsigned)g_learnedVtable);
    }
    if (!g_panel) return;

    // One-time orientation cue.
    if (!g_started) {
        g_started = true;
        Say(acc::strings::Get(acc::strings::Id::PazaakStart), true);
    }

    // Snapshot + announce — runs even while a result message box is layered
    // above the board (that's exactly when win/lose/tie lands).
    Snap cur = ReadSnap(g_panel);
    if (cur.valid) {
        if (g_prev.valid && cur.pBoard < g_prev.pBoard) {
            // New set dealt (board cleared) — re-arm the result latch + cursor.
            g_resultAnnounced = false;
            g_zone = 0;
            g_col = 0;
        }
        AnnounceDeltas(g_panel, cur);
        g_prev = cur;
    }

    // Input — only when the board itself is the active foreground panel.
    g_boardForeground = (fg == g_panel);
    if (g_boardForeground) {
        ConsumeConflicts();
        PollShortcuts(g_panel);
    }
}

}  // namespace acc::pazaak
