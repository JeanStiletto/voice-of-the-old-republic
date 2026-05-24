// Combat read-side queries — Phases 2A, 2B, 2C.
//
// Layer: read-only stat snapshot helpers + hotkey owners. No engine
// re-entry beyond the documented engine accessors (GetMaxHitPoints,
// GetSTRStat, etc.). Each entry point is self-gating on player-loaded.
//
// Three independent surfaces share this TU because they all read the
// same creature-stats chain (CSWSCreature → creature_stats @+0xa74):
//
//   * Phase 2A — selected-PC full stat block. A configurable hotkey
//     reads HP / FP / AC / 6 attributes / 3 saves / alignment / active
//     effects on the controlled party member and speaks one composed
//     line. Triggered also on party-leader switch (Tab).
//
//   * Phase 2B — opponent cycle-announcement enrichment. When
//     `passive_narrate.cpp` fires for a target that's a Creature kind,
//     it calls into `BuildTargetCombatBrief` here to append HP / AC /
//     faction-relation to the existing name announcement.
//
//   * Phase 2C — Shift+H Examine hotkey. Resolves the current cycle/
//     LastTarget focus, calls `CGuiInGame::ShowExamineBox @0x62d3e0`
//     against it, then reads the populated examine panel's
//     `message_box` and speaks the rendered text. The panel stays open
//     for sighted-player use; we just ensure the text is also spoken.
//
// All three reads use the same `creature_stats @+0xa74` chain. Engine
// accessors are preferred over raw field reads where the address is
// known (HP / AC / Max FP); attribute totals + saves use the inline
// byte fields per the plan's "client-side fields" table since those
// engine accessors weren't separately validated.

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::combat::query {

// Phase 2A — speak a full status snapshot for the controlled player
// creature. No-op when no player is loaded (returns false).
bool SpeakSelectedPcStatBlock();

// Phase 2A trigger: poll the active leader name, fire SpeakSelectedPcStatBlock
// on change. Called from core_tick.cpp; cheap when the leader hasn't
// changed.
void TickLeaderChangeAutoAnnounce();

// Phase 2B — append a localised target combat brief into `outBuf`.
// Caller (passive_narrate.cpp) builds the base name announcement first
// then concatenates this suffix. Returns true if the suffix was written
// (target is a Creature kind and resolved cleanly), false to leave the
// buffer untouched (non-creature target — Door / Item / etc.).
//
// `targetServerObject` is the resolved CSWSObject* the caller already
// holds — we do not re-resolve handles here.
bool BuildTargetCombatBrief(void* targetServerObject,
                            const char* targetName,
                            char* outBuf, size_t outBufSize);

// Phase 2C — Shift+H hotkey. Resolves cycle focus / LastTarget the same
// way `interact_hotkey.cpp` does, calls CGuiInGame::ShowExamineBox, and
// speaks an opener cue. The actual examine-text speech is kicked off by
// a per-tick monitor that diffs the panel's content (since the engine
// populates the panel asynchronously).
void HotkeyShiftH();

// Per-tick monitor for the Examine panel. When the panel becomes
// foreground OR its message_box content changes, speak the rendered
// text. Called from core_tick.cpp.
void TickExaminePanel();

// Phase 2C poll wrapper — checks the Win32 Shift+H key state and fires
// HotkeyShiftH on rising edge with foreground-window + in-world gates.
// Called from interact_hotkey.cpp's PollHotkey alongside the other
// in-world hotkeys.
void PollWin32Hotkey();

// Bare-H self status. Speaks the currently-controlled leader's raw HP,
// active status effects (deduped, named only), and main/off-hand equipped
// weapons. Distance and name are intentionally omitted — distance is
// always zero (you ARE the leader), and the name is redundant when the
// user just wants a quick "where am I health-wise" readout. Gated on
// in-world + no-UI-blocking-panel (same gate Tab leader-announce uses).
void SpeakSelfStatus();

// Bare-H poll wrapper. Called from interact_hotkey.cpp alongside
// PollWin32Hotkey (Shift+H). Self-gates on player-loaded and UI-block.
void PollWin32SelfStatusHotkey();

}  // namespace acc::combat::query
