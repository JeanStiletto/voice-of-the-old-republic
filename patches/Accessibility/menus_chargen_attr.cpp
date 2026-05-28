// chargen attributes panel tweaks.
// See menus_chargen_attr.h for the design rationale.

#include <windows.h>
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "menus_chargen_attr.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chain.h"
#include "menus_extract.h"
#include "strings.h"
#include "prism.h"

namespace acc::menus::chargen_attr {

bool IsChargenAttributesPanel(void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiAbilitiesCharGen;
}

int AbilityIndexFromButton(void* panel, void* control) {
    if (!IsChargenAttributesPanel(panel) || !control) return -1;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* btn  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = btn - base;
    if (off < (ptrdiff_t)kAbilitiesCharGenButtonsArrayOffset) {
        return -1;
    }
    ptrdiff_t rel = off - (ptrdiff_t)kAbilitiesCharGenButtonsArrayOffset;
    if (rel % (ptrdiff_t)kCSWGuiButtonSize != 0) return -1;
    int i = (int)(rel / (ptrdiff_t)kCSWGuiButtonSize);
    if (i < 0 || i >= kAbilitiesCharGenAbilityCount) return -1;
    return i;
}

void SyncSelectedAbilityFromChainFocus() {
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsChargenAttributesPanel(panel)) return;
    if (acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    void* focused =
        acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    int idx = AbilityIndexFromButton(panel, focused);
    if (idx < 0) return;

    auto* base = reinterpret_cast<unsigned char*>(panel);
    int* slot = reinterpret_cast<int*>(
        base + kAbilitiesCharGenSelectedAbilityOffset);
    int prev = 0;
    __try {
        prev = *slot;
        if (prev != idx) {
            *slot = idx;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (prev != idx) {
        acclog::Write("Menus.ChargenAttr",
                      "selected_ability %d -> %d (focused=%p)",
                      prev, idx, focused);
    }
}

int RowPitchForCursorWarp(void* panel, void* control) {
    if (AbilityIndexFromButton(panel, control) < 0) return 0;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    int top0 = 0, top1 = 0;
    __try {
        // CSWGuiControl extent: { left, top, width, height } as four ints
        // starting at +kControlExtentOffset. We want top, which is the
        // second int (offset +0x4 within the extent struct).
        auto* ext0 = reinterpret_cast<int*>(
            base + kAbilitiesCharGenButtonsArrayOffset + kControlExtentOffset);
        auto* ext1 = reinterpret_cast<int*>(
            base + kAbilitiesCharGenButtonsArrayOffset + kCSWGuiButtonSize +
            kControlExtentOffset);
        top0 = ext0[1];
        top1 = ext1[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    int pitch = top1 - top0;
    // Sanity-bound: real pitch is ~45 px; reject anything outside a
    // plausible UI row spacing range to avoid garbage offsets if a
    // future build moves the layout.
    if (pitch <= 0 || pitch > 100) return 0;
    return pitch;
}

void CaptureLabelsIfApplicable(void* panel) {
    if (!IsChargenAttributesPanel(panel)) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    for (int i = 0; i < kAbilitiesCharGenAbilityCount; ++i) {
        void* labelCtl = base + kAbilitiesCharGenLabelsArrayOffset +
                         (size_t)i * kCSWGuiLabelSize;
        void* buttonCtl = base + kAbilitiesCharGenButtonsArrayOffset +
                          (size_t)i * kCSWGuiButtonSize;

        char text[64] = {0};
        bool got = false;
        __try {
            if (acc::engine::ReadGuiString(
                    labelCtl, kLabelGuiStringPtrOffset,
                    text, sizeof(text)) && text[0] != '\0') {
                got = true;
            } else if (acc::engine::ExtractTextOrStrRefIndirect(
                           labelCtl,
                           kLabelTextOffset,
                           kLabelStrRefOffset,
                           kLabelTextObjectOffset,
                           text, sizeof(text)) &&
                       text[0] != '\0') {
                got = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            got = false;
        }

        if (got) {
            acc::menus::extract::CaptureCycleCategory(buttonCtl, text);
            acclog::Write("Menus.ChargenAttr",
                          "capture[%d] button=%p label=\"%s\"",
                          i, buttonCtl, text);
        }
    }
}

namespace {

}  // namespace

namespace {

// Parse the engine-rendered ability value text ("8", "10", "18") into
// an int. Returns -1 on parse failure or out-of-range. Tolerant of
// transient states the engine may briefly render (single dash, empty)
// — caller treats -1 as "skip computed suffix".
int ParseAbilityValueText(const char* text) {
    if (!text || !text[0]) return -1;
    int v = 0;
    bool sawDigit = false;
    for (const char* p = text; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            sawDigit = true;
        } else if (sawDigit) {
            break;
        }
    }
    if (!sawDigit) return -1;
    if (v < 1 || v > 30) return -1;
    return v;
}

// D&D 3.5 ability modifier: floor((value - 10) / 2). C++ integer
// division truncates toward zero, so we adjust for negative deltas
// to get true floor (e.g. -1 → -1, not 0).
int ComputeAbilityModifier(int value) {
    int delta = value - 10;
    return (delta >= 0) ? (delta / 2) : ((delta - 1) / 2);
}

// Engine accessor for the point-buy cost of the next +1 increment.
// Signature per SARIF is `int __thiscall(int param_1)`. First attempt
// passing param_1 = ability index (0..5 in struct order) returned 1
// for every ability regardless of value (verified in
// patch-20260509-090132.log: STR at 17 returned cost=1 when the actual
// deduction was 3). Param is therefore NOT the ability index — we now
// pass the ability's CURRENT VALUE, which matches the engine's
// "cost to push from this value to value+1" curve.
typedef int (__thiscall* PFN_GetAbilityPointCost)(void* this_, int param);

int ReadEngineAbilityCost(void* panel, int currentValue) {
    if (!panel) return -1;
    if (currentValue < 0) return -1;
    int cost = -1;
    __try {
        auto fn = reinterpret_cast<PFN_GetAbilityPointCost>(
            kAddrCSWGuiAbilitiesCharGenGetCost);
        cost = fn(panel, currentValue);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    // Bound-check: chargen costs observed in-engine are 1..3. Anything
    // else is suspect (uninitialized return, bad calling convention,
    // panel in transient state). Treat as missing.
    if (cost < 0 || cost > 99) return -1;
    return cost;
}

// Render the modifier with sign so it reads correctly through Prism:
// positive gets an explicit "+", zero is bare "0" (engine renders
// this as bare "-" which sounds broken), negative carries its own
// sign already.
void FormatModifier(int mod, char* outBuf, size_t bufSize) {
    if (mod == 0) {
        snprintf(outBuf, bufSize, "0");
    } else if (mod > 0) {
        snprintf(outBuf, bufSize, "+%d", mod);
    } else {
        snprintf(outBuf, bufSize, "%d", mod);
    }
}

}  // namespace

void AnnounceChainStepSuffix(void* panel, void* control) {
    int idx = AbilityIndexFromButton(panel, control);
    if (idx < 0) return;

    char valueText[32];
    if (!acc::engine::ReadButtonText(control, valueText, sizeof(valueText))) return;
    int value = ParseAbilityValueText(valueText);
    if (value < 0) return;

    int mod  = ComputeAbilityModifier(value);
    int cost = ReadEngineAbilityCost(panel, value);

    char modText[16];
    FormatModifier(mod, modText, sizeof(modText));

    char costText[8];
    if (cost >= 0) {
        snprintf(costText, sizeof(costText), "%d", cost);
    } else {
        snprintf(costText, sizeof(costText), "?");
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(
                 acc::strings::Id::FmtChargenAttrInfoSuffix),
             modText, costText);

    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.ChargenAttr",
                  "chain-step suffix focus=%p idx=%d value=%d mod=\"%s\" cost=%d",
                  control, idx, value, modText, cost);
}

// Per-ability last-announced modifier and cost. Lets us detect when a
// +/- press tipped the value across a modifier breakpoint (e.g. 9→10
// flips mod from -1 to 0) or a cost-curve breakpoint (e.g. 13→14 flips
// cost from 1 to 2). Sentinel INT_MIN means "not yet tracked"; first
// observation seeds the slot without announcing — the user already
// heard both numbers in the chain-step suffix when they navigated to
// this row.
namespace {
struct ChangeTracker {
    void* panel = nullptr;
    int   lastMod[6]  = { INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN };
    int   lastCost[6] = { INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN };
};
ChangeTracker s_tracker;

void ResetTrackerIfPanelChanged(void* panel) {
    if (s_tracker.panel == panel) return;
    s_tracker.panel = panel;
    for (int i = 0; i < 6; ++i) {
        s_tracker.lastMod[i]  = INT_MIN;
        s_tracker.lastCost[i] = INT_MIN;
    }
}
}  // namespace

bool AnnounceValueChange(void* panel, void* control) {
    int idx = AbilityIndexFromButton(panel, control);
    if (idx < 0) return false;

    char value[32];
    char remaining[32];
    bool gotValue = acc::engine::ReadButtonText(control,
                         value, sizeof(value));
    bool gotRem   = acc::engine::ReadLabelTextAt(panel,
                         kAbilitiesCharGenRemainingValueOffset,
                         remaining, sizeof(remaining));
    if (!gotValue) return false;

    int parsedValue = ParseAbilityValueText(value);
    int newMod      = (parsedValue >= 0)
                          ? ComputeAbilityModifier(parsedValue)
                          : INT_MIN;
    int cost        = (parsedValue >= 0)
                          ? ReadEngineAbilityCost(panel, parsedValue)
                          : -1;

    const char* remText = gotRem ? remaining : "?";
    char costText[8];
    if (cost >= 0) {
        snprintf(costText, sizeof(costText), "%d", cost);
    } else {
        snprintf(costText, sizeof(costText), "?");
    }

    // Modifier and cost change detection: announce each only when
    // this press tipped its value across a breakpoint. Modifier flips
    // every two ability points (8→9 stays at -1, 9→10 jumps to 0);
    // cost flips at the curve transitions (13→14 jumps 1→2, 15→16
    // jumps 2→3 in vanilla chargen). First observation per ability
    // seeds both slots without announcing — the user already heard
    // both numbers in the chain-step suffix when they navigated to
    // this row.
    ResetTrackerIfPanelChanged(panel);
    int prevMod  = s_tracker.lastMod[idx];
    int prevCost = s_tracker.lastCost[idx];
    bool firstSeenMod  = (prevMod  == INT_MIN);
    bool firstSeenCost = (prevCost == INT_MIN);
    bool modChanged  = !firstSeenMod  && newMod != INT_MIN && newMod != prevMod;
    bool costChanged = !firstSeenCost && cost   >= 0       && cost   != prevCost;
    if (newMod != INT_MIN) s_tracker.lastMod[idx]  = newMod;
    if (cost   >= 0)       s_tracker.lastCost[idx] = cost;

    char modText[16] = {0};
    if (modChanged) FormatModifier(newMod, modText, sizeof(modText));

    acc::strings::Id fmtId;
    if (modChanged && costChanged) {
        fmtId = acc::strings::Id::FmtChargenAttrValueChangeWithModAndCost;
    } else if (modChanged) {
        fmtId = acc::strings::Id::FmtChargenAttrValueChangeWithMod;
    } else if (costChanged) {
        fmtId = acc::strings::Id::FmtChargenAttrValueChangeWithCost;
    } else {
        fmtId = acc::strings::Id::FmtChargenAttrValueChangeBare;
    }
    const char* fmt = acc::strings::Get(fmtId);

    char msg[160];
    if (modChanged && costChanged) {
        snprintf(msg, sizeof(msg), fmt, value, modText, remText, costText);
    } else if (modChanged) {
        snprintf(msg, sizeof(msg), fmt, value, modText, remText);
    } else if (costChanged) {
        snprintf(msg, sizeof(msg), fmt, value, remText, costText);
    } else {
        snprintf(msg, sizeof(msg), fmt, value, remText);
    }

    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.ChargenAttr",
                  "value-change focus=%p idx=%d value=\"%s\" "
                  "mod=%d (prev=%d, %s) cost=%d (prev=%d, %s) remaining=\"%s\"",
                  control, idx, value,
                  newMod, prevMod,
                  modChanged ? "CHANGED" : (firstSeenMod ? "first-seen" : "same"),
                  cost, prevCost,
                  costChanged ? "CHANGED" : (firstSeenCost ? "first-seen" : "same"),
                  remText);
    return true;
}

}  // namespace acc::menus::chargen_attr
