#include "tutorial_hints.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_reads.h"    // LookupTlk
#include "log.h"
#include "strings.h"

namespace acc::tutorial_hints {

namespace {

using acc::strings::Id;

// CSWGuiTutorialBox +0x994 (uint8) — the source tutorial.2da row index the
// popup was built from. RE-confirmed: SetTutorialReason writes it, SetNextMessage
// reads it back as the 2DA row. Survives after the popup is built, so a poll
// recovers the popup's identity language-independently.
constexpr size_t kTutorialBoxRowOffset = 0x994;

// Surface 2 map: source strref -> hint Id. Resolved to the current language's
// text lazily via the engine TLK, then matched against the rendered dialogue
// line. Strrefs are the Endar Spire Trask / pop-window mouse lines.
struct DialogHint {
    uint32_t strref;
    Id       id;
};
constexpr DialogHint kDialogHints[] = {
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
};
constexpr int kDialogHintCount =
    static_cast<int>(sizeof(kDialogHints) / sizeof(kDialogHints[0]));

// Resolved-text cache, built once the TLK is loaded.
struct ResolvedHint {
    char text[512];
    Id   id;
    bool valid;
};
ResolvedHint s_resolved[kDialogHintCount];
bool         s_resolvedBuilt = false;

// Case-sensitive compare that ignores leading/trailing whitespace on both
// sides — guards against a stray padding space between the engine's rendered
// label and its own TLK resolution of the same strref.
bool EqualsTrimmed(const char* a, const char* b) {
    while (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r') ++a;
    while (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r') ++b;
    size_t la = std::strlen(a), lb = std::strlen(b);
    while (la > 0 && (a[la - 1] == ' ' || a[la - 1] == '\t' ||
                      a[la - 1] == '\n' || a[la - 1] == '\r')) --la;
    while (lb > 0 && (b[lb - 1] == ' ' || b[lb - 1] == '\t' ||
                      b[lb - 1] == '\n' || b[lb - 1] == '\r')) --lb;
    return la == lb && std::memcmp(a, b, la) == 0;
}

void BuildResolvedIfNeeded() {
    if (s_resolvedBuilt) return;
    int resolvedCount = 0;
    for (int i = 0; i < kDialogHintCount; ++i) {
        s_resolved[i].id    = kDialogHints[i].id;
        s_resolved[i].valid = false;
        s_resolved[i].text[0] = '\0';
        if (acc::engine::LookupTlk(kDialogHints[i].strref, s_resolved[i].text,
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
        acclog::Write("TutorialHint", "dialog hint map built: %d/%d strrefs resolved",
                      resolvedCount, kDialogHintCount);
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
    Id id;
    switch (row) {
        case 0:  id = Id::TutHintCombatFeat;       break;  // Use_Combat_Feat
        case 1:  id = Id::TutHintGrenade;          break;  // Use_Grenade
        case 2:  id = Id::TutHintMine;             break;  // Set_Mine
        case 3:  id = Id::TutHintFriendlyPower;    break;  // Use_Friendly_Force_Power
        case 4:  id = Id::TutHintHostilePower;     break;  // Use_Hostile_Force_Power
        case 5:  id = Id::TutHintActionMenuScroll; break;  // Use_Vertical_Menu
        case 7:  id = Id::TutHintOpenScreens;      break;  // Press_Start
        case 8:  id = Id::TutHintCycleTargets;     break;  // Use_Triggers_To_Cycle...
        case 11: id = Id::TutHintEquipSlot;        break;  // Press_A_On_Equip_Screen
        case 20: id = Id::TutHintInventory;        break;  // party stash / inventory
        case 12: id = Id::TutHintMessages;         break;  // Enter_Messages_Screen
        case 13: id = Id::TutHintMapScreen;        break;  // Enter_Map_Screen
        case 14: id = Id::TutHintJournal;          break;  // Receive_Journal_Entry
        case 15: id = Id::TutHintPartyDies;        break;  // Party_Member_Dies
        case 21: id = Id::TutHintEnemyNear;        break;  // Hostile_Creature_Encountered
        case 33: id = Id::TutHintBash;             break;  // Bash
        case 34: id = Id::TutHintAttack;           break;  // Attack
        case 35: id = Id::TutHintAttackAuto;       break;  // Attack_Button_Mash
        case 42: id = Id::TutHintMovement;         break;  // Movement_Keys
        default: return nullptr;
    }
    const char* s = acc::strings::Get(id);
    return (s && s[0]) ? s : nullptr;
}

// The message strrefs (Message0/1/2 columns of tutorial.2da) for every mapped
// row, paired with that row's keyboard-hint Id. The single-row message listbox
// announces these mouse-worded strings; we resolve them through the engine TLK
// so we can (a) suppress the raw announce on the popup's first-sight listbox
// row, and (b) substitute the keyboard hint when the user arrow-navigates onto
// the message in the popup's chain. Covers every page of the multi-message rows
// (Attack 48566+42458, Attack_Button_Mash 48567+48568).
struct MouseMsg {
    uint32_t strref;
    Id       hint;
};
constexpr MouseMsg kTutorialMouseMsgs[] = {
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
constexpr int kTutorialMouseMsgCount =
    static_cast<int>(sizeof(kTutorialMouseMsgs) / sizeof(kTutorialMouseMsgs[0]));

char s_mouseText[kTutorialMouseMsgCount][512];
bool s_mouseTextBuilt = false;

void BuildMouseTextIfNeeded() {
    if (s_mouseTextBuilt) return;
    int resolved = 0;
    for (int i = 0; i < kTutorialMouseMsgCount; ++i) {
        s_mouseText[i][0] = '\0';
        if (acc::engine::LookupTlk(kTutorialMouseMsgs[i].strref, s_mouseText[i],
                                   sizeof(s_mouseText[i]))) {
            ++resolved;
        }
    }
    if (resolved > 0) {
        s_mouseTextBuilt = true;
        acclog::Write("TutorialHint", "popup mouse-text map built: %d/%d strrefs resolved",
                      resolved, kTutorialMouseMsgCount);
    }
}

const char* HintForMouseText(const char* text) {
    if (!text || !text[0]) return nullptr;
    BuildMouseTextIfNeeded();
    if (!s_mouseTextBuilt) return nullptr;
    for (int i = 0; i < kTutorialMouseMsgCount; ++i) {
        if (s_mouseText[i][0] && EqualsTrimmed(text, s_mouseText[i])) {
            const char* s = acc::strings::Get(kTutorialMouseMsgs[i].hint);
            return (s && s[0]) ? s : nullptr;
        }
    }
    return nullptr;
}

bool IsSuppressedTutorialText(const char* text) {
    return HintForMouseText(text) != nullptr;
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
    for (int i = 0; i < kDialogHintCount; ++i) {
        if (!s_resolved[i].valid) continue;
        if (EqualsTrimmed(renderedLine, s_resolved[i].text)) {
            const char* s = acc::strings::Get(s_resolved[i].id);
            if (!s || !s[0]) return nullptr;
            // s_resolved is built parallel to kDialogHints, so index i carries
            // the source strref.
            if (outStrref) *outStrref = kDialogHints[i].strref;
            return s;
        }
    }
    return nullptr;
}

bool IsForcedSpokenDialogLine(const char* renderedLine) {
    if (!renderedLine || !renderedLine[0]) return false;
    BuildForcedTextIfNeeded();
    if (!s_forcedTextBuilt) return false;
    for (int i = 0; i < kForcedSpokenCount; ++i) {
        if (s_forcedText[i][0] && EqualsTrimmed(renderedLine, s_forcedText[i])) {
            return true;
        }
    }
    return false;
}

}  // namespace acc::tutorial_hints
