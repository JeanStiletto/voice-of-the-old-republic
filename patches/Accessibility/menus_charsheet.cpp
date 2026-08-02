// character sheet stat rows.
//
// See menus_charsheet.h for design overview. User-facing strings route
// through acc::strings::Get (memory: feedback_centralise_user_strings.md)
// so a non-German install gets localised phrases.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_charsheet.h"

#include "engine_offsets.h"
#include "engine_reads.h"
#include "strings.h"
#include "engine_offsets_select.h"

namespace acc::menus::charsheet {

namespace {

// CSWGuiInGameCharacter member offsets — value labels (the numbers).
// All are inline CSWGuiLabel @ +0x140 each, gui_string-readable.
//
// NOTE: Lane's struct names are partially misleading — verified live in
// patch-20260505-151714.log:984 by reading every candidate offset and
// inspecting the spoken output:
//   - lbl_class @ 0x1A4 holds the HEADING "Klasse", not "Soldat"
//   - lbl_class1 @ 0x2E4 holds the actual class name ("Soldat")
//   - lbl_level @ 0x564 holds heading "Level", actual value at lbl_level1
// XP slots: Lane's naming holds — lbl_exp_stat @0x11e4 is the current
// XP value, lbl_needed_xp @0x1464 is the threshold for the next level.
// A prior commit swapped these based on a session where the dev
// believed the player had 6000 current XP and 3001 was the threshold;
// reversed the assignment so lbl_exp_stat became "threshold" and
// lbl_needed_xp became "current". Verified wrong in session 20260514-
// 201250: Gauner level 1 with 1275 XP (above the 1000 threshold so
// CAN level) read 0x11e4 = "1275" and 0x1464 = "1000", matching
// Lane's naming. The earlier "6000 current XP" assumption was the
// confused step — the dev's character was actually at 3001 current
// XP with 6000 being the level-4 threshold.
// Comments below state what each offset ACTUALLY contains.
//
// HP / FP: Lane's lbl_force_stat (0x16e4) holds FP, lbl_vitality_stat
// (0x1824) holds HP — Lane's naming is correct. An earlier commit
// reversed these two based on a session where the user had godmoded
// FP=999/999 and normal HP=36/36 — the dev concluded "Lane's names
// are reversed" but the actually-godmoded slot was FP, not HP.
// Verified via session 20260514-201250: Soldat level 3 with infinite-
// force cheat shows 0x16e4 = "999/999" (godmoded FP) and 0x1824 =
// "30/36" (normal HP for that class+level). Names match Lane.
// KOTOR 2 column mined from the K2 panel ctor 0x0084C3A0 (tag wiring, label
// stride 0x148) and cross-checked in the K2 SetStats twin 0x0084E6F0, which
// writes the save values into the *_STAT labels (0x588/0x6d0/0x818), FP into
// 0xfc8 behind the IsJedi branch, HP into 0x1110, and attribute value/mod
// into 0x1630/0x18c0 — the same role split assumed here. KOTOR 2's character
// panel builds NO class or level labels at all (no CLASS/LEVEL tag in its
// ctor), so those two rows are Kotor1Only and the spec walk skips them.
const size_t kCharSheetLblClass    = acc::off::Kotor1Only(0x02e4);  // class name "Soldat" (lbl_class1)
const size_t kCharSheetLblLevel    = acc::off::Kotor1Only(0x06a4);  // level number "1" (lbl_level1)
const size_t kCharSheetLblFort     = acc::off::Pick(0x0924, 0x588);   // fortitude save val
const size_t kCharSheetLblRef      = acc::off::Pick(0x0a64, 0x6d0);   // reflex save val
const size_t kCharSheetLblWill     = acc::off::Pick(0x0ba4, 0x818);   // will save val
const size_t kCharSheetLblXpCur    = acc::off::Pick(0x11e4, 0xaa8);   // current XP — Lane: lbl_exp_stat
const size_t kCharSheetLblXpThresh = acc::off::Pick(0x1464, 0xd38);   // next-level threshold — Lane: lbl_needed_xp
const size_t kCharSheetLblDefStat  = acc::off::Pick(0x15a4, 0xe80);   // defense val
const size_t kCharSheetLblFp       = acc::off::Pick(0x16e4, 0xfc8);   // FP — Lane: lbl_force_stat
const size_t kCharSheetLblHp       = acc::off::Pick(0x1824, 0x1110);  // HP — Lane: lbl_vitality_stat
const size_t kCharSheetLblStr      = acc::off::Pick(0x1d24, 0x1630);  // "14"
const size_t kCharSheetLblStrMod   = acc::off::Pick(0x1fa4, 0x18c0);  // "+2"
const size_t kCharSheetLblWis      = acc::off::Pick(0x20e4, 0x1a08);
const size_t kCharSheetLblWisMod   = acc::off::Pick(0x2364, 0x1c98);
const size_t kCharSheetLblCha      = acc::off::Pick(0x24a4, 0x1de0);
const size_t kCharSheetLblChaMod   = acc::off::Pick(0x2724, 0x2070);
const size_t kCharSheetLblInt      = acc::off::Pick(0x2864, 0x21b8);
const size_t kCharSheetLblIntMod   = acc::off::Pick(0x2ae4, 0x2448);
const size_t kCharSheetLblCon      = acc::off::Pick(0x2c24, 0x2590);
const size_t kCharSheetLblConMod   = acc::off::Pick(0x2ea4, 0x2820);
const size_t kCharSheetLblDex      = acc::off::Pick(0x2fe4, 0x2968);
const size_t kCharSheetLblDexMod   = acc::off::Pick(0x3264, 0x2bf8);

// Alignment slider — CSWGuiSlider @+0x55c4. cur/max at the standard slider
// offsets per engine_offsets.h.
const size_t kCharSheetSldAlign    = acc::off::Pick(0x55c4, 0x3a60);

// Whether the engine showed the Force line is our "is a Force user" signal.
// CSWGuiInGameCharacter::SetStats @0x006afda0 calls CSWClass::IsJedi on the
// displayed character's class and, for non-Jedi, CLEARS bit 0x02 of
// lbl_force_stat.control.bit_flags (hides it); for Jedi it sets the text and
// SETS bit 0x02. So lbl_force_stat's "shown" bit is the engine's own Force-
// user decision — far more robust than re-deriving it (the panel caches NO
// creature pointer; the +0x59e4 region holds party_count / selected index,
// and SetStats re-fetches the creature fresh each call). lbl_force_stat lives
// at panel + kCharSheetLblFp; bit_flags at +kControlBitFlagsOffset (0x44).
// Same bit on KOTOR 2: its SetStats twin 0x0084E6F0 runs the identical
// `& ~2` / `| 2` dance on the force label's bit_flags (at +0x48 there,
// matching kControlBitFlagsOffset's K2 column).
const uint32_t kControlShownBit = acc::off::Same(0x2);

// True iff the character currently shown on this sheet is a Force user.
// Reads the engine's own decision: lbl_force_stat's "shown" bit (0x02 of
// control.bit_flags at +0x44), which SetStats clears for non-Jedi and sets
// for Jedi. Fails safe to false (hide FP) on any null/fault.
bool DisplayedHasForce(void* panel) {
    if (!panel) return false;
    __try {
        uint32_t flags = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(panel) + kCharSheetLblFp +
            kControlBitFlagsOffset);
        return (flags & kControlShownBit) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Stat-row anchor table. The chain inserts a virtual entry for each
// row; the extractor dispatches on `kind` to decide which engine fields
// to read and how to format them.
//
// `sortCy` is the synthetic y-coordinate used to position the virtual
// entry in the navigable chain. Real button entries on Charakterblatt
// sit at cy >= 237; we anchor the stat block ABOVE those at cy 1..12
// so Up/Down navigation reads:
//
//   [stats: Klasse, Stufe, Erfahrung, HP, FP, Str, Dex, Con, Int, Wis,
//    Cha, Gesinnung] then [real buttons: Autom., Levelaufst, Schliess,
//    Kurzbefehle, Vorheriger, Nächster].
//
// Synthetic cy lets us enforce reading order independent of the engine's
// label coordinates (which would otherwise interleave stats with buttons:
// Stufe at panel y=112 lands ABOVE Gauner at y=120, Erfahrung at y=392
// lands AFTER attributes etc.). Mouse warp goes via cx which we still
// read from the real label/slider position, so cursor lands on it.
enum class StatRowKind {
    LabelValue,         // single label: %s
    LabelValueMod,      // value + modifier (attributes): %s, %s
    LabelValueThresh,   // value + threshold (XP): %s von %s
    Slider,             // CSWGuiSlider cur_value / max_value (alignment)
};

struct StatRowSpec {
    size_t           valueOffset;  // label control offset, OR slider offset for Slider kind
    size_t           modOffset;    // 2nd label offset (mod / threshold); 0 if unused
    acc::strings::Id formatId;
    int              sortCy;
    StatRowKind      kind;
};

const StatRowSpec k_statRowSpecs[] = {
    // Identity block — class, level, experience.
    { kCharSheetLblClass,  0,                     acc::strings::Id::FmtCharSheetClass,      1, StatRowKind::LabelValue },
    { kCharSheetLblLevel,  0,                     acc::strings::Id::FmtCharSheetLevel,      2, StatRowKind::LabelValue },
    // XP — value + threshold rendered as 2× %s. Both labels live at
    // different offsets; we anchor on XpCur and read XpThresh inline.
    { kCharSheetLblXpCur,  kCharSheetLblXpThresh, acc::strings::Id::FmtCharSheetXp,         3, StatRowKind::LabelValueThresh },
    // Resource pools (HP + FP) — single value labels.
    { kCharSheetLblHp,     0,                     acc::strings::Id::FmtCharSheetHp,         4, StatRowKind::LabelValue },
    { kCharSheetLblFp,     0,                     acc::strings::Id::FmtCharSheetFp,         5, StatRowKind::LabelValue },
    // Six attributes — value + modifier each.
    { kCharSheetLblStr,    kCharSheetLblStrMod,   acc::strings::Id::FmtCharSheetStr,        6, StatRowKind::LabelValueMod },
    { kCharSheetLblDex,    kCharSheetLblDexMod,   acc::strings::Id::FmtCharSheetDex,        7, StatRowKind::LabelValueMod },
    { kCharSheetLblCon,    kCharSheetLblConMod,   acc::strings::Id::FmtCharSheetCon,        8, StatRowKind::LabelValueMod },
    { kCharSheetLblInt,    kCharSheetLblIntMod,   acc::strings::Id::FmtCharSheetInt,        9, StatRowKind::LabelValueMod },
    { kCharSheetLblWis,    kCharSheetLblWisMod,   acc::strings::Id::FmtCharSheetWis,       10, StatRowKind::LabelValueMod },
    { kCharSheetLblCha,    kCharSheetLblChaMod,   acc::strings::Id::FmtCharSheetCha,       11, StatRowKind::LabelValueMod },
    // Alignment slider — exposed as a virtual chain entry because
    // sld_align isn't IsChainNavigable (our IsSlider vtable-equality
    // check rejects whatever subclass the panel embeds), so the user
    // can't reach it through the normal chain. The slider control
    // itself is the anchor; ExtractStatRow reads cur_value / max_value
    // off it via the standard CSWGuiSlider offsets.
    { kCharSheetSldAlign,  0,                     acc::strings::Id::FmtCharSheetAlignment, 12, StatRowKind::Slider },
};
constexpr int k_statRowCount = static_cast<int>(
    sizeof(k_statRowSpecs) / sizeof(k_statRowSpecs[0]));

// Resolve `labelControl` to a StatRowSpec for `panel`. Returns nullptr
// if the address isn't one of the registered anchors.
const StatRowSpec* FindSpecForControl(void* panel, void* labelControl) {
    if (!panel || !labelControl) return nullptr;
    uintptr_t panelBase = reinterpret_cast<uintptr_t>(panel);
    uintptr_t ctrl      = reinterpret_cast<uintptr_t>(labelControl);
    if (ctrl < panelBase) return nullptr;
    size_t offset = static_cast<size_t>(ctrl - panelBase);
    for (int i = 0; i < k_statRowCount; ++i) {
        // Rows with no counterpart on the running game (class/level on
        // KOTOR 2) carry the poison offset and can never match a real
        // control address, but skip them explicitly for clarity.
        if (!acc::off::Ok(k_statRowSpecs[i].valueOffset)) continue;
        if (k_statRowSpecs[i].valueOffset == offset) {
            // FP row only exists for Force users — for everyone else the
            // row is dropped from the chain (no nav landing, no extract).
            if (offset == kCharSheetLblFp && !DisplayedHasForce(panel)) {
                return nullptr;
            }
            return &k_statRowSpecs[i];
        }
    }
    return nullptr;
}

}  // namespace

bool IsStatRowAnchor(void* panel, void* labelControl) {
    return FindSpecForControl(panel, labelControl) != nullptr;
}

void ForEachStatRowAnchor(void* panel,
                          bool (*callback)(void* labelControl, int sortCy,
                                           void* userData),
                          void* userData) {
    if (!panel || !callback) return;
    auto* base = reinterpret_cast<unsigned char*>(panel);
    bool hasForce = DisplayedHasForce(panel);
    for (int i = 0; i < k_statRowCount; ++i) {
        // Drop the FP row entirely for non-Force characters (Option A) —
        // RebindChain never inserts a virtual entry for it, so Up/Down skips
        // straight from HP to the attributes.
        if (k_statRowSpecs[i].valueOffset == kCharSheetLblFp && !hasForce) {
            continue;
        }
        // Never form base+poison for a row the running game lacks — a wild
        // non-null anchor would be registered in the chain (the pointer-
        // formation trap documented in engine_offsets_select.h).
        if (!acc::off::Ok(k_statRowSpecs[i].valueOffset)) continue;
        void* label = base + k_statRowSpecs[i].valueOffset;
        if (!callback(label, k_statRowSpecs[i].sortCy, userData)) return;
    }
}

bool ExtractStatRow(void* panel, void* labelControl,
                    char* outBuf, size_t bufSize) {
    if (bufSize == 0) return false;
    const StatRowSpec* spec = FindSpecForControl(panel, labelControl);
    if (!spec) return false;

    using acc::strings::Get;

    // Slider rows: read cur/max directly off the CSWGuiSlider struct
    // at panel + valueOffset. cur_value @+0x74, max_value @+0x70 per
    // swkotor.exe.h. Two %u in the format (FmtCharSheetAlignment).
    if (spec->kind == StatRowKind::Slider) {
        uint32_t curVal = 0, maxVal = 0;
        __try {
            auto* sld = reinterpret_cast<unsigned char*>(panel) +
                        spec->valueOffset;
            curVal = acc::engine::ReadU32(sld, kSliderCurValueOffset);
            maxVal = acc::engine::ReadU32(sld, kSliderMaxValueOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (maxVal == 0) return false;
        snprintf(outBuf, bufSize, Get(spec->formatId), curVal, maxVal);
        return true;
    }

    // Label rows: read value (and optionally mod / threshold) text.
    char value[64];
    if (!acc::engine::ReadLabelTextAt(panel, spec->valueOffset,
                                      value, sizeof(value))) {
        return false;
    }

    char mod[16];
    mod[0] = '\0';
    if (spec->modOffset != 0) {
        acc::engine::ReadLabelTextAt(panel, spec->modOffset, mod, sizeof(mod));
    }

    switch (spec->kind) {
    case StatRowKind::LabelValueThresh:
        // FmtCharSheetXp: 2× %s (current, threshold).
        if (mod[0] == '\0') return false;
        snprintf(outBuf, bufSize, Get(spec->formatId), value, mod);
        return true;
    case StatRowKind::LabelValueMod:
        // FmtCharSheet{Str,Dex,…}: 3× %s (value, separator, modifier).
        // Same shape MaybeAnnounce uses for the snapshot — the
        // separator is ", " when the modifier is non-empty, "" otherwise.
        snprintf(outBuf, bufSize, Get(spec->formatId),
                 value, mod[0] ? ", " : "", mod);
        return true;
    case StatRowKind::LabelValue:
    default:
        // Single-value formats (Class, Level, HP, FP).
        snprintf(outBuf, bufSize, Get(spec->formatId), value);
        return true;
    }
}

}  // namespace acc::menus::charsheet
