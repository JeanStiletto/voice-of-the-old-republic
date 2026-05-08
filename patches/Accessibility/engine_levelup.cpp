#include "engine_levelup.h"

#include <windows.h>
#include <cstdint>

#include "engine_panels.h"  // ResolveGuiInGame
#include "log.h"

namespace acc::engine_levelup {

namespace {

// CGuiInGame::ShowLevelUpGUI — __thiscall(int) -> undefined4 @0x0062dc00.
// Top-level dispatcher; tested in patch-20260505-144838.log returned 0
// without opening a panel because of the same gate as
// CSWGuiInGameCharacter::ShowLevelUpGUI (level_up_mode == 0). Kept as a
// fallback when the InGameCharacter panel slot is null.
constexpr uintptr_t kAddrCGuiInGameShowLevelUpGUI = 0x0062dc00;

// CSWGuiInGameCharacter::ShowLevelUpGUI — __thiscall(int) @0x006b0bb0.
// btn_levelup click handler. RE'd via runtime peek (DumpFunctionBytes
// in core_dllmain — dump kept in patch-20260505-150749.log line for
// "RE-peek CSWGuiInGameCharacter::ShowLevelUpGUI"). Gate sequence:
//
//   ECX = AppManager.client_app
//   gui_in_game = client_app->GetInGameGui()           ; CALL 0x005ED690
//   if (gui_in_game->level_up_mode == 0) return 0;     ; field at +0x10C
//   if (client_app->GetCharacterChangeInProgress())    ; CALL 0x005EE190
//       return 0;
//   this->SetStats();                                  ; CALL 0x006AFDA0
//   panel = operator new(0x2560);
//   ... build CSWGuiLevelUpPanel from this->field_59E4 (CSWCCreature*) ...
//
// Both gates explain the 0-return-no-panel behaviour seen in our runs:
// `level_up_mode` is 0 in normal gameplay and only flipped to 1 when
// the engine wants to open the level-up wizard (via SetLevelUpMode below).
constexpr uintptr_t kAddrCSWGuiInGameCharacterShowLevelUpGUI = 0x006b0bb0;

// CGuiInGame::SetLevelUpMode — __thiscall(int) -> void @0x00628650.
// Tiny setter (function range 0x00628650..0x00628664 = 0x14 bytes).
// Writes its int arg into CGuiInGame.level_up_mode (+0x10C).
// Existing Ghidra comment at 0x0066aa28 ("Sets the in-game
// level_up_mode = 0. Seems to prevent level ups from occurring?")
// confirms the directionality: 0 = block, 1 = allow.
constexpr uintptr_t kAddrCGuiInGameSetLevelUpMode = 0x00628650;

// CGuiInGame.in_game_character — slot @+0x14 per swkotor.exe.h:10225,
// matching the panel-kind classifier in engine_panels.cpp.
constexpr size_t kCGuiInGameCharacterSlotOffset = 0x14;

typedef uint32_t (__thiscall* PFN_ShowLevelUpGUI)(void* this_, int param_1);
typedef void     (__thiscall* PFN_SetLevelUpMode)(void* this_, int mode);

// Read the live CSWGuiInGameCharacter pointer from the CGuiInGame
// singleton. Returns nullptr if the panel hasn't been instantiated (e.g.
// user never opened the Charakterblatt this session).
void* GetInGameCharacterPanel(void* gui) {
    if (!gui) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gui) +
            kCGuiInGameCharacterSlotOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Set CGuiInGame.level_up_mode via the engine's own setter. Wrapping the
// call rather than poking the field directly so future engine-side
// invariants (timer arming, parallel state updates) remain intact —
// SetLevelUpMode is 0x14 bytes today but we treat it as a black box.
bool SetLevelUpMode(void* gui, int mode) {
    if (!gui) return false;
    __try {
        auto fn = reinterpret_cast<PFN_SetLevelUpMode>(
            kAddrCGuiInGameSetLevelUpMode);
        fn(gui, mode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LevelUp", "SetLevelUpMode(%d) faulted gui=%p", mode, gui);
        return false;
    }
}

}  // namespace

bool TriggerLevelUp() {
    void* gui = acc::engine::ResolveGuiInGame();
    if (!gui) {
        acclog::Write("LevelUp", "TriggerLevelUp -- CGuiInGame unresolved");
        return false;
    }

    // Flip level_up_mode → 1 unconditionally. The engine's
    // ShowLevelUpGUI requires it; vanilla code only flips it on
    // engine-internal triggers (cutscenes / module init / scripts), so
    // a player who has accumulated XP in tutorial may stay locked at
    // mode==0 indefinitely. The mode goes back to 0 inside the level-up
    // panel's commit/cancel paths so this latch is one-shot per dispatch.
    if (!SetLevelUpMode(gui, 1)) {
        acclog::Write("LevelUp", "SetLevelUpMode(1) failed; aborting");
        return false;
    }
    acclog::Write("LevelUp", "SetLevelUpMode(1) ok gui=%p", gui);

    // First-choice path: CSWGuiInGameCharacter::ShowLevelUpGUI — the
    // btn_levelup click handler. Calls SetStats + new CSWGuiLevelUpPanel
    // + initializes from this->field_59E4 (CSWCCreature*). Requires the
    // InGameCharacter panel slot to be non-null (panel must have been
    // opened at least once this session). RE'd via runtime peek; see
    // address constant comment above.
    void* charPanel = GetInGameCharacterPanel(gui);
    if (charPanel) {
        __try {
            auto fn = reinterpret_cast<PFN_ShowLevelUpGUI>(
                kAddrCSWGuiInGameCharacterShowLevelUpGUI);
            uint32_t ret = fn(charPanel, 0);
            acclog::Write("LevelUp", "CSWGuiInGameCharacter::ShowLevelUpGUI "
                "dispatched panel=%p ret=0x%08x",
                charPanel, ret);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("LevelUp", "CSWGuiInGameCharacter::ShowLevelUpGUI faulted "
                "panel=%p — falling back to CGuiInGame variant",
                charPanel);
            // fall through
        }
    } else {
        acclog::Write("LevelUp", "in_game_character slot is null "
            "(panel never opened this session) — falling back");
    }

    // Fallback: top-level CGuiInGame::ShowLevelUpGUI. Same internal
    // gates, just dispatched from the singleton instead of the panel —
    // works when the user has never opened the Charakterblatt.
    __try {
        auto fn = reinterpret_cast<PFN_ShowLevelUpGUI>(
            kAddrCGuiInGameShowLevelUpGUI);
        uint32_t ret = fn(gui, 0);
        acclog::Write("LevelUp", "CGuiInGame::ShowLevelUpGUI dispatched gui=%p "
            "ret=0x%08x",
            gui, ret);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LevelUp", "CGuiInGame::ShowLevelUpGUI faulted gui=%p",
                      gui);
        return false;
    }
}

}  // namespace acc::engine_levelup
