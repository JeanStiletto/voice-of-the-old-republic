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
