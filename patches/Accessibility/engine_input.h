// Engine input-code translation. Pure read-side; no engine state access.

#pragma once

namespace acc::engine {

// Human-readable name. "?" for unknown, "INPUTDEVICE_NONE" for -1.
const char* InputIndexName(int code);

// Translates the four KOTOR logical-action codes
// CSWGuiManager::HandleInputEvent rewrites before per-panel dispatch.
// Unknown codes pass through.
int ManagerTranslateCode(int code);

// Cold-start DirectInput wake-up. The engine acquires its keyboard/mouse
// DirectInput devices only on a CExoInput::SetActive(1) transition, which it
// drives from game-state events (HideLoadScreen, end-of-movie, dialog/pause
// resume) rather than purely from the window's WM_ACTIVATE. On a cold launch
// the final render window is created without that activation ever reaching
// the live input object, so CExoRawInputInternal::active stays 0 — and
// GetKeyboardBuffer returns zero input while active==0, leaving the menu
// keyboard-dead until the user alt-tabs (which forces a real
// SetActive(0)->SetActive(1) cycle). This replicates the activate half
// exactly: read the engine's own global ExoInput pointer and call
// CExoInput::SetActive(ExoInput, 1) — byte-for-byte what HideLoadScreen does.
// Idempotent by engine design (the inner SetActive guards on a state change),
// so a redundant call when already active is a harmless no-op. Returns true
// if the call was dispatched (ExoInput non-null), false otherwise.
bool EnsureInputAcquired();

// Force a real SetActive(0)->SetActive(1) edge to re-Acquire the DirectInput
// devices unconditionally — the software equivalent of an alt-tab.
//
// Needed for the post-load input-death case: when keys are pressed during a
// load (while the engine destroys+recreates its render window), the input
// `active` flag can desync to 1 while the DirectInput keyboard is actually
// unacquired. Because the engine only re-Acquires on a SetActive state-change
// EDGE, its own post-load SetActive(1) (and our EnsureInputAcquired) no-op
// against the stuck flag — leaving the keyboard dead until the user alt-tabs.
// Cycling 1->0->1 guarantees the 0->1 edge fires, re-Acquiring regardless of
// the prior flag state. SetActive only touches input-device acquisition (not
// pause / audio / mouse-capture), so the cycle is side-effect-free beyond a
// sub-frame device re-grab — exactly what the engine itself does on alt-tab.
// Returns true if dispatched (ExoInput non-null), false otherwise.
bool ForceReacquireInput();

// Release the engine's DirectInput grab via SetActive(0) — the deactivate
// half. KOTOR acquires its keyboard at BACKGROUND cooperative level, so once
// acquired it keeps reading the device even when another window is foreground
// (harmless fullscreen; how the game shipped). Windowed, that bleeds menu/nav
// keys into the game while the user is in another window — e.g. a screen-reader
// user navigating their reader's window also drives the game's menu. Calling
// this on focus-loss makes input mirror the foreground: the game holds the
// keyboard only while it owns the foreground; ForceReacquireInput restores it
// on the regain edge. Returns true if dispatched (ExoInput non-null).
bool ReleaseInput();

// Deferred-reacquire request, for focus/window events that arrive on the
// engine's message-pump thread but at a fragile point (inside the engine's
// own WM_ACTIVATEAPP / window-recreation handling). Rather than calling
// ForceReacquireInput re-entrantly from the wndproc, the focus subclass sets
// this flag and the next Dispatch() drains it — so the SetActive(0)->(1)
// cycle lands on a clean tick, after the engine has finished rebuilding the
// render window and re-binding DirectInput to the new HWND.
//
// Motivating case: a misbehaving external process repeatedly steals the
// foreground while KOTOR runs windowed (FullScreen=0). Each focus regain
// makes the engine tear down and recreate its render window; the input
// `active` flag desyncs to a stuck 1 and the keyboard goes dead — including
// in menus, so even the "Do you really want to quit?" dialog stops
// responding. The area-change reacquire in transitions::Tick never fires
// (no module load), so this focus-driven path is the only recovery.
// RequestInputReacquire is set-only and coalescing (multiple requests before
// one drain collapse to a single cycle); safe to call from any thread.
void RequestInputReacquire();

// Counterpart for focus-LOSS: request that the next drain releases the engine's
// DirectInput grab (ReleaseInput). Set from the focus subclass on
// WM_ACTIVATEAPP active=0 so input mirrors foreground — without it the game
// keeps the (background-level) keyboard acquired and nav keys bleed in while
// the user is in another window. Set-only and coalescing; last of
// Reacquire/Release before a drain wins, so the final settled foreground state
// is what gets applied. Safe to call from any thread.
void RequestInputRelease();

// Drains a pending input-acquisition change (no-op if none). Call once per tick
// from the engine thread: runs ForceReacquireInput on a pending focus-gain, or
// ReleaseInput on a pending focus-loss.
void DrainPendingReacquire();

}  // namespace acc::engine

// Pre-translation codes received by CSWGuiManager::HandleInputEvent.
// File-scope for callsite brevity in the menu hooks.
constexpr int kInputNavUp    = 0xb6;
constexpr int kInputNavDown  = 0xb7;
constexpr int kInputNavLeft  = 0xb8;
constexpr int kInputNavRight = 0xb9;
constexpr int kInputEnter1   = 0xb5;
constexpr int kInputEnter2   = 0xbb;
constexpr int kInputEsc1     = 0xb4;
constexpr int kInputEsc2     = 0xdf;
constexpr int kInputActivate = 0x27;   // KEYBOARD_F1, engine's activate code

// "Default action on current target" — the command keymap.2da row
// `DefaultAction` (default key R) emits. CClientExoAppInternal::HandleInputEvent
// case 0xef: GetDefaultActions on last_target, then calls the descriptor's
// action function (talk/open/bash/use…). The keyboard twin of Mouse 1.
constexpr int kInputDefaultAction = 0xef;

// Raw InputIndices — engine has no logical-action translation for these.
// Stock kotor.ini has no [Keymapping] for scancodes 32/33, so they arrive
// bare at the manager hook.
constexpr int kInputHome     = 32;
constexpr int kInputEnd      = 33;

// Internal routing codes (NOT engine InputIndices) — interact_hotkey
// synthesises these for the unified action menu when Ctrl is held with
// Home/End, to jump to the first / last category. Picked well outside the
// engine's small InputIndex range so they can't collide with a real event.
constexpr int kInputCatFirst = 0x201;  // Ctrl+Home → first category
constexpr int kInputCatLast  = 0x202;  // Ctrl+End  → last  category

// Raw InputIndices for unmapped Pillar 4 cycle keys.
// kInputKbAnnounce = physical key right of `.` (KEYBOARD_SLASH = 105) —
// labelled `-` on QWERTZ, `/` on QWERTY, same physical position. Keeps
// `,` `.` `-` contiguous on QWERTZ (`,` `.` `/` on QWERTY).
constexpr int kInputKbLeftShift  = 24;
constexpr int kInputKbRightShift = 25;
constexpr int kInputKbComma      = 103;
constexpr int kInputKbPeriod     = 104;
constexpr int kInputKbAnnounce   = 105;
