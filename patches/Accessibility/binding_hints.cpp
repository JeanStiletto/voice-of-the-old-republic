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
    // KOTOR 2 hints. TutK2FirstPerson, TutK2Pause and TutK2ActionRemove have
    // no twin on purpose (see strings.h) — absent here means Text() keeps the
    // keyboard wording, which stays truthful with a pad connected.
    { S::TutK2SwoopRace, S::TutK2SwoopRacePad },
    { S::TutK2ActionQueue, S::TutK2ActionQueuePad },
    { S::TutK2PlasmaTorch, S::TutK2PlasmaTorchPad },
    { S::TutK2SpaceSuit, S::TutK2SpaceSuitPad },
    { S::TutK2CommRoom, S::TutK2CommRoomPad },
    { S::TutK2Mines, S::TutK2MinesPad },
    { S::TutK2Vibrocutter, S::TutK2VibrocutterPad },
    { S::TutK2DialogueChoice, S::TutK2DialogueChoicePad },
    { S::TutK2EquipWeapon, S::TutK2EquipWeaponPad },
    { S::TutK2WeaponConfig, S::TutK2WeaponConfigPad },
    { S::TutK2TakeItem, S::TutK2TakeItemPad },
    { S::TutK2GalaxyMap, S::TutK2GalaxyMapPad },
    { S::TutK2SoloMode, S::TutK2SoloModePad },
    { S::TutK2RepairKit, S::TutK2RepairKitPad },
    { S::TutK2SecuritySpike, S::TutK2SecuritySpikePad },
    // Swoop race — spoken by minigame_swoop_race.cpp, not a tutorial popup,
    // but it names controls all the same.
    { S::SwoopRaceControls, S::SwoopRaceControlsPad },
    { S::SwoopRaceControlsK2, S::SwoopRaceControlsK2Pad },
    { S::SwoopRaceStuckHint, S::SwoopRaceStuckHintPad },
    { S::SwoopRaceStuckHintLeft, S::SwoopRaceStuckHintLeftPad },
    { S::SwoopRaceStuckHintRight, S::SwoopRaceStuckHintRightPad },
    { S::SwoopRaceStuckBarWallLeft, S::SwoopRaceStuckBarWallLeftPad },
    { S::SwoopRaceStuckBarWallRight, S::SwoopRaceStuckBarWallRightPad },
    { S::SwoopRaceStuckBarStrip, S::SwoopRaceStuckBarStripPad },
    { S::SwoopRaceStuckBarJump, S::SwoopRaceStuckBarJumpPad },
    // The J/L-swap ids share their pad wording with the K1 twins — the pad
    // texts name no letter keys, so one pad id serves both games.
    { S::TutK2OpenScreens, S::TutHintOpenScreensPad },
    { S::TutK2Messages, S::TutHintMessagesPad },
    { S::TutK2Journal, S::TutHintJournalPad },
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
