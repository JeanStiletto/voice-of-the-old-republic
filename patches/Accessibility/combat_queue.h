// Combat action-queue submenu.
//
// While active:
//   Open        — speaks "Aktionsschlange, N Aktionen" (or "leer").
//   Up/Down     — cycles entries, "Angriff Malak, 1 von 3".
//   Enter       — removes focused entry; re-announces or closes if empty.
//   Shift+Enter — clears all + closes.
//   Esc         — closes.
//
// Self-disarms when controlled creature changes / combat round drops /
// chain faults.
//
// Skeleton: positional remove is unresolved (RemoveLastAction only
// removes the tail). Enter on the tail clears all; Enter on non-tail
// speaks QueueRemoveFailed. See combat-system "open" item for the
// positional-remove primitive search.

#pragma once

namespace acc::combat::queue {

// Returns the player's filtered combat-round queue size (excludes the
// engine's 0xFF "currently dispatching" placeholder). Used by the
// action-bar / target-action announce path to report "Platz N" after
// firing. 0 on no-player / no-combat-round / read fault.
int CountPlayerEntries();

// Snapshot the queue depth RIGHT BEFORE the engine dispatches a press.
// Called from every action-fire site (input_pipeline bare 1..7,
// actionbar_menu Enter, target_action_menu Enter). The post-press
// announce reads it back via GetPrePressDepth() to detect engine
// cap-hits without any string comparison: when CSWSCombatRound.actions
// ->internal->count is already 4 at press time, CSWSCombatRound::
// AddAction silently free's the new action node and returns — so the
// queue depth doesn't grow on a successful press, it stays at 4.
void ReportPrePressDepth();

// Returns the depth stashed by the most recent ReportPrePressDepth()
// call (or -1 if none). The announce site compares this against the
// post-press CountPlayerEntries() result.
int  GetPrePressDepth();

// True iff armed (queue had >= 1 entry — empty queue speaks "leer" and
// returns false).
bool Open();

bool IsActive();

// Press-edge only. Called after actionbar/radial gates.
bool HandleInputEvent(int code, int value);

// Engine owns the queue — no engine-side cleanup needed.
void ForceDisarm(const char* reason);

void Tick();
void PollWin32Hotkey();

}  // namespace acc::combat::queue
