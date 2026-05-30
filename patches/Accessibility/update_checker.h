// In-game auto-updater. Port of arena's UpdateChecker.cs.
//
// Flow:
//   1. StartBackgroundCheck() kicks a WinHTTP-backed worker thread that
//      GETs the latest release JSON from GitHub. Called once from
//      menus.cpp AnnouncePanelTitle's MainMenu branch — i.e. only after
//      the engine has finished its delicate startup window (intro-movie
//      playback, OpenGL/DirectInput bringup) and the main menu is
//      sighted. Used to fire from OnRulesInit; that competed with Bink
//      playback for COM apartment / message-loop state and is a leading
//      suspect for "menu loaded but unresponsive, alt-tab fixes it"
//      reports.
//   2. Tick() runs per frame from core_tick::Dispatch. Announces the
//      "update available" cue once when the background check completes,
//      and handles download-task completion (announce + spawn batch).
//   3. F5 from the main menu (gated on GetPlayerPosition == false) calls
//      HandleF5(), which either announces the current state or kicks the
//      download task.
//   4. On download success, writes a .bat to %TEMP% that:
//        - waits for swkotor.exe to exit
//        - launches the installer with `--auto-update` (app.manifest
//          handles UAC elevation)
//        - relaunches the game via Steam URL
//      …then calls ExitProcess so the game closes and the bat takes over.
//
// Architectural mirror of arena's UpdateChecker.cs — see
// arena/src/Core/Services/UpdateChecker.cs and AccessibleArenaMod.cs for
// the reference implementation. Behaviour-equivalent state machine,
// adapted to our installer-based install (vs. arena's single-DLL swap).

#pragma once

namespace acc::update_checker {

// Kick off the background version check. Idempotent — repeat calls are
// dropped while a check is in flight or has completed.
void StartBackgroundCheck();

// Per-frame poll. Announces check-complete + drives the download task's
// completion handling. Cheap to call when idle.
void Tick();

// F5 handler. Speaks the appropriate cue based on internal state:
//   - download in flight → "Update wird heruntergeladen."
//   - no update available → "Kein Update verfügbar. Aktuelle Version X."
//   - update available → kicks the download task.
// Gated by the caller on GetPlayerPosition == false (main menu / loading
// screens only). The caller is responsible for refusing the keypress
// during active gameplay and announcing UpdateNotInMenu.
void HandleF5();

}  // namespace acc::update_checker
