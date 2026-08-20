// chargen Fähigkeiten panel tweaks.
// See menus_chargen_skills.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "menus_chargen_skills.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chargen_layout.h"
#include "strings.h"
#include "prism.h"

namespace acc::menus::chargen_skills {

namespace {
// Everything this panel shares with chargen_attr is driven from here —
// see menus_chargen_layout.h.
const chargen_layout::PanelDesc kDesc = {
    /*vtable*/              kVtableCSWGuiSkillsCharGen,
    /*buttonsOffset*/       kSkillsCharGenButtonsArrayOffset,
    /*labelsOffset*/        kSkillsCharGenLabelsArrayOffset,
    /*selectedOffset*/      kSkillsCharGenSelectedSkillOffset,
    /*descListBoxOffset*/   kSkillsCharGenDescriptionListBoxOffset,
    /*onEnterPointsButton*/ kAddrCSWGuiSkillsCharGenOnEnterPointsButton,
    /*count*/               kSkillsCharGenSkillCount,
    /*logTag*/              "Menus.ChargenSkill",
    /*selectedFieldName*/   "selected_skill",
    /*creatureOffset*/      kSkillsCharGenChargenCreatureOffset,
    /*creatureStatsOffset*/ kChargenCreatureLevelUpStatsOffset,
};
}  // namespace

bool IsChargenSkillsPanel(void* panel) {
    return chargen_layout::IsPanel(kDesc, panel);
}

int SkillIndexFromButton(void* panel, void* control) {
    return chargen_layout::IndexFromButton(kDesc, panel, control);
}

void SyncSelectedSkillFromChainFocus() {
    chargen_layout::SyncSelectedFromChainFocus(kDesc);
}

void CaptureLabelsIfApplicable(void* panel) {
    chargen_layout::CaptureLabels(kDesc, panel);
}

int RowPitchForCursorWarp(void* panel, void* control) {
    return chargen_layout::RowPitchForCursorWarp(kDesc, panel, control);
}

namespace {

// Engine predicate: is the skill at `skillIdx` a class skill for the
// chargen creature's class? Returns 1 (class) or 2 (cross-class) as
// the cost; -1 on SEH fault. The engine's signature widens param_1
// to ushort; we pass int here and let the calling convention handle
// the truncation.
typedef int (__thiscall* PFN_IsClassSkill)(void* this_, unsigned short skillIdx);

int ReadEngineSkillCost(void* panel, int skillIdx) {
    if (!panel) return -1;
    if (skillIdx < 0 || skillIdx >= kSkillsCharGenSkillCount) return -1;
    // Diagnostic only for now: the binding probe's verdict is logged so we
    // can correlate it with faults, but it must NOT gate the call. Gating
    // on it suppressed cost + description on a perfectly ordinary KOTOR 2
    // level-up screen (log patch-20260819-221733) — the probe's reading of
    // "not ready" does not match panels the engine itself drives happily.
    chargen_layout::LogBindingWord(kDesc, panel, "pre-cost");
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
    return chargen_layout::AnnounceDescription(kDesc, panel, control);
}

bool IsChargenSkillsDescriptionListbox(void* listBox) {
    return chargen_layout::IsDescriptionListbox(kDesc, listBox);
}

bool AnnounceValueChange(void* panel, void* control) {
    int idx = SkillIndexFromButton(panel, control);
    if (idx < 0) return false;

    char value[32];
    char remaining[32];
    bool gotValue = acc::engine::ReadButtonText(control,
                         value, sizeof(value));
    bool gotRem   = acc::engine::ReadLabelTextAt(panel,
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
