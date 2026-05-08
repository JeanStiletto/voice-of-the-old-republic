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
//   - lbl_exp_stat / lbl_needed_xp returned 3001 / 6000 respectively, but
//     the player has 6000 current XP and 3001 is the next-level threshold
//     → so lbl_exp_stat IS the threshold, lbl_needed_xp IS current XP
//   - lbl_vitality_stat returned 36/36 (force) and lbl_force_stat returned
//     999/999 (HP) — Lane's names are reversed; we use the values, not the
//     names
// Comments below state what each offset ACTUALLY contains, not what Lane
// labelled it.
constexpr size_t kCharSheetLblClass    = 0x02e4;  // class name "Soldat" (lbl_class1)
constexpr size_t kCharSheetLblLevel    = 0x06a4;  // level number "1" (lbl_level1)
constexpr size_t kCharSheetLblFort     = 0x0924;  // fortitude save val
constexpr size_t kCharSheetLblRef      = 0x0a64;  // reflex save val
constexpr size_t kCharSheetLblWill     = 0x0ba4;  // will save val
constexpr size_t kCharSheetLblXpThresh = 0x11e4;  // next-level threshold ("3001")
constexpr size_t kCharSheetLblXpCur    = 0x1464;  // current XP ("6000")
constexpr size_t kCharSheetLblDefStat  = 0x15a4;  // defense val
constexpr size_t kCharSheetLblHp       = 0x16e4;  // HP "999/999" (Lane: lbl_force_stat)
constexpr size_t kCharSheetLblFp       = 0x1824;  // FP "36/36" (Lane: lbl_vitality_stat)
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

}  // namespace

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
