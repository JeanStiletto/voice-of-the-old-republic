#include "tutorial_hints.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "binding_hints.h"  // Text — keyboard vs controller wording
#include "engine_game.h"     // IsKotor2 — per-game strref/row tables
#include "engine_reads.h"    // LookupTlk
#include "log.h"
#include "strings.h"
#include "engine_offsets_select.h"

namespace acc::tutorial_hints {

namespace {

using acc::strings::Id;

// CSWGuiTutorialBox +0x994 (uint8) — the source tutorial.2da row index the
// popup was built from. RE-confirmed: SetTutorialReason writes it, SetNextMessage
// reads it back as the 2DA row. Survives after the popup is built, so a poll
// recovers the popup's identity language-independently.
// KOTOR 2: +0x9d8, witnessed in both directions — SetTutorialReason
// @0x00898f60 writes it (and zeroes the page counter at +0x9d9), and
// SetNextMessage @0x00899630 reads it back as the row for its
// "Message_PC%i" 2DA lookups.
const size_t kTutorialBoxRowOffset = acc::off::Pick(0x994, 0x9d8);

// Surface 2 map: source strref -> hint Id. Resolved to the current language's
// text lazily via the engine TLK, then matched against the rendered dialogue
// line. KOTOR 1: the Endar Spire Trask / pop-window mouse lines. KOTOR 2: the
// Ebon Hawk prologue's T3-M4 instruction lines (module 001EBO) — the K2
// equivalent of the Trask set.
struct DialogHint {
    uint32_t strref;
    Id       id;
};
constexpr DialogHint kDialogHintsK1[] = {
    // Closing line of Trask's intro tree ("grab your things, we have to go").
    // Not a rewritten tutorial line — this is stock story VO — but it's the
    // point where the intro conversation ends and the player is first handed
    // control, so we hang the core-controls popup off it (fires on the
    // dialogue-close flush, since the tree ends here with no reply break).
    {10326, Id::TutTraskGettingStarted},
    {48330, Id::TutTraskEquipOpen},
    {48331, Id::TutTraskEquipBrowse},
    {48332, Id::TutTraskEquipWeapon},
    {48344, Id::TutTraskCamera},
    {48345, Id::TutTraskFootlocker},
    {48348, Id::TutTraskPickItem},
    {48350, Id::TutTraskLeader},
    {48351, Id::TutTraskMakeLeader},
    {48353, Id::TutTraskMenus},
    {48354, Id::TutTraskTabs},
    {48360, Id::TutTraskActionMenu},
    {48363, Id::TutTraskMedkit},
    {48441, Id::TutTraskOpenDoor},
    {48544, Id::TutTraskActivateEntry},
    {48547, Id::TutTraskTargetMenu},
    {48550, Id::TutTraskConfirmEntry},
    {48551, Id::TutTraskPaused},
    {48552, Id::TutTraskWalkTo},
    {48555, Id::TutTraskSecurity},
    {48556, Id::TutTraskHealWounded},
    {48324, Id::TutLevelUp},
    // Taris — Carth's stealth tutorial. His mouse-worded "Klickt auf die
    // Fähigkeit Tarnung, um diese zu aktivieren" line becomes the G-key hint,
    // fired as a popup at the reply break / dialogue close like the Trask set.
    {48557, Id::TutStealthMode},
};

// KOTOR 2 — every strref is a T3-M4 instruction line from the Ebon Hawk
// prologue (001EBO), mined from the module's _dlg.erf 2026-08-25. Most lines
// exist twice, once worded for the PC (mouse + vanilla default keys) and once
// for the Xbox pad; both variants map to the same hint id, so whichever the
// engine renders is caught. The spoken vanilla line is suppressed for these
// (see dialog_speech.cpp — T3 is a droid, so the human-VO suppression that
// covers Trask on KOTOR 1 does not apply) and the hint fires as a popup at
// the reply break, exactly like the Trask set.
constexpr DialogHint kDialogHintsK2[] = {
    {130726, Id::TutHintMovement},       // controls.dlg (PC)
    {130727, Id::TutHintMovement},       // controls.dlg (Xbox)
    {130735, Id::TutTraskFootlocker},    // target.dlg (PC) — use objects
    {130736, Id::TutTraskFootlocker},    // target.dlg (Xbox)
    {124183, Id::TutK2DialogueChoice},   // 3cfd_int.dlg (PC)
    {124184, Id::TutK2DialogueChoice},   // 3cfd_int.dlg (Xbox)
    {124181, Id::TutTraskLeader},        // 3cfd_end.dlg — party switching
    {124781, Id::TutHintAttack},         // combat.dlg (PC)
    {124782, Id::TutHintAttack},         // combat.dlg (Xbox)
    {132460, Id::TutHintAttack},         // combat.dlg (Xbox variant 2)
    {124786, Id::TutK2Pause},            // combat.dlg — pausing
    {124787, Id::TutK2Pause},            // combat.dlg (Xbox: WHITE button)
    {124788, Id::TutHintGrenade},        // combat.dlg (Xbox)
    {124789, Id::TutHintGrenade},        // combat.dlg (PC)
    {124783, Id::TutK2WeaponConfig},     // combat.dlg (PC) — two weapon configs
    {124784, Id::TutK2WeaponConfig},     // combat.dlg (Xbox)
    {124793, Id::TutK2EquipWeapon},      // combat.dlg — pick a weapon
    {124794, Id::TutK2EquipWeapon},      // combat.dlg (Xbox: START menu)
    {130047, Id::TutK2EquipWeapon},      // sens_drd.dlg — T3 needs a blaster
    {130048, Id::TutK2EquipWeapon},      // sens_drd.dlg (Xbox)
    {126827, Id::TutK2EquipWeapon},      // flameth.dlg — droid flamethrower
    {126828, Id::TutK2EquipWeapon},      // flameth.dlg (Xbox)
    { 74999, Id::TutTraskEquipOpen},     // minecntr.dlg — found armor
    {126834, Id::TutTraskEquipOpen},     // minecntr.dlg (Xbox)
    {123746, Id::TutK2TakeItem},         // plscylsp.dlg (PC) — the probe
    {123747, Id::TutK2TakeItem},         // plscylsp.dlg (Xbox)
    {123744, Id::TutK2Journal},          // journal.dlg — new quests (J on K2)
    {123743, Id::TutK2Journal},          // journal.dlg (Xbox)
    {130036, Id::TutHintMapScreen},      // map.dlg — map help
    {130037, Id::TutHintMapScreen},      // map.dlg (Xbox)
    {130030, Id::TutK2GalaxyMap},        // glxy_map.dlg (PC)
    {132456, Id::TutK2GalaxyMap},        // glxy_map.dlg (Xbox)
    {124804, Id::TutK2SoloMode},         // gar_door.dlg — solo-mode door
    {124805, Id::TutK2SoloMode},         // gar_door.dlg (Xbox: BACK button)
    {128196, Id::TutK2RepairKit},        // rep_kit.dlg (PC)
    {128197, Id::TutK2RepairKit},        // rep_kit.dlg (Xbox)
    {127843, Id::TutK2SecuritySpike},    // sectun.dlg (PC)
    {126838, Id::TutK2SecuritySpike},    // sectun.dlg (Xbox)
    {126837, Id::TutK2SecuritySpike},    // sectun.dlg — "lock won't open" variant
    {128553, Id::TutHintBash},           // fl_bash.dlg (PC)
    {123737, Id::TutHintBash},           // fl_bash.dlg (Xbox)
    {126337, Id::TutHintBash},           // fl_bash.dlg — "find a weapon" variant
    { 74965, Id::TutHintMine},           // blast.dlg — blow the door with a mine
    // Deliberately unmapped: 124798 (the damaged comm-console header) — its
    // one mouse clause is minor and the flavor text should stay audible.
};

constexpr int kDialogHintCountK1 =
    static_cast<int>(sizeof(kDialogHintsK1) / sizeof(kDialogHintsK1[0]));
constexpr int kDialogHintCountK2 =
    static_cast<int>(sizeof(kDialogHintsK2) / sizeof(kDialogHintsK2[0]));
constexpr int kDialogHintMax =
    kDialogHintCountK1 > kDialogHintCountK2 ? kDialogHintCountK1
                                            : kDialogHintCountK2;

// The running game's table. Selected once — the game cannot change mid-process.
inline const DialogHint* ActiveDialogHints(int* count) {
    if (acc::game::IsKotor2()) {
        *count = kDialogHintCountK2;
        return kDialogHintsK2;
    }
    *count = kDialogHintCountK1;
    return kDialogHintsK1;
}

// Resolved-text cache, built once the TLK is loaded.
struct ResolvedHint {
    char text[512];
    Id   id;
    bool valid;
};
ResolvedHint s_resolved[kDialogHintMax];
int          s_resolvedCount = 0;
bool         s_resolvedBuilt = false;

// Guards against a stray padding space between the engine's rendered label
// and its own TLK resolution of the same strref. Shared implementation —
// see engine_reads.h.
using acc::engine::EqualsTrimmed;

void BuildResolvedIfNeeded() {
    if (s_resolvedBuilt) return;
    int count = 0;
    const DialogHint* hints = ActiveDialogHints(&count);
    int resolvedCount = 0;
    for (int i = 0; i < count; ++i) {
        s_resolved[i].id    = hints[i].id;
        s_resolved[i].valid = false;
        s_resolved[i].text[0] = '\0';
        if (acc::engine::LookupTlk(hints[i].strref, s_resolved[i].text,
                                   sizeof(s_resolved[i].text))) {
            s_resolved[i].valid = true;
            ++resolvedCount;
        }
    }
    // Only latch "built" once we actually resolved at least one entry — before
    // the TLK table is live (main menu / early load) every lookup fails and we
    // must retry on a later call.
    if (resolvedCount > 0) {
        s_resolvedBuilt = true;
        s_resolvedCount = count;
        acclog::Write("TutorialHint", "dialog hint map built: %d/%d strrefs resolved",
                      resolvedCount, count);
    }
}

}  // namespace

int ReadTutorialRow(void* panel) {
    if (!panel) return -1;
    __try {
        return *reinterpret_cast<uint8_t*>(
            reinterpret_cast<unsigned char*>(panel) + kTutorialBoxRowOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

const char* HintForTutorialRow(int row) {
    // Rows 0-42 carry the same labels in both games' tutorial.2da (verified
    // against the K2 2DA 2026-08-25), so this block is shared. Rows only
    // KOTOR 2 fires — its extra rows 43+, plus rows that exist in KOTOR 1's
    // 2DA but were left unmapped there — follow in the K2-only block.
    Id id;
    bool have = true;
    switch (row) {
        case 0:  id = Id::TutHintCombatFeat;       break;  // Use_Combat_Feat
        case 1:  id = Id::TutHintGrenade;          break;  // Use_Grenade
        case 2:  id = Id::TutHintMine;             break;  // Set_Mine
        case 3:  id = Id::TutHintFriendlyPower;    break;  // Use_Friendly_Force_Power
        case 4:  id = Id::TutHintHostilePower;     break;  // Use_Hostile_Force_Power
        case 5:  id = Id::TutHintActionMenuScroll; break;  // Use_Vertical_Menu
        // Rows 7/12/14 name the Messages/Quests keys, which KOTOR 2's keymap
        // swaps (Quests J, Messages L — mirror of KOTOR 1), so they pick the
        // J/L-swapped K2 ids there.
        case 7:                                            // Press_Start
            id = acc::game::IsKotor2() ? Id::TutK2OpenScreens
                                       : Id::TutHintOpenScreens;
            break;
        case 8:  id = Id::TutHintCycleTargets;     break;  // Use_Triggers_To_Cycle...
        case 11: id = Id::TutHintEquipSlot;        break;  // Press_A_On_Equip_Screen
        case 20: id = Id::TutHintInventory;        break;  // party stash / inventory
        case 12:                                           // Enter_Messages_Screen
            id = acc::game::IsKotor2() ? Id::TutK2Messages
                                       : Id::TutHintMessages;
            break;
        case 13: id = Id::TutHintMapScreen;        break;  // Enter_Map_Screen
        case 14:                                           // Receive_Journal_Entry
            id = acc::game::IsKotor2() ? Id::TutK2Journal
                                       : Id::TutHintJournal;
            break;
        case 15: id = Id::TutHintPartyDies;        break;  // Party_Member_Dies
        case 21: id = Id::TutHintEnemyNear;        break;  // Hostile_Creature_Encountered
        case 33: id = Id::TutHintBash;             break;  // Bash
        case 34: id = Id::TutHintAttack;           break;  // Attack
        case 35: id = Id::TutHintAttackAuto;       break;  // Attack_Button_Mash
        case 42: id = Id::TutHintMovement;         break;  // Movement_Keys
        default: have = false;                     break;
    }
    if (!have && acc::game::IsKotor2()) {
        have = true;
        switch (row) {
            case 9:            id = Id::TutK2SwoopRace;    break;  // Start_Swoop_Race
            case 29: case 30:  id = Id::TutK2ActionQueue;  break;  // X-button queue
            case 31: case 32:  id = Id::TutK2ActionRemove; break;  // Y-button remove
            case 43:           id = Id::TutK2PlasmaTorch;  break;  // Bash_Plasma_Torch
            case 44:           id = Id::TutK2FirstPerson;  break;  // First_Person_Hint
            case 45:           id = Id::TutK2SpaceSuit;    break;  // K2_Temp3
            case 48: case 57:  id = Id::TutK2CommRoom;     break;  // K2_Temp6/15 (same strref)
            case 49:           id = Id::TutK2Mines;        break;  // K2_Temp7
            case 52:           id = Id::TutK2Vibrocutter;  break;  // Vibrocutter_Hint
            case 58:           id = Id::TutTraskMedkit;    break;  // Medpac_hint
            default:           have = false;               break;
        }
    }
    if (!have) return nullptr;
    // Text(), not Get(): a gamepad turns every one of these into its
    // controller wording, from the one predicate every hint surface asks.
    const char* s = acc::binding_hints::Text(id);
    return (s[0]) ? s : nullptr;
}

// The message strrefs (the Message columns of tutorial.2da) for every mapped
// row, paired with that row's keyboard-hint Id. The single-row message listbox
// announces these mouse-worded strings; we resolve them through the engine TLK
// so we can (a) suppress the raw announce on the popup's first-sight listbox
// row, and (b) substitute the keyboard hint when the user arrow-navigates onto
// the message in the popup's chain. Covers every page of the multi-message rows
// (K1: Attack 48566+42458, Attack_Button_Mash 48567+48568).
struct MouseMsg {
    uint32_t strref;
    Id       hint;
};
constexpr MouseMsg kTutorialMouseMsgsK1[] = {
    {48559, Id::TutHintCombatFeat},       // row 0  Use_Combat_Feat
    {48560, Id::TutHintGrenade},          // row 1  Use_Grenade
    {48366, Id::TutHintMine},             // row 2  Set_Mine
    {48367, Id::TutHintFriendlyPower},    // row 3  Use_Friendly_Force_Power
    {48563, Id::TutHintHostilePower},     // row 4  Use_Hostile_Force_Power
    {48564, Id::TutHintActionMenuScroll}, // row 5  Use_Vertical_Menu
    {48371, Id::TutHintOpenScreens},      // row 7  Press_Start
    {48372, Id::TutHintCycleTargets},     // row 8  Use_Triggers_To_Cycle...
    {48374, Id::TutHintEquipSlot},        // row 11 Press_A_On_Equip_Screen
    {42136, Id::TutHintInventory},        // row 20 party stash / inventory
    {41880, Id::TutHintMessages},         // row 12 Enter_Messages_Screen
    {41881, Id::TutHintMapScreen},        // row 13 Enter_Map_Screen
    {41882, Id::TutHintJournal},          // row 14 Receive_Journal_Entry
    {41883, Id::TutHintPartyDies},        // row 15 Party_Member_Dies
    {48375, Id::TutHintEnemyNear},        // row 21 Hostile_Creature_Encountered
    {48565, Id::TutHintBash},             // row 33 Bash
    {48566, Id::TutHintAttack},           // row 34 Attack page 0
    {42458, Id::TutHintAttack},           // row 34 Attack page 1
    {48567, Id::TutHintAttackAuto},       // row 35 Attack_Button_Mash page 0
    {48568, Id::TutHintAttackAuto},       // row 35 Attack_Button_Mash page 1
    {49121, Id::TutHintMovement},         // row 42 Movement_Keys
};

// KOTOR 2's tutorial.2da splits the columns into message_pc* and
// message_xbox* (mined 2026-08-25). The engine reads message_pc on PC
// (SetNextMessage @0x00899630 formats "Message_PC%i" unconditionally), but
// both variants are listed where they differ so a build that ever renders the
// pad column is still caught — text matching is cheap and cannot collide.
constexpr MouseMsg kTutorialMouseMsgsK2[] = {
    {49009, Id::TutHintCombatFeat},       // row 0  pc
    {41868, Id::TutHintCombatFeat},       // row 0  xbox
    {49010, Id::TutHintGrenade},          // row 1  pc
    {41869, Id::TutHintGrenade},          // row 1  xbox
    {49011, Id::TutHintMine},             // row 2  pc
    {41870, Id::TutHintMine},             // row 2  xbox
    {49012, Id::TutHintFriendlyPower},    // row 3  pc
    {41871, Id::TutHintFriendlyPower},    // row 3  xbox
    {49013, Id::TutHintHostilePower},     // row 4  pc
    {41872, Id::TutHintHostilePower},     // row 4  xbox
    {49014, Id::TutHintActionMenuScroll}, // row 5  pc
    {41873, Id::TutHintActionMenuScroll}, // row 5  xbox
    {49015, Id::TutK2OpenScreens},        // row 7  pc (J/L swapped)
    {41875, Id::TutK2OpenScreens},        // row 7  xbox
    {49016, Id::TutHintCycleTargets},     // row 8  pc
    {41876, Id::TutHintCycleTargets},     // row 8  xbox
    {49017, Id::TutK2SwoopRace},          // row 9  pc
    {41877, Id::TutK2SwoopRace},          // row 9  xbox
    {49018, Id::TutHintEquipSlot},        // row 11 pc
    {41879, Id::TutHintEquipSlot},        // row 11 xbox
    {41880, Id::TutK2Messages},           // row 12 (shared column; L on K2)
    {41881, Id::TutHintMapScreen},        // row 13 (shared column)
    {41882, Id::TutK2Journal},            // row 14 (shared column; J on K2)
    {41883, Id::TutHintPartyDies},        // row 15 (shared column)
    {42136, Id::TutHintInventory},        // row 20 (shared column)
    {49019, Id::TutHintEnemyNear},        // row 21 pc
    {42137, Id::TutHintEnemyNear},        // row 21 xbox
    {49021, Id::TutK2ActionQueue},        // row 29 pc
    {49022, Id::TutK2ActionQueue},        // row 30 pc
    {42461, Id::TutK2ActionQueue},        // rows 29/30 xbox
    {49023, Id::TutK2ActionRemove},       // rows 31/32 pc
    {42462, Id::TutK2ActionRemove},       // rows 31/32 xbox
    {49024, Id::TutHintBash},             // row 33 pc
    {42174, Id::TutHintBash},             // row 33 xbox
    {49025, Id::TutHintAttack},           // row 34 page 0 pc
    {42457, Id::TutHintAttack},           // row 34 page 0 xbox
    {42458, Id::TutHintAttack},           // row 34 page 1 (shared column)
    {49026, Id::TutHintAttackAuto},       // row 35 page 0 pc
    {42459, Id::TutHintAttackAuto},       // row 35 page 0 xbox
    {49027, Id::TutHintAttackAuto},       // row 35 page 1 pc
    {42460, Id::TutHintAttackAuto},       // row 35 page 1 xbox
    {49028, Id::TutHintMovement},         // row 42 pc (no xbox column)
    {48221, Id::TutK2PlasmaTorch},        // row 43 (shared column)
    {48222, Id::TutK2FirstPerson},        // row 44 (shared column)
    {48223, Id::TutK2SpaceSuit},          // row 45 (shared column)
    {48226, Id::TutK2CommRoom},           // rows 48+57 (shared column)
    {48227, Id::TutK2Mines},              // row 49 (shared column)
    {48230, Id::TutK2Vibrocutter},        // row 52 (shared column)
    {48629, Id::TutTraskMedkit},          // row 58 (shared column)
};

constexpr int kTutorialMouseMsgCountK1 =
    static_cast<int>(sizeof(kTutorialMouseMsgsK1) / sizeof(kTutorialMouseMsgsK1[0]));
constexpr int kTutorialMouseMsgCountK2 =
    static_cast<int>(sizeof(kTutorialMouseMsgsK2) / sizeof(kTutorialMouseMsgsK2[0]));
constexpr int kTutorialMouseMsgMax =
    kTutorialMouseMsgCountK1 > kTutorialMouseMsgCountK2 ? kTutorialMouseMsgCountK1
                                                        : kTutorialMouseMsgCountK2;

inline const MouseMsg* ActiveMouseMsgs(int* count) {
    if (acc::game::IsKotor2()) {
        *count = kTutorialMouseMsgCountK2;
        return kTutorialMouseMsgsK2;
    }
    *count = kTutorialMouseMsgCountK1;
    return kTutorialMouseMsgsK1;
}

// 1024, not 512: KOTOR 2's longest mapped popup text (the mine tutorial,
// 48227) is ~850 bytes, and the announce paths hand rendered text around in
// kAnnounceTextMax (1024) buffers — the stored copy must not truncate earlier
// than the rendered one or EqualsTrimmed never matches.
char s_mouseText[kTutorialMouseMsgMax][1024];
int  s_mouseTextCount = 0;
bool s_mouseTextBuilt = false;

void BuildMouseTextIfNeeded() {
    if (s_mouseTextBuilt) return;
    int count = 0;
    const MouseMsg* msgs = ActiveMouseMsgs(&count);
    int resolved = 0;
    for (int i = 0; i < count; ++i) {
        s_mouseText[i][0] = '\0';
        if (acc::engine::LookupTlk(msgs[i].strref, s_mouseText[i],
                                   sizeof(s_mouseText[i]))) {
            ++resolved;
        }
    }
    if (resolved > 0) {
        s_mouseTextBuilt = true;
        s_mouseTextCount = count;
        acclog::Write("TutorialHint", "popup mouse-text map built: %d/%d strrefs resolved",
                      resolved, count);
    }
}

const char* HintForMouseText(const char* text) {
    if (!text || !text[0]) return nullptr;
    BuildMouseTextIfNeeded();
    if (!s_mouseTextBuilt) return nullptr;
    int count = 0;
    const MouseMsg* msgs = ActiveMouseMsgs(&count);
    for (int i = 0; i < s_mouseTextCount; ++i) {
        if (s_mouseText[i][0] && EqualsTrimmed(text, s_mouseText[i])) {
            const char* s = acc::binding_hints::Text(msgs[i].hint);
            return (s[0]) ? s : nullptr;
        }
    }
    return nullptr;
}

// ---- Forced-spoken VO-less subtitle lines --------------------------------
//
// Curated strrefs whose dialogue node ships as subtitle text with no VO. The
// speaker (Trask) classifies as a voiced human, so dialog_speech's human-
// subtitle suppression would silence these entirely. We resolve each strref to
// the current locale's text once (same lazy TLK path as the hint map) and
// trim-match it against the rendered line so dialog_speech can force it spoken.
//
//   39454 — "Du hast genug Erfahrung, um aufzusteigen. Mache den Levelaufstieg,
//            bevor du durch diese Tür gehst." Trask's Endar Spire level-up-gate
//            line, said when the player tries the locked door before assigning
//            their first level. No VO recorded; subtitle only. (verified in the
//            German dialog.tlk, 2026-07-19)
constexpr uint32_t kForcedSpokenStrrefs[] = { 39454 };
constexpr int kForcedSpokenCount =
    static_cast<int>(sizeof(kForcedSpokenStrrefs) / sizeof(kForcedSpokenStrrefs[0]));

char s_forcedText[kForcedSpokenCount][512];
bool s_forcedTextBuilt = false;

void BuildForcedTextIfNeeded() {
    if (s_forcedTextBuilt) return;
    int resolved = 0;
    for (int i = 0; i < kForcedSpokenCount; ++i) {
        s_forcedText[i][0] = '\0';
        if (acc::engine::LookupTlk(kForcedSpokenStrrefs[i], s_forcedText[i],
                                   sizeof(s_forcedText[i]))) {
            ++resolved;
        }
    }
    if (resolved > 0) {
        s_forcedTextBuilt = true;
        acclog::Write("TutorialHint", "forced-spoken map built: %d/%d strrefs resolved",
                      resolved, kForcedSpokenCount);
    }
}

const char* HintForDialogLine(const char* renderedLine, uint32_t* outStrref) {
    if (!renderedLine || !renderedLine[0]) return nullptr;
    BuildResolvedIfNeeded();
    if (!s_resolvedBuilt) return nullptr;
    int count = 0;
    const DialogHint* hints = ActiveDialogHints(&count);
    for (int i = 0; i < s_resolvedCount; ++i) {
        if (!s_resolved[i].valid) continue;
        if (EqualsTrimmed(renderedLine, s_resolved[i].text)) {
            const char* s = acc::binding_hints::Text(s_resolved[i].id);
            if (!s[0]) return nullptr;
            // s_resolved is built parallel to the active hint table, so index
            // i carries the source strref.
            if (outStrref) *outStrref = hints[i].strref;
            return s;
        }
    }
    return nullptr;
}

bool IsForcedSpokenDialogLine(const char* renderedLine) {
    if (!renderedLine || !renderedLine[0]) return false;
    // The curated strrefs are KOTOR 1 lines; on KOTOR 2 the same numbers name
    // unrelated strings, so resolving them would arm the matcher with garbage.
    if (acc::game::IsKotor2()) return false;
    BuildForcedTextIfNeeded();
    if (!s_forcedTextBuilt) return false;
    for (int i = 0; i < kForcedSpokenCount; ++i) {
        if (s_forcedText[i][0] && EqualsTrimmed(renderedLine, s_forcedText[i])) {
            return true;
        }
    }
    return false;
}

// ---- Voiced subtitle lines to suppress ------------------------------------
//
// The INVERSE of the forced-spoken list: curated strrefs whose line carries
// real recorded voice-over, but whose speaker the classifier cannot resolve —
// so the human-VO suppression never fires and the mod speaks the subtitle
// over the actor. First entries (2026-08-26): the KOTOR 2 Ebon Hawk
// prologue's narrator, "the voice of the Ebon Hawk" (strref 134048 names her)
// — her lines render as ownerless barks and cinematic-copy panels
// (speaker=[?], appearance -1), and every one below has its VO file verified
// present under StreamVoice/001/. The prologue's silent lines (the security
// consoles, whose VO resrefs have no audio on disk) are deliberately NOT
// here: with no voice behind them, the subtitle is the only channel.
//
// Tutorial-mapped lines (kDialogHintsK2) are also not here — dialog_speech
// suppresses those through the hint match and replaces them with a popup.
constexpr uint32_t kVoicedSuppressedK2[] = {
    74957, 74958, 124138, 124139, 125865, 125866, 125867, 125868,  // 3cfd
    125872, 125874, 125889, 127840, 127841,
    124182,  // 3cfd_end
    124185, 125637,  // 3cfd_int
    124785, 124791, 124792, 124796, 124797, 126017, 131370, 131371,  // combat
    131372,
    131373,  // comcon
    126331, 126332, 126333, 126334, 126335, 126336,  // comp_voc (ship status)
    118948, 118949, 132430,  // eng_door
    117948, 130728,  // eng_port
    117949,  // eng_star
    128552,  // exit
    132431, 132432, 132433, 132434, 132435, 132436, 132437, 132438,  // extra
    132439, 132440, 132441, 132442, 132443, 132444, 132445, 132446,  // (nav +
    132447, 132448, 132449, 132450, 132451, 132452, 132453, 132454,  //  areas)
    132455,
    123738,  // fl_bash (workbench aside)
    124806,  // gar_door (hull-damage flavor)
    123740,  // glxy_map (map intro)
    124187,  // got_meds
    74991,  // hk50dr
    74992, 117953, 117954, 117955, 117960, 117961, 117962, 123361,  // hyper
    130033, 131379,
    74993,  // impdoor
    118950, 118951, 118952, 125643, 125645, 125647, 130034, 130035,  // intro
    131380, 134048,
    116311,  // kre_body
    126831,  // lift_002
    124192,  // minecntr (needs-key flavor)
    125708,  // no_mines
    130040,  // outro
    75004,  // pcbody
    75005,  // pilotdr
    75006,  // plscylsp (container aside)
    75010,  // secdoor
    75011,  // secdoor2
    75012,  // secspk
    132458,  // sectun (difficulty aside)
    130049,  // sens_drd ("now hostile")
    128556, 128557, 128558, 128559, 128560, 128561, 128562, 128563,  // sparkwir
    131383,  // stardoor
    123776,  // tr_lift
    117970, 117971, 117972, 117973, 117974, 117975,  // treatinj (<FullName>)
    123780, 123781, 123782,  // wrkbnch
};
constexpr int kVoicedSuppressedCount = static_cast<int>(
    sizeof(kVoicedSuppressedK2) / sizeof(kVoicedSuppressedK2[0]));

char s_voicedText[kVoicedSuppressedCount][512];
bool s_voicedTextBuilt = false;

void BuildVoicedTextIfNeeded() {
    if (s_voicedTextBuilt) return;
    int resolved = 0;
    for (int i = 0; i < kVoicedSuppressedCount; ++i) {
        s_voicedText[i][0] = '\0';
        if (acc::engine::LookupTlk(kVoicedSuppressedK2[i], s_voicedText[i],
                                   sizeof(s_voicedText[i]))) {
            ++resolved;
        }
    }
    if (resolved > 0) {
        s_voicedTextBuilt = true;
        acclog::Write("TutorialHint", "voiced-suppress map built: %d/%d strrefs resolved",
                      resolved, kVoicedSuppressedCount);
    }
}

// memcmp-based strstr for a length-bounded needle segment.
const char* FindSegment(const char* hay, const char* needle, size_t n) {
    if (n == 0) return hay;
    size_t hayLen = std::strlen(hay);
    if (hayLen < n) return nullptr;
    for (size_t i = 0; i + n <= hayLen; ++i) {
        if (std::memcmp(hay + i, needle, n) == 0) return hay + i;
    }
    return nullptr;
}

// EqualsTrimmed, extended for lines the engine token-substitutes at render
// time (<FullName> and friends, e.g. the treatinj medbay lines): the stored
// TLK text's literal segments must appear left to right in the rendered line,
// a token matches any run of characters, and — like EqualsTrimmed — leading /
// trailing padding spaces don't count. Degenerates to EqualsTrimmed when the
// stored text has no token.
bool MatchesWithTokens(const char* rendered, const char* stored) {
    if (std::strchr(stored, '<') == nullptr) {
        return EqualsTrimmed(rendered, stored);
    }
    while (*rendered == ' ') ++rendered;
    while (*stored == ' ') ++stored;
    const char* r = rendered;
    const char* s = stored;
    bool first = true;
    bool endsWithToken = false;
    while (*s) {
        if (*s == '<') {
            const char* close = std::strchr(s, '>');
            if (!close) { endsWithToken = true; break; }  // malformed tail
            s = close + 1;
            endsWithToken = (*s == '\0');
            first = false;
            continue;
        }
        const char* tok = std::strchr(s, '<');
        size_t segLen = tok ? static_cast<size_t>(tok - s) : std::strlen(s);
        size_t cmpLen = segLen;
        if (!tok) {  // final literal: ignore its trailing padding
            while (cmpLen > 0 && s[cmpLen - 1] == ' ') --cmpLen;
        }
        if (first) {
            if (std::strncmp(r, s, cmpLen) != 0) return false;
            r += cmpLen;
        } else {
            const char* hit = FindSegment(r, s, cmpLen);
            if (!hit) return false;
            r = hit + cmpLen;
        }
        s += segLen;
        first = false;
    }
    if (endsWithToken) return true;  // a trailing token swallows the rest
    while (*r == ' ') ++r;           // after the final literal: padding only
    return *r == '\0';
}

bool IsVoicedSuppressedLine(const char* renderedLine) {
    if (!renderedLine || !renderedLine[0]) return false;
    // The curated strrefs are KOTOR 2 lines; a future KOTOR 1 entry gets its
    // own table (see the forced-spoken list for the mirror-image gate).
    if (!acc::game::IsKotor2()) return false;
    BuildVoicedTextIfNeeded();
    if (!s_voicedTextBuilt) return false;
    for (int i = 0; i < kVoicedSuppressedCount; ++i) {
        if (s_voicedText[i][0] &&
            MatchesWithTokens(renderedLine, s_voicedText[i])) {
            return true;
        }
    }
    return false;
}

}  // namespace acc::tutorial_hints
