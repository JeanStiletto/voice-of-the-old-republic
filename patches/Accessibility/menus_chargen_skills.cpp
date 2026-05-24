// KOTOR Accessibility — chargen Fähigkeiten panel tweaks.
// See menus_chargen_skills.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "menus_chargen_skills.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chain.h"
#include "menus_extract.h"
#include "strings.h"
#include "prism.h"

namespace acc::menus::chargen_skills {

bool IsChargenSkillsPanel(void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiSkillsCharGen;
}

int SkillIndexFromButton(void* panel, void* control) {
    if (!IsChargenSkillsPanel(panel) || !control) return -1;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    auto* btn  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = btn - base;
    if (off < (ptrdiff_t)kSkillsCharGenButtonsArrayOffset) return -1;
    ptrdiff_t rel = off - (ptrdiff_t)kSkillsCharGenButtonsArrayOffset;
    if (rel % (ptrdiff_t)kCSWGuiButtonSize != 0) return -1;
    int i = (int)(rel / (ptrdiff_t)kCSWGuiButtonSize);
    if (i < 0 || i >= kSkillsCharGenSkillCount) return -1;
    return i;
}

void SyncSelectedSkillFromChainFocus() {
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsChargenSkillsPanel(panel)) return;
    if (acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    void* focused =
        acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    int idx = SkillIndexFromButton(panel, focused);
    if (idx < 0) return;

    auto* base = reinterpret_cast<unsigned char*>(panel);
    int* slot = reinterpret_cast<int*>(
        base + kSkillsCharGenSelectedSkillOffset);
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
        acclog::Write("Menus.ChargenSkill",
                      "selected_skill %d -> %d (focused=%p)",
                      prev, idx, focused);
    }
}

void CaptureLabelsIfApplicable(void* panel) {
    if (!IsChargenSkillsPanel(panel)) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    for (int i = 0; i < kSkillsCharGenSkillCount; ++i) {
        void* labelCtl = base + kSkillsCharGenLabelsArrayOffset +
                         (size_t)i * kCSWGuiLabelSize;
        void* buttonCtl = base + kSkillsCharGenButtonsArrayOffset +
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
            acclog::Write("Menus.ChargenSkill",
                          "capture[%d] button=%p label=\"%s\"",
                          i, buttonCtl, text);
        }
    }
}

