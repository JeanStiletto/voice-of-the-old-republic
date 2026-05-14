// KOTOR Accessibility — character sheet sub-screen opener announce.
//
// See menus_charsheet.h for design overview. Lifted from menus.cpp in
// Step 2A of the refactor; the only behaviour change vs the original
// is that user-facing strings now route through acc::strings::Get
// (memory: feedback_centralise_user_strings.md) so a non-German
// install gets the localised opener instead of hardcoded German.

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "menus_charsheet.h"

#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

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
constexpr size_t kCharSheetLblClass    = 0x02e4;  // class name "Soldat" (lbl_class1)
constexpr size_t kCharSheetLblLevel    = 0x06a4;  // level number "1" (lbl_level1)
constexpr size_t kCharSheetLblFort     = 0x0924;  // fortitude save val
constexpr size_t kCharSheetLblRef      = 0x0a64;  // reflex save val
constexpr size_t kCharSheetLblWill     = 0x0ba4;  // will save val
constexpr size_t kCharSheetLblXpCur    = 0x11e4;  // current XP — Lane: lbl_exp_stat
constexpr size_t kCharSheetLblXpThresh = 0x1464;  // next-level threshold — Lane: lbl_needed_xp
constexpr size_t kCharSheetLblDefStat  = 0x15a4;  // defense val
constexpr size_t kCharSheetLblFp       = 0x16e4;  // FP — Lane: lbl_force_stat
constexpr size_t kCharSheetLblHp       = 0x1824;  // HP — Lane: lbl_vitality_stat
constexpr size_t kCharSheetLblStr      = 0x1d24;  // "14"
constexpr size_t kCharSheetLblStrMod   = 0x1fa4;  // "+2"
constexpr size_t kCharSheetLblWis      = 0x20e4;
constexpr size_t kCharSheetLblWisMod   = 0x2364;
constexpr size_t kCharSheetLblCha      = 0x24a4;
constexpr size_t kCharSheetLblChaMod   = 0x2724;
constexpr size_t kCharSheetLblInt      = 0x2864;
constexpr size_t kCharSheetLblIntMod   = 0x2ae4;
constexpr size_t kCharSheetLblCon      = 0x2c24;
constexpr size_t kCharSheetLblConMod   = 0x2ea4;
constexpr size_t kCharSheetLblDex      = 0x2fe4;
constexpr size_t kCharSheetLblDexMod   = 0x3264;

// Alignment slider — CSWGuiSlider @+0x55c4. cur/max at the standard slider
// offsets per engine_offsets.h.
constexpr size_t kCharSheetSldAlign    = 0x55c4;

