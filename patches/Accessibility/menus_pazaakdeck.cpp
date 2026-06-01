// Pazaak side-deck builder accessibility — see menus_pazaakdeck.h.
//
// Integrates with the menu chain: ExtractCardLabel plugs into the per-kind
// extractor ladder (menus_extract.cpp) so focused card widgets announce a
// real label, and IsChainDecorative plugs into RebindChain's decorative
// filter (menus_chain.cpp) so the overlay labels and unaddable cards drop out
// of navigation. Card identity comes from the panel's card model
// (card_counts / sidedeck), not the widgets — AddChosenCard writes the model
// but doesn't call SetCard on the chosen-slot widgets.

#include "menus_pazaakdeck.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_input.h"    // kInputNav* / kInputEnter*
#include "engine_manager.h"  // IsPanelInManager
#include "engine_offsets.h"  // CExoArrayList, kPanelControlsOffset, kControlIdOffset
#include "menus_pending.h"   // QueueActivate (Play button)
#include "pazaak.h"          // FormatCardLabel, CardContext
#include "prism.h"
#include "strings.h"

namespace acc::menus::pazaakdeck {

namespace {

constexpr uintptr_t kVtablePazaakStart = 0x007532e8;  // CSWGuiPazaakStart
constexpr uintptr_t kVtablePazaakCard  = 0x007531c0;  // CSWGuiPazaakCard
constexpr uintptr_t kVtableCSWGuiLabel = 0x0073e5b8;  // CSWGuiLabel (overlay text)

constexpr size_t kAllCardsOff    = 0x1A4;   // all_cards[18]   (stride 0x31C)
constexpr size_t kSidedeckGuiOff = 0x501C;  // sidedeck_gui[10] (stride 0x31C)
constexpr size_t kCardCountsOff  = 0x755C;  // int card_counts[18]
constexpr size_t kSidedeckOff    = 0x75A4;  // CPazaakCard sidedeck[10]
constexpr size_t kCardStride     = 0x31C;
constexpr int    kNumTypes = 18;
constexpr int    kNumSlots = 10;

bool VtableIs(void* p, uintptr_t vt) {
    __try { return *reinterpret_cast<uintptr_t*>(p) == vt; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int ReadIntAt(void* base, size_t off) {
    __try { return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(base) + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Classify a card widget by its position in the panel's embedded arrays.
// outAvailable=true → available-grid index; false → chosen-slot index.
// Returns false if `control` isn't an element of either array.
bool Classify(void* panel, void* control, bool& outAvailable, int& outIdx) {
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* c    = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t a = c - (base + kAllCardsOff);
    if (a >= 0 && static_cast<size_t>(a) < static_cast<size_t>(kNumTypes) * kCardStride &&
        a % static_cast<ptrdiff_t>(kCardStride) == 0) {
        outAvailable = true; outIdx = static_cast<int>(a / static_cast<ptrdiff_t>(kCardStride));
        return true;
    }
    ptrdiff_t s = c - (base + kSidedeckGuiOff);
    if (s >= 0 && static_cast<size_t>(s) < static_cast<size_t>(kNumSlots) * kCardStride &&
        s % static_cast<ptrdiff_t>(kCardStride) == 0) {
        outAvailable = false; outIdx = static_cast<int>(s / static_cast<ptrdiff_t>(kCardStride));
        return true;
    }
    return false;
}

}  // namespace

bool IsDeckPanel(void* panel) {
    return panel && VtableIs(panel, kVtablePazaakStart);
}

bool ExtractCardLabel(void* panel, void* control, char* outBuf, size_t bufSize) {
    using namespace acc::strings;
    if (!IsDeckPanel(panel) || !control || !outBuf || bufSize == 0) return false;
    if (!VtableIs(control, kVtablePazaakCard)) return false;

    bool avail; int idx;
    if (!Classify(panel, control, avail, idx)) return false;

    char name[80];
    if (avail) {
        if (idx < 0 || idx >= kNumTypes) return false;
        int count = ReadIntAt(panel, kCardCountsOff + static_cast<size_t>(idx) * 4);
        // all_cards[i] always represents card type i.
        acc::pazaak::FormatCardLabel(idx, 0, acc::pazaak::CardContext::Collection,
                                     name, sizeof(name));
        if (count <= 0) snprintf(outBuf, bufSize, Get(Id::PazaakDeckNoneLeft), name);
        else            snprintf(outBuf, bufSize, Get(Id::PazaakDeckAvailable), name, count);
        return true;
    }

    if (idx < 0 || idx >= kNumSlots) return false;
    size_t slotOff = kSidedeckOff + static_cast<size_t>(idx) * 8;  // CPazaakCard{index,is_flipped}
    int cardIdx = ReadIntAt(panel, slotOff + 0);
    int flip    = ReadIntAt(panel, slotOff + 4);
    if (cardIdx < 0) {
        snprintf(outBuf, bufSize, Get(Id::PazaakDeckSlotEmpty), idx + 1);
        return true;
    }
    acc::pazaak::FormatCardLabel(cardIdx, flip, acc::pazaak::CardContext::Collection,
                                 name, sizeof(name));
    snprintf(outBuf, bufSize, Get(Id::PazaakDeckSlotFilled), idx + 1, name);
    return true;
}

bool IsChainDecorative(void* panel, void* control) {
    if (!IsDeckPanel(panel) || !control) return false;
    // Drop every label control — value / count / section-title overlays. Their
    // info is folded into the card labels (count) and the panel-title speech.
    if (VtableIs(control, kVtableCSWGuiLabel)) return true;
    // Drop available cards with no copies left to place — they aren't
    // actionable (AddChosenCard refuses at count 0), so the grid steps only
    // through addable cards.
    if (VtableIs(control, kVtablePazaakCard)) {
        bool avail; int idx;
        if (Classify(panel, control, avail, idx) && avail && idx >= 0 && idx < kNumTypes) {
            if (ReadIntAt(panel, kCardCountsOff + static_cast<size_t>(idx) * 4) <= 0) return true;
        }
    }
    return false;
}

// ---- 3-row arrow navigator -------------------------------------------------

namespace {

typedef int (__thiscall* PFN_AddChosenCard)(void* panel, int cardType, int slot);
typedef int (__thiscall* PFN_RemoveChosenCard)(void* panel, int slot);
constexpr uintptr_t kAddrAddChosenCard    = 0x0067fb10;
constexpr uintptr_t kAddrRemoveChosenCard = 0x0067fd10;
constexpr int kControlPlayId = 78;  // "Spielen" button (.gui id, locale-stable)

enum class Op { None, Add, Remove, Play };
int   g_row = 0;          // 0 collection, 1 deck slots, 2 controls
int   g_col = 0;
void* g_navPanel = nullptr;
// Deferred action (set on Enter, drained in Tick off the input-hook stack).
Op    g_op = Op::None;
int   g_opArg = 0;        // card type (Add) or slot (Remove)
void* g_opPanel = nullptr;

// Owned card types, ascending. A type is "owned" if it has copies left in the
// collection OR copies currently placed in the deck — so the row is stable as
// cards move between the two zones (the total is conserved).
int BuildCollection(void* panel, int* outTypes) {
    int n = 0;
    for (int i = 0; i < kNumTypes; ++i) {
        int owned = ReadIntAt(panel, kCardCountsOff + static_cast<size_t>(i) * 4);
        if (owned <= 0) {
            for (int s = 0; s < kNumSlots; ++s) {
                if (ReadIntAt(panel, kSidedeckOff + static_cast<size_t>(s) * 8) == i) {
                    owned = 1; break;
                }
            }
        }
        if (owned > 0) outTypes[n++] = i;
    }
    return n;
}

int DeckFilledCount(void* panel) {
    int c = 0;
    for (int s = 0; s < kNumSlots; ++s)
        if (ReadIntAt(panel, kSidedeckOff + static_cast<size_t>(s) * 8) >= 0) ++c;
    return c;
}

int RowLength(void* panel, int row) {
    if (row == 0) { int t[kNumTypes]; return BuildCollection(panel, t); }
    if (row == 1) return kNumSlots;
    return 1;  // controls: Play only
}

void* FindControlById(void* panel, int id) {
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (!list->data || list->size <= 0) return nullptr;
        int n = list->size > 256 ? 256 : list->size;
        for (int i = 0; i < n; ++i) {
            void* c = list->data[i];
            if (!c) continue;
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
            if (cid == id) return c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

void Speak(const char* s) { if (s && s[0]) prism::Speak(s, /*interrupt=*/true); }

void AnnounceFocus(void* panel) {
    using namespace acc::strings;
    char name[80], msg[160];
    if (g_row == 0) {
        int types[kNumTypes];
        int cnt = BuildCollection(panel, types);
        if (cnt == 0) return;
        if (g_col >= cnt) g_col = cnt - 1;
        if (g_col < 0) g_col = 0;
        int t = types[g_col];
        int count = ReadIntAt(panel, kCardCountsOff + static_cast<size_t>(t) * 4);
        acc::pazaak::FormatCardLabel(t, 0, acc::pazaak::CardContext::Collection,
                                     name, sizeof(name));
        if (count <= 0) snprintf(msg, sizeof(msg), Get(Id::PazaakDeckNoneLeft), name);
        else            snprintf(msg, sizeof(msg), Get(Id::PazaakDeckAvailable), name, count);
        Speak(msg);
    } else if (g_row == 1) {
        if (g_col < 0) g_col = 0;
        if (g_col >= kNumSlots) g_col = kNumSlots - 1;
        size_t off = kSidedeckOff + static_cast<size_t>(g_col) * 8;
        int idx = ReadIntAt(panel, off);
        int flip = ReadIntAt(panel, off + 4);
        if (idx < 0) {
            snprintf(msg, sizeof(msg), Get(Id::PazaakDeckSlotEmpty), g_col + 1);
        } else {
            acc::pazaak::FormatCardLabel(idx, flip, acc::pazaak::CardContext::Collection,
                                         name, sizeof(name));
            snprintf(msg, sizeof(msg), Get(Id::PazaakDeckSlotFilled), g_col + 1, name);
        }
        Speak(msg);
    } else {
        snprintf(msg, sizeof(msg), Get(Id::PazaakDeckPlay), DeckFilledCount(panel));
        Speak(msg);
    }
}

}  // namespace

bool TryHandleInput(void* panel, int param_1, int param_2, int& rv) {
    if (!IsDeckPanel(panel)) return false;
    if (panel != g_navPanel) { g_navPanel = panel; g_row = 0; g_col = 0; }

    const bool isNav   = (param_1 == kInputNavUp || param_1 == kInputNavDown ||
                          param_1 == kInputNavLeft || param_1 == kInputNavRight);
    const bool isEnter = (param_1 == kInputEnter1 || param_1 == kInputEnter2);
    if (!isNav && !isEnter) return false;  // let letters / Esc / etc. pass through

    rv = 1;                       // consume both edges so the engine never acts
    if (param_2 == 0) return true;  // release: consumed, no action

    if (isNav) {
        if (param_1 == kInputNavUp)         { if (g_row > 0) --g_row; }
        else if (param_1 == kInputNavDown)  { if (g_row < 2) ++g_row; }
        else if (param_1 == kInputNavLeft)  { if (g_col > 0) --g_col; }
        else if (param_1 == kInputNavRight) {
            int len = RowLength(panel, g_row);
            if (g_col < len - 1) ++g_col;
        }
        int len = RowLength(panel, g_row);
        if (g_col >= len) g_col = (len > 0) ? len - 1 : 0;
        if (g_col < 0) g_col = 0;
        AnnounceFocus(panel);
        return true;
    }

    // Enter: stage the deep action for Tick.
    if (g_op == Op::None) {
        if (g_row == 0) {
            int types[kNumTypes];
            int cnt = BuildCollection(panel, types);
            if (cnt > 0 && g_col >= 0 && g_col < cnt) {
                g_op = Op::Add; g_opArg = types[g_col]; g_opPanel = panel;
            }
        } else if (g_row == 1) {
            int idx = ReadIntAt(panel, kSidedeckOff + static_cast<size_t>(g_col) * 8);
            if (idx >= 0) { g_op = Op::Remove; g_opArg = g_col; g_opPanel = panel; }
            // empty slot: nothing to remove — stay silent.
        } else {
            g_op = Op::Play; g_opPanel = panel;
        }
    }
    return true;
}

void Tick() {
    if (g_op == Op::None) return;
    Op    op    = g_op;
    void* panel = g_opPanel;
    int   arg   = g_opArg;
    g_op = Op::None; g_opPanel = nullptr;

    if (!panel || !acc::engine::IsPanelInManager(panel) || !IsDeckPanel(panel)) return;

    using namespace acc::strings;
    char name[80], msg[160];

    if (op == Op::Add) {
        acc::pazaak::FormatCardLabel(arg, 0, acc::pazaak::CardContext::Collection,
                                     name, sizeof(name));
        int r = 0;
        __try { r = reinterpret_cast<PFN_AddChosenCard>(kAddrAddChosenCard)(panel, arg, -1); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = 0; }
        if (r) {
            snprintf(msg, sizeof(msg), Get(Id::PazaakDeckAdded), name, DeckFilledCount(panel));
            Speak(msg);
        } else {
            Speak(Get(Id::PazaakDeckFull));
        }
    } else if (op == Op::Remove) {
        int idx = ReadIntAt(panel, kSidedeckOff + static_cast<size_t>(arg) * 8);  // before removal
        acc::pazaak::FormatCardLabel(idx, 0, acc::pazaak::CardContext::Collection,
                                     name, sizeof(name));
        int r = 0;
        __try { r = reinterpret_cast<PFN_RemoveChosenCard>(kAddrRemoveChosenCard)(panel, arg); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = 0; }
        if (r) {
            snprintf(msg, sizeof(msg), Get(Id::PazaakDeckRemoved), name);
            Speak(msg);
        }
    } else if (op == Op::Play) {
        void* btn = FindControlById(panel, kControlPlayId);
        if (btn) acc::menus::pending::QueueActivate(btn);
    }
}

}  // namespace acc::menus::pazaakdeck
