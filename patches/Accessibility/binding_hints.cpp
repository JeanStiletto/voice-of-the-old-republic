// See binding_hints.h for why this is one TU and not an inline predicate.

#include "binding_hints.h"

#include "log.h"
#include "pad_input.h"

namespace acc::binding_hints {

namespace {

using S = acc::strings::Id;

// keyboard id -> controller id. Order mirrors the twin block in strings.h so
// the two can be read side by side; lookup is a linear scan, which is right for
// a table this size asked a handful of times per tutorial popup.
struct Twin {
    S kb;
    S pad;
};

constexpr Twin kTwins[] = {
    { S::TutHintCombatFeat, S::TutHintCombatFeatPad },
    { S::TutHintGrenade, S::TutHintGrenadePad },
    { S::TutHintMine, S::TutHintMinePad },
    { S::TutHintFriendlyPower, S::TutHintFriendlyPowerPad },
    { S::TutHintHostilePower, S::TutHintHostilePowerPad },
    { S::TutHintActionMenuScroll, S::TutHintActionMenuScrollPad },
    { S::TutHintOpenScreens, S::TutHintOpenScreensPad },
    { S::TutHintCycleTargets, S::TutHintCycleTargetsPad },
    { S::TutHintEquipSlot, S::TutHintEquipSlotPad },
    { S::TutHintEnemyNear, S::TutHintEnemyNearPad },
    { S::TutHintBash, S::TutHintBashPad },
    { S::TutHintAttack, S::TutHintAttackPad },
    { S::TutHintAttackAuto, S::TutHintAttackAutoPad },
    { S::TutHintMovement, S::TutHintMovementPad },
    { S::TutHintMapScreen, S::TutHintMapScreenPad },
    { S::TutHintJournal, S::TutHintJournalPad },
    { S::TutHintMessages, S::TutHintMessagesPad },
    { S::TutHintPartyDies, S::TutHintPartyDiesPad },
    { S::TutHintInventory, S::TutHintInventoryPad },
    { S::TutTraskGettingStarted, S::TutTraskGettingStartedPad },
    { S::TutTraskEquipOpen, S::TutTraskEquipOpenPad },
    { S::TutTraskEquipBrowse, S::TutTraskEquipBrowsePad },
    { S::TutTraskEquipWeapon, S::TutTraskEquipWeaponPad },
    { S::TutTraskCamera, S::TutTraskCameraPad },
    { S::TutTraskFootlocker, S::TutTraskFootlockerPad },
    { S::TutTraskPickItem, S::TutTraskPickItemPad },
    { S::TutTraskLeader, S::TutTraskLeaderPad },
    { S::TutTraskMakeLeader, S::TutTraskMakeLeaderPad },
    { S::TutTraskMenus, S::TutTraskMenusPad },
    { S::TutTraskTabs, S::TutTraskTabsPad },
    { S::TutTraskActionMenu, S::TutTraskActionMenuPad },
    { S::TutTraskMedkit, S::TutTraskMedkitPad },
    { S::TutTraskOpenDoor, S::TutTraskOpenDoorPad },
    { S::TutTraskActivateEntry, S::TutTraskActivateEntryPad },
    { S::TutTraskTargetMenu, S::TutTraskTargetMenuPad },
    { S::TutTraskConfirmEntry, S::TutTraskConfirmEntryPad },
    { S::TutTraskPaused, S::TutTraskPausedPad },
    { S::TutTraskWalkTo, S::TutTraskWalkToPad },
    { S::TutTraskSecurity, S::TutTraskSecurityPad },
    { S::TutTraskHealWounded, S::TutTraskHealWoundedPad },
    { S::TutLevelUp, S::TutLevelUpPad },
    { S::TutStealthMode, S::TutStealthModePad },
    { S::DialogRepeatLineHint, S::DialogRepeatLineHintPad },
};
constexpr int kTwinCount = static_cast<int>(sizeof(kTwins) / sizeof(kTwins[0]));

// Log the flip, not the state: the wording changing under the player is the
// thing worth seeing in a log, and Text() is called far too often to trace.
bool g_lastActive  = false;
bool g_everLogged  = false;

}  // namespace

bool ControllerActive() {
    const bool active = acc::pad::Connected();
    if (!g_everLogged || active != g_lastActive) {
        acclog::Write("Hints", "wording device: %s",
                      active ? "controller" : "keyboard");
        g_lastActive = active;
        g_everLogged = true;
    }
    return active;
}

S IdFor(S keyboardId) {
    if (!ControllerActive()) return keyboardId;
    for (int i = 0; i < kTwinCount; ++i) {
        if (kTwins[i].kb != keyboardId) continue;
        // An untranslated twin is not an error and must not silence the hint —
        // the keyboard wording is still true with a pad plugged in. Same rule
        // the language tables already follow for a missing case.
        if (acc::strings::Get(kTwins[i].pad)[0]) return kTwins[i].pad;
        return keyboardId;
    }
    return keyboardId;
}

const char* Text(S keyboardId) {
    return acc::strings::Get(IdFor(keyboardId));
}

}  // namespace acc::binding_hints
