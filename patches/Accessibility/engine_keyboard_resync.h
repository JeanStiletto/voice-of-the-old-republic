// Keyboard transition-stream re-synchronisation.
//
// The engine reads its keyboard as a stream of DirectInput transitions and
// derives every "is this key down" flag from that stream alone. It has several
// places where it discards queued transitions without re-reading the device
// (every SetActive 0->1 edge drains the buffer; a lost or unacquired device
// answers an empty batch), and nothing ever re-derives the true state
// afterwards — a discarded release leaves that key held in the engine until
// the player presses and releases it again. Symptoms: a camera that spins
// after a dialogue or popup, a party that walks off on its own.
//
// This wrapper sits on the one call through which the per-frame event fetch
// pulls the keyboard batch, and after every fetch compares what the engine
// has been told (a shadow of every transition it has received) with the true
// level state DirectInput reports. When the engine believes a key is down that
// DirectInput says is up, and no transition for that key has arrived for two
// consecutive frames, the missing release is appended to the batch so the
// engine's own consumer processes it. Presses are never synthesised: a missed
// press is the safe failure. In a normal frame the two views agree and the
// batch passes through untouched.
//
// See gui-and-input-internals.md, "keyboard_transition_stream", for the
// decompile this rests on. Install: KOTOR 2 from DllMain (unpacked image),
// KOTOR 1 from EnsureSessionBringup (packed .text — same rule as the mouse
// guard). Idempotent.
#pragma once

namespace acc::engine {

void InstallKeyboardStreamResync();

}  // namespace acc::engine