int RowPitchForCursorWarp(void* panel, void* control) {
    if (SkillIndexFromButton(panel, control) < 0) return 0;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    int top0 = 0, top1 = 0;
    __try {
        auto* ext0 = reinterpret_cast<int*>(
            base + kSkillsCharGenButtonsArrayOffset + kControlExtentOffset);
        auto* ext1 = reinterpret_cast<int*>(
            base + kSkillsCharGenButtonsArrayOffset + kCSWGuiButtonSize +
            kControlExtentOffset);
        top0 = ext0[1];
        top1 = ext1[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    int pitch = top1 - top0;
    if (pitch <= 0 || pitch > 100) return 0;
    return pitch;
}

namespace {

// Read a CSWGuiButton's rendered text without the cycle-category
// prefix. Used by AnnounceValueChange — we need the bare "1" the
// engine puts in skill_buttons[i].text, not the "Computer, 1"
// composed string FromControl returns.
bool ReadButtonTextDirect(void* button, char* outBuf, size_t bufSize) {
    if (!button || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    __try {
        if (acc::engine::ReadGuiString(button, kButtonGuiStringPtrOffset,
                                       outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
        if (acc::engine::ExtractTextOrStrRefIndirect(
                button,
                kButtonTextOffset,
                kButtonStrRefOffset,
                kButtonTextObjectOffset,
                outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

bool ReadLabelTextAt(void* panel, size_t offset,
                     char* outBuf, size_t bufSize) {
    if (!panel || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    auto* label = reinterpret_cast<unsigned char*>(panel) + offset;
    __try {
        if (acc::engine::ReadGuiString(label, kLabelGuiStringPtrOffset,
                                       outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
        if (acc::engine::ExtractTextOrStrRefIndirect(
                label,
                kLabelTextOffset,
                kLabelStrRefOffset,
                kLabelTextObjectOffset,
                outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return false;
}

// Engine predicate: is the skill at `skillIdx` a class skill for the
// chargen creature's class? Returns 1 (class) or 2 (cross-class) as
// the cost; -1 on SEH fault. The engine's signature widens param_1
// to ushort; we pass int here and let the calling convention handle
// the truncation.
typedef int (__thiscall* PFN_IsClassSkill)(void* this_, unsigned short skillIdx);

int ReadEngineSkillCost(void* panel, int skillIdx) {
    if (!panel) return -1;
    if (skillIdx < 0 || skillIdx >= kSkillsCharGenSkillCount) return -1;
    int isClass = 0;
    __try {
        auto fn = reinterpret_cast<PFN_IsClassSkill>(
            kAddrCSWGuiSkillsCharGenIsClassSkill);
        isClass = fn(panel, (unsigned short)skillIdx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return isClass ? 1 : 2;
}

}  // namespace

void AnnounceChainStepSuffix(void* panel, void* control) {
    int idx = SkillIndexFromButton(panel, control);
    if (idx < 0) return;

    int cost = ReadEngineSkillCost(panel, idx);

    char costText[8];
    if (cost >= 0) {
        snprintf(costText, sizeof(costText), "%d", cost);
    } else {
        snprintf(costText, sizeof(costText), "?");
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(
                 acc::strings::Id::FmtChargenSkillInfoSuffix),
             costText);

    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.ChargenSkill",
                  "chain-step suffix focus=%p idx=%d cost=%d",
                  control, idx, cost);
}

bool AnnounceChainStepDescription(void* panel, void* control) {
    int idx = SkillIndexFromButton(panel, control);
    if (idx < 0) return false;

    // Synchronously call the engine's OnEnterPointsButton with the
    // FOCUSED button. The engine populates description_list_box from
    // the passed-in button's skill — bypassing the cursor-warp's
    // hover hit-test which resolves to the row above (label overlap
    // makes Y compensation insufficient on this panel). The call's
    // SetActive on description_list_box fires our listbox hook, which
    // already silences chargen-skills (added below), so this doesn't
    // produce duplicate speech.
    typedef void (__thiscall* PFN_OnEnter)(void* this_, void* btn);
    __try {
        auto fn = reinterpret_cast<PFN_OnEnter>(
            kAddrCSWGuiSkillsCharGenOnEnterPointsButton);
        fn(panel, control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    // Read description_list_box.controls[0] — the row whose text the
    // engine just rewrote.
    auto* base = reinterpret_cast<unsigned char*>(panel);
    void* listBox = base + kSkillsCharGenDescriptionListBoxOffset;
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
    void* row = nullptr;
    __try {
        if (lbList && lbList->data && lbList->size > 0) {
            row = lbList->data[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!row) return false;

    char buf[1024];
    buf[0] = '\0';
    __try {
        if (acc::engine::ReadGuiString(row, kLabelGuiStringPtrOffset,
                                       buf, sizeof(buf)) &&
            buf[0] != '\0') {
            // ok
        } else if (acc::engine::ExtractTextOrStrRefIndirect(
                       row,
                       kLabelTextOffset,
                       kLabelStrRefOffset,
                       kLabelTextObjectOffset,
                       buf, sizeof(buf))) {
            // ok
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (buf[0] == '\0') return false;

    // Replace embedded newlines with " | " so we can see the full text
    // on a single log line (the engine's "Attribut: <attr>.\n<body>"
    // format hides the body otherwise).
    char dump[1024];
    {
        size_t n = strnlen(buf, sizeof(buf) - 1);
        if (n >= sizeof(dump)) n = sizeof(dump) - 1;
        for (size_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n' || c == '\r') c = ' ';
            dump[i] = c;
        }
        dump[n] = '\0';
    }

    prism::Speak(buf, /*interrupt=*/false);
    acclog::Write("Menus.ChargenSkill",
                  "chain-step description focus=%p idx=%d (first 300 chars: \"%.300s\")",
                  control, idx, dump);
    return true;
}

bool IsChargenSkillsDescriptionListbox(void* listBox) {
    if (!listBox) return false;
    void* panel = acc::menus::chain::g_chainPanel;
    if (!IsChargenSkillsPanel(panel)) return false;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    return listBox == reinterpret_cast<void*>(
        base + kSkillsCharGenDescriptionListBoxOffset);
}

bool AnnounceValueChange(void* panel, void* control) {
    int idx = SkillIndexFromButton(panel, control);
    if (idx < 0) return false;

    char value[32];
    char remaining[32];
    bool gotValue = ReadButtonTextDirect(control,
                         value, sizeof(value));
    bool gotRem   = ReadLabelTextAt(panel,
                         kSkillsCharGenRemainingValueOffset,
                         remaining, sizeof(remaining));
    if (!gotValue) return false;

    const char* remText = gotRem ? remaining : "?";

    char msg[128];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(
                 acc::strings::Id::FmtChargenSkillValueChange),
             value, remText);

    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.ChargenSkill",
                  "value-change focus=%p idx=%d value=\"%s\" remaining=\"%s\"",
                  control, idx, value, remText);
    return true;
}

}  // namespace acc::menus::chargen_skills
