#include "engine_levelup.h"

#include <windows.h>
#include <cstdint>

#include "engine_panels.h"    // ResolveGuiInGame, HasActiveLevelUpPanel
#include "engine_player.h"    // GetClientLeader, kClientObjectServerObjectOffset
#include "engine_offsets.h"   // kCreatureStatsPointerOffset
#include "engine_subscreen.h" // Begin/EndOverlayPause — freeze the world like
                              // the action menu, WITHOUT opening a sub-screen
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

// CSWSCreatureStats::CanLevelUp — undefined4 __thiscall(void) @0x005a6810.
// Pure read-only predicate (RE'd by byte dump 2026-06-16): returns 1 only
// when the leader's current level is below the level cap, accumulated
// experience >= required_exp_per_level[level] (rules table at 0x007a3a28),
// AND the two class-side gates pass — i.e. exactly when the Charakterblatt
// btn_levelup button is enabled. No writes, no allocations, so it's safe
// to call purely as a gate. ECX = CSWSCreatureStats*, no stack params.
constexpr uintptr_t kAddrCSWSCreatureStatsCanLevelUp = 0x005a6810;

typedef uint32_t (__thiscall* PFN_CanLevelUp)(void* this_);

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

// CGuiInGame::SetSWGuiStatus — __thiscall(int status, int p2) -> void
// @0x0062aa00. Decompiled: a small state setter. status 3 = a sub-screen owns
// input; 4 = sub-screen finishing (restores status to 1). After the switch it
// only adds/removes the main-interface HUD panel and destroys a leftover pause
// panel — no scripts, no input-class change, no character panel.
//
// This is the ONE thing the wizard needs and the ONLY thing we take from the
// full sub-screen open: with status==1 (in-world) the engine routes keyboard to
// the world, so the wizard sits foreground on the modal stack but receives
// nothing until some transition sets status=3 (the "frozen until you press
// Escape once" symptom — Escape opened a real sub-screen, which set it).
// ShowSWInGameGui also sets status=3, but it ADDITIONALLY adds the Character
// panel (screen 2), whose input handler re-codes the wizard's nav keys
// (181/182/183 → 208/214/221) and breaks our forwarding. Driving the status bit
// alone keeps the in-world key codes — confirmed by the F2-opened menu strip in
// patch-20260723-161404.log, which set status=3 with NO character panel and the
// wizard then navigated with 181/182/183.
constexpr uintptr_t kAddrCGuiInGameSetSWGuiStatus = 0x0062aa00;
typedef void (__thiscall* PFN_SetSWGuiStatus)(void* this_, int status, int p2);

void DriveSWGuiStatus(void* gui, int status, int p2) {
    if (!gui) return;
    __try {
        reinterpret_cast<PFN_SetSWGuiStatus>(kAddrCGuiInGameSetSWGuiStatus)(
            gui, status, p2);
        acclog::Write("LevelUp", "DriveSWGuiStatus(%d,%d) gui=%p",
                      status, p2, gui);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LevelUp", "DriveSWGuiStatus(%d) faulted gui=%p",
                      status, gui);
    }
}

// Wizard-open bookkeeping. Two engine touches, no sub-screen open:
//   1. sw_gui_status = 3 → the engine routes keyboard to the wizard modal
//      (fixes "frozen until Escape") while keeping the in-world key codes.
//   2. BeginOverlayPause → freeze the world the same way the action menu does
//      (no movement leak). status=3 alone does NOT pause the simulation.
// TickLevelUpPause reverses both once the wizard closes.
bool s_pauseHeld = false;   // open state outstanding (status driven + pause held)
bool s_sawPanel  = false;   // wizard panel observed live at least once — guards
                            // against releasing in the frame before the panel
                            // registers

void NoteLevelUpOpened(void* gui) {
    if (s_pauseHeld) return;  // idempotent per open
    // Replicate the THREE things a real sub-screen open establishes, minus the
    // Character panel that re-routes nothing useful for us:
    //   1. input_class = 2 (menu/GUI) → physical keys translate to the manager's
    //      nav codes (181-185); without it they stay world-coded (208/214/221)
    //      and the wizard is unreachable (the "frozen until Escape" limbo).
    //   2. sw_gui_status = 3 → the client routes input to the GUI manager, which
    //      dispatches to the top modal (our wizard).
    //   3. BeginOverlayPause → freeze the world (status/class don't pause sim).
    acc::engine::SetGuiInputClass(2);
    DriveSWGuiStatus(gui, 3, 1);
    acc::engine::BeginOverlayPause(acc::engine::OverlayPauseOwner::LevelUp);
    s_pauseHeld = true;
    s_sawPanel  = false;
    acclog::Write("LevelUp", "wizard opened: input_class=2 + sw_gui_status=3 + "
        "overlay pause (input routed to wizard, world frozen)");
}

}  // namespace

bool PlayerCanLevelUp() {
    void* clientLeader = acc::engine::GetClientLeader();
    if (!clientLeader) {
        acclog::Write("LevelUp", "PlayerCanLevelUp -- no client leader");
        return false;
    }
    // Leader's *server* creature owns the authoritative stats (same chain
    // engine_player.cpp uses): client +0xf8 -> CSWSCreature, +0xa74 ->
    // CSWSCreatureStats. CanLevelUp is a server-side predicate.
    __try {
        void* serverCreature = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientLeader) +
            kClientObjectServerObjectOffset);
        if (!serverCreature) return false;
        void* stats = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverCreature) +
            kCreatureStatsPointerOffset);
        if (!stats) return false;
        auto fn = reinterpret_cast<PFN_CanLevelUp>(
            kAddrCSWSCreatureStatsCanLevelUp);
        return fn(stats) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LevelUp", "PlayerCanLevelUp -- SEH fault resolving stats");
        return false;
    }
}

bool TriggerLevelUp() {
    // Root-cause guard: ShowLevelUpGUI only gates on level_up_mode, which
    // we force to 1 below, so the engine's natural XP check never runs.
    // Refuse here when the leader hasn't earned the level — otherwise the
    // wizard re-opens indefinitely (known-issues.md endless-level-up).
    if (!PlayerCanLevelUp()) {
        acclog::Write("LevelUp",
            "TriggerLevelUp -- CanLevelUp=0, refusing (capped / not enough XP)");
        return false;
    }

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
            NoteLevelUpOpened(gui);
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
        NoteLevelUpOpened(gui);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("LevelUp", "CGuiInGame::ShowLevelUpGUI faulted gui=%p",
                      gui);
        return false;
    }
}

void TickLevelUpPause() {
    if (!s_pauseHeld) return;
    if (acc::engine::HasActiveLevelUpPanel()) {
        s_sawPanel = true;   // wizard is live — keep the world frozen
        return;
    }
    if (!s_sawPanel) return; // just opened; panel not registered yet — wait
                             // until we've seen it before releasing the pause
    // Reverse the three open touches, in reverse order. status 4 =
    // "sub-screen finishing": the decompile restores sw_gui_status to 1 (unless
    // it was 2) and re-adds the HUD panel. input_class 0 = back to in-world key
    // routing (same call the in-game menus use to close, SetInputClass(0,1)).
    // Then release the world pause.
    DriveSWGuiStatus(acc::engine::ResolveGuiInGame(), 4, 1);
    acc::engine::SetGuiInputClass(0);
    acc::engine::EndOverlayPause(acc::engine::OverlayPauseOwner::LevelUp);
    s_pauseHeld = false;
    acclog::Write("LevelUp",
        "wizard closed — input_class/status restored + overlay pause released");
}

}  // namespace acc::engine_levelup