// Read a CSWGuiLabel's rendered text at panel+offset into outBuf. Tries
// gui_string (the engine's actual render source) first, falling back to
// the inline CExoString and TLK strref paths via the indirect helper.
// Empty-string result on failure — caller treats "" as "skip this field".
void ReadCharSheetLabel(void* panel, size_t offset,
                        char* outBuf, size_t bufSize) {
    if (bufSize == 0) return;
    outBuf[0] = '\0';
    auto* label = reinterpret_cast<unsigned char*>(panel) + offset;
    __try {
        if (acc::engine::ReadGuiString(label, kLabelGuiStringPtrOffset,
                                       outBuf, bufSize) &&
            outBuf[0] != '\0') {
            return;
        }
        acc::engine::ExtractTextOrStrRefIndirect(
            label, kLabelTextOffset, kLabelStrRefOffset,
            kLabelTextObjectOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
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

constexpr StatRowSpec k_statRowSpecs[] = {
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
        if (k_statRowSpecs[i].valueOffset == offset) {
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
    for (int i = 0; i < k_statRowCount; ++i) {
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
    ReadCharSheetLabel(panel, spec->valueOffset, value, sizeof(value));
    if (value[0] == '\0') return false;

    char mod[16];
    mod[0] = '\0';
    if (spec->modOffset != 0) {
        ReadCharSheetLabel(panel, spec->modOffset, mod, sizeof(mod));
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

void MaybeAnnounce(void* panel) {
    if (!panel) return;
    if (acc::engine::IdentifyPanel(panel) !=
        acc::engine::PanelKind::InGameCharacter) {
        return;
    }
    // No per-panel "already spoken" guard — `IsSubScreenTracked` in the
    // caller already gates first-sight per panel-open cycle, so a close +
    // reopen re-fires both the kind name AND this opener cleanly.

    // Read every value field. Buffers are sized for the typical content
    // ("14", "+2", "999/999", "Soldat") — ample headroom for localised
    // class names which can be 32+ chars in some translations.
    char cls[64], lvl[16], hp[32], fp[32], xpCur[32], xpThresh[32];
    char str[8],  strMod[8], dex[8], dexMod[8], con[8], conMod[8];
    char intel[8], intMod[8], wis[8], wisMod[8], cha[8], chaMod[8];

    ReadCharSheetLabel(panel, kCharSheetLblClass,    cls,      sizeof(cls));
    ReadCharSheetLabel(panel, kCharSheetLblLevel,    lvl,      sizeof(lvl));
    ReadCharSheetLabel(panel, kCharSheetLblHp,       hp,       sizeof(hp));
    ReadCharSheetLabel(panel, kCharSheetLblFp,       fp,       sizeof(fp));
    ReadCharSheetLabel(panel, kCharSheetLblXpCur,    xpCur,    sizeof(xpCur));
    ReadCharSheetLabel(panel, kCharSheetLblXpThresh, xpThresh, sizeof(xpThresh));
    ReadCharSheetLabel(panel, kCharSheetLblStr,      str,    sizeof(str));
    ReadCharSheetLabel(panel, kCharSheetLblStrMod,   strMod, sizeof(strMod));
    ReadCharSheetLabel(panel, kCharSheetLblDex,      dex,    sizeof(dex));
    ReadCharSheetLabel(panel, kCharSheetLblDexMod,   dexMod, sizeof(dexMod));
    ReadCharSheetLabel(panel, kCharSheetLblCon,      con,    sizeof(con));
    ReadCharSheetLabel(panel, kCharSheetLblConMod,   conMod, sizeof(conMod));
    ReadCharSheetLabel(panel, kCharSheetLblInt,      intel,  sizeof(intel));
    ReadCharSheetLabel(panel, kCharSheetLblIntMod,   intMod, sizeof(intMod));
    ReadCharSheetLabel(panel, kCharSheetLblWis,      wis,    sizeof(wis));
    ReadCharSheetLabel(panel, kCharSheetLblWisMod,   wisMod, sizeof(wisMod));
    ReadCharSheetLabel(panel, kCharSheetLblCha,      cha,    sizeof(cha));
    ReadCharSheetLabel(panel, kCharSheetLblChaMod,   chaMod, sizeof(chaMod));

    // Alignment slider value. cur_value range is [0..100] in vanilla;
    // 50 = neutral, 0 = Dark Side, 100 = Light Side.
    uint32_t alignCur = 0, alignMax = 0;
    __try {
        auto* sld = reinterpret_cast<unsigned char*>(panel) +
                    kCharSheetSldAlign;
        alignCur = acc::engine::ReadU32(sld, kSliderCurValueOffset);
        alignMax = acc::engine::ReadU32(sld, kSliderMaxValueOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        alignCur = 0;
        alignMax = 0;
    }

    // Build the announce string. Pre-formatted attribute modifiers
    // ("+2") come straight from the engine so we don't reimplement
    // the +N/-N formatting. Skips fields that read as empty rather
    // than emitting bare "Stärke ." sentences.
    using acc::strings::Get;
    using acc::strings::Id;

    char msg[1024];
    size_t off = 0;
    auto append = [&](const char* fmt, auto... args) {
        if (off >= sizeof(msg)) return;
        int n = snprintf(msg + off, sizeof(msg) - off, fmt, args...);
        if (n > 0) off += (size_t)n;
        if (off > sizeof(msg)) off = sizeof(msg);
    };
    if (cls[0])    append(Get(Id::FmtCharSheetClass), cls);
    if (lvl[0])    append(Get(Id::FmtCharSheetLevel), lvl);
    if (xpCur[0] && xpThresh[0]) {
        append(Get(Id::FmtCharSheetXp), xpCur, xpThresh);
    }
    if (hp[0])     append(Get(Id::FmtCharSheetHp), hp);
    if (fp[0])     append(Get(Id::FmtCharSheetFp), fp);
    if (str[0])    append(Get(Id::FmtCharSheetStr),
                          str, strMod[0] ? ", " : "", strMod);
    if (dex[0])    append(Get(Id::FmtCharSheetDex),
                          dex, dexMod[0] ? ", " : "", dexMod);
    if (con[0])    append(Get(Id::FmtCharSheetCon),
                          con, conMod[0] ? ", " : "", conMod);
    if (intel[0])  append(Get(Id::FmtCharSheetInt),
                          intel, intMod[0] ? ", " : "", intMod);
    if (wis[0])    append(Get(Id::FmtCharSheetWis),
                          wis, wisMod[0] ? ", " : "", wisMod);
    if (cha[0])    append(Get(Id::FmtCharSheetCha),
                          cha, chaMod[0] ? ", " : "", chaMod);
    if (alignMax > 0) {
        append(Get(Id::FmtCharSheetAlignment), alignCur, alignMax);
    }

    if (off == 0) {
        acclog::Write("Menus.CharSheet", "panel=%p — all fields empty, skip",
                      panel);
        return;
    }

    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.CharSheet", "panel=%p text=\"%.500s\"", panel, msg);
}

}  // namespace acc::menus::charsheet
