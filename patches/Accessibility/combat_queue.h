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

// Arm the user-attribution latch: "a user press is dispatching a queue add
// right now." Called from the two user-initiated queue sites — the bare-key
// dispatch (input_pipeline) and the unified-menu Enter. The AddAction detour
// only announces adds that land within a short freshness window of an arm,
// so the engine's own auto-queued leader attacks (which fire on the combat-
// round cadence, with no preceding press) stay silent. Refresh-on-arm (no
// consume) so a rapid burst of presses each announce.
void ArmUserQueueAdd();

// The other half of the same arm: a user press that produces NO queue add.
//
// KOTOR 1 refuses an unusable action at the GUI layer — DoPersonalAction sees
// the entry's usable bit clear, plays its error sound and never dispatches, and
// the unified menu speaks the engine's own reason for it ("Volle Gesundheit").
// KOTOR 2 does not: the same medkit at full health passes that test, the
// handler runs, and it silently does nothing — no queue add, no feedback line,
// no sound. The press is simply swallowed.
//
// So the absence of an add IS the signal. ArmUserQueueAdd starts this watch;
// an attributed add cancels it; if the attribution window closes with nothing
// added, the fire did nothing and TickNoOpWatch says so.
//
// Cancel it explicitly when the caller has already explained the outcome —
// otherwise KOTOR 1's spoken refusal reason would be followed by the generic
// line saying the same thing twice.
void CancelNoOpWatch();

// Per-tick verdict for the watch above. Call once per frame, after the input
// polls, so an add triggered by this tick's dispatch is still counted.
void TickNoOpWatch();

// Announce a queued action the engine just accepted (or rejected at the
// cap) — the authoritative "X, Platz N" / "Warteschlange voll" cue. Called
// from the CSWSCombatRound::AddAction detour, which fires once per genuine
// add at function entry (pre-insert), so the round's current count is the
// pre-add depth: 0..3 land at slot count+1, >=4 is rejected ("voll"). This
// replaces the old rising-edge poll announce, which under-counted on key
// auto-repeat and raced the queue drain. Only the controlled creature's
// round announces, and only when attributable to a user press (see
// ArmUserQueueAdd) — engine auto-attacks are ignored.
void OnEngineActionAdded(void* combatRound, void* action);

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
