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
// Shift+F11: the next test phrase, rendered by the user's NVDA Eloquence
//            voice and settings (eloquence_render.h) and played 3 m away in
//            the direction the phrase names — left, right, ahead, behind, in
//            turn. (SAPI and OneCore passed on 2026-10-27 and were retired
//            from the cycle; SAPI2SR renders nothing, as expected.)
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
