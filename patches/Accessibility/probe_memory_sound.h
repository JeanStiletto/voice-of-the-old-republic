// In-memory sound probe — positional-speech groundwork (KOTOR 1 only).
//
// Proves that the engine plays a WAV that has no file behind it through its
// own 3D path (LoopSource::StartFromMemory; mechanism in
// docs/llm-docs/in-memory-sound-resource.md), first with a synthesised tone,
// then with speech rendered to memory.
//
// F11:       a 3 s pulsed 440 Hz tone, 3 m to the LEFT of the current view.
//            Turn the camera while it plays: the pulses should swing across.
//            Alternates two resrefs per press; mash it to stress the
//            create / free cycle (each press frees the previous sound).
// Shift+F11: the behind-you cue A/B. "Eins .. Vier. Ausgang Nord-West" in
//            the exit lines' voice, 5 m straight behind the listener, one
//            after another, each with a different rear shelf (the table in
//            probe_memory_sound.cpp: shipped 3 kHz -10 dB, then 1.5 kHz
//            -10 dB, 1.5 kHz -14 dB, 1 kHz -18 dB).
//            (The Eloquence / SAPI / OneCore phrase cycle passed on
//            2026-10-27 and was retired.)
//
// Every press logs under "ProbeMemSound" and "LoopSource" (3D voice attached,
// resource state), and logs the state again once the sound should have
// finished. A failed press says "memory probe failed" through the screen
// reader; a successful one is silent apart from the sound itself.
//
// Developer-facing: the phrases are deliberate literals (probe_* convention),
// not strings::Get() entries.

#pragma once

namespace acc::probe_memory_sound {

// Per-tick poll. Self-gates on a loaded player.
void PollWin32();

}  // namespace acc::probe_memory_sound
