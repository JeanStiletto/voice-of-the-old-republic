// KOTOR Accessibility — virtual stat-row anchors for the Equip panel.
// See menus_equipstats.h for design summary.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_equipstats.h"

#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "strings.h"

namespace acc::menus::equipstats {

namespace {

// Spec table — one entry per virtual stat row. Anchors on the label the
// engine writes the VALUE into (not the static caption labels). Attack
// + Damage anchor on the RIGHT-hand pair because single-weapon mode
// always populates right-hand and blanks left; dual-wield populates both.
//
// sortCy values: real Equip-panel buttons live at y < ~500 (screen
// coords). Chain entries here use 10000+ so they sort AFTER everything
// else — slot buttons, Back, Change1/2, character_left/right.
//
// Order: Vitality → Defense → Attack → Damage (matches the bottom-bar
// reading order sighted players scan from left to right).
//
// Lane's struct DB names the damage label `*_attack_label` and the
// to-hit label `*_tohit_label`; verified via Ghidra decomp of
// UpdateInventory @0x006b9970 — the format string puts "%d-%d" into
// `*_attack_label` (damage range) and "+%d" / "%d" into
// `*_tohit_label` (to-hit bonus). Caption-only labels at 0x2a98 /
// 0x2bd8 are NOT used.
struct EquipStatRowSpec {
    size_t           valueOffset;     // RIGHT-hand value label offset
    acc::strings::Id formatSingle;
    acc::strings::Id formatDual;      // Id::Count_ when no dual variant
    size_t           leftValueOffset; // peer LEFT label for dual-wield
                                      // detection; 0 when single-only
    int              sortCy;
};

constexpr EquipStatRowSpec k_specs[] = {
    { kEquipPanelHpLabelOffset,
      acc::strings::Id::FmtEquipVitality, acc::strings::Id::Count_,
      0,                                              10000 },
    { kEquipPanelDefenseLabelOffset,
      acc::strings::Id::FmtEquipDefense,  acc::strings::Id::Count_,
      0,                                              10001 },
    { kEquipPanelRightWeaponTohitLabelOffset,
      acc::strings::Id::FmtEquipAttack,   acc::strings::Id::FmtEquipAttackDual,
      kEquipPanelLeftWeaponTohitLabelOffset,          10002 },
    { kEquipPanelRightWeaponDamageLabelOffset,
      acc::strings::Id::FmtEquipDamage,   acc::strings::Id::FmtEquipDamageDual,
      kEquipPanelLeftWeaponDamageLabelOffset,         10003 },
};
constexpr int k_specCount = static_cast<int>(sizeof(k_specs) / sizeof(k_specs[0]));

const EquipStatRowSpec* FindSpecForControl(void* panel, void* labelControl) {
    if (!panel || !labelControl) return nullptr;
    if (acc::engine::IdentifyPanel(panel) !=
        acc::engine::PanelKind::InGameEquip) return nullptr;
    uintptr_t panelBase = reinterpret_cast<uintptr_t>(panel);
    uintptr_t ctrl      = reinterpret_cast<uintptr_t>(labelControl);
    if (ctrl < panelBase) return nullptr;
    size_t offset = static_cast<size_t>(ctrl - panelBase);
    for (int i = 0; i < k_specCount; ++i) {
        if (k_specs[i].valueOffset == offset) return &k_specs[i];
    }
    return nullptr;
}

// Read a CSWGuiLabel's rendered text. gui_string first (the engine's
// actual render source), falls back to inline CExoString / TLK strref
// via the indirect helper.
bool ReadEquipLabel(void* panel, size_t offset, char* outBuf, size_t bufSize) {
    if (bufSize == 0) return false;
    outBuf[0] = '\0';
    auto* label = reinterpret_cast<unsigned char*>(panel) + offset;
    __try {
        if (acc::engine::ReadGuiString(label, kLabelGuiStringPtrOffset,
                                       outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return true;
        }
        acc::engine::ExtractTextOrStrRefIndirect(
            label, kLabelTextOffset, kLabelStrRefOffset,
            kLabelTextObjectOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
    }
    return outBuf[0] != '\0';
}

}  // namespace

bool IsEquipStatRowAnchor(void* panel, void* labelControl) {
    return FindSpecForControl(panel, labelControl) != nullptr;
}

void ForEachEquipStatRowAnchor(void* panel,
                               bool (*callback)(void* labelControl, int sortCy,
                                                void* userData),
                               void* userData) {
    if (!panel || !callback) return;
    if (acc::engine::IdentifyPanel(panel) !=
        acc::engine::PanelKind::InGameEquip) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    for (int i = 0; i < k_specCount; ++i) {
        void* label = base + k_specs[i].valueOffset;
        if (!callback(label, k_specs[i].sortCy, userData)) return;
    }
}

bool ExtractEquipStatRow(void* panel, void* labelControl,
                         char* outBuf, size_t bufSize) {
    if (bufSize == 0) return false;
    const EquipStatRowSpec* spec = FindSpecForControl(panel, labelControl);
    if (!spec) return false;

    using acc::strings::Get;
    using acc::strings::Id;

    // Read the primary (right-hand) value.
    char rightVal[32] = "";
    if (!ReadEquipLabel(panel, spec->valueOffset, rightVal, sizeof(rightVal))) {
        return false;
    }

    // Dual-wield branch: if the spec has a left-peer offset AND the
    // engine populated text there, the character is dual-wielding and
    // we speak both hands. Single-weapon mode blanks the left peer to
    // "", so this naturally falls through to the single format.
    if (spec->leftValueOffset != 0 && spec->formatDual != Id::Count_) {
        char leftVal[32] = "";
        if (ReadEquipLabel(panel, spec->leftValueOffset,
                           leftVal, sizeof(leftVal))) {
            snprintf(outBuf, bufSize, Get(spec->formatDual),
                     leftVal, rightVal);
            return true;
        }
    }

    snprintf(outBuf, bufSize, Get(spec->formatSingle), rightVal);
    return true;
}

}  // namespace acc::menus::equipstats
