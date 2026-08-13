// Mod-wide per-tick dispatcher.
//
// Owns the OnUpdate hook (CSWGuiManager::Update @ 0x0040ce76). Fans out
// to each subsystem in a fixed order — explicit calls in Dispatch() so
// the file reads as the canonical "what fires per tick" list.

#pragma once

#include <windows.h>

namespace acc::tick {

void Dispatch();

// Timestamp of the tick currently being dispatched, in milliseconds, wrapping
// at 2^32 exactly like GetTickCount — so it drops into the existing
// `now - then >= window` idiom unchanged.
//
// Two reasons to prefer it over GetTickCount inside the fan-out:
//
//  * Resolution. It is derived from QueryPerformanceCounter (the read the
//    watchdog already performs at the top of every Dispatch, so this costs
//    nothing extra). GetTickCount resolves to ~15.6 ms, and a tick is one
//    rendered FRAME — ~16.7 ms at 60 Hz vsync, ~6.9 ms at 144 Hz, less with
//    vsync off. Any code that DIVIDES by an inter-tick interval therefore
//    needs this: with the coarse clock, consecutive ticks land in the same
//    quantum and the interval reads as zero, which does not degrade a rate,
//    it falsifies it (camera_announce reported 0°/s mid-turn and fired three
//    spurious stop cues in one second — patch-20260813-200714).
//  * Agreement. Sampled once, before any subsystem runs, so the whole tick
//    shares one "now" instead of each module reading the clock a moment apart.
//
// Code that only asks "have N hundred ms passed" is fine on GetTickCount and
// need not migrate.
//
// CONSTRAINT: this is the CURRENT TICK's timestamp, not a live clock. It is
// only valid inside the Dispatch fan-out. Code reached from an input hook, a
// menu callback or a hotkey outside the tick will read a stale value — call
// GetTickCount there.
DWORD NowMs();

}  // namespace acc::tick
