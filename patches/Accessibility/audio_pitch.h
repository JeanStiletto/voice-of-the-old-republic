// Pitch-variance neutraliser — handler declaration + per-call gate.
//
// Public surface:
//   * BeginScopedZero / EndScopedZero — RAII-style toggle wrapped around
//     the engine's Play3DOneShotSound / PlayOneShotSound calls in
//     audio_bus.cpp. While the flag is set, the detour at
//     CExoSoundSourceInternal::CalculatePitchVarianceFrequency replaces
//     the engine's randomised frequency with the source's
//     base_frequency. While clear, the detour falls through to the
//     original engine code so non-cue sounds keep their jitter.
//   * OnCalculatePitchVarianceFrequency — the framework-called handler
//     symbol; reads the flag to decide whether to consume or fall
//     through.
//
// Why a thread_local flag instead of a global hook or resref-matching:
//   * Global zero-variance affects every engine sound (footsteps, sword
//     swings, GUI clicks, dialogue) — the user wants surgical scope.
//   * Resref-matching catches engine-driven plays of the same gui_*
//     resrefs (Inventory_Add, Quest_New_Notify, ...) too, which still
//     affects the rest of the game.
//   * The flag is set only across our specific Play*OneShotSound call,
//     so only the source created during that call gets zero variance.
//     thread_local guards against the audio thread running
//     CalculatePitchVarianceFrequency mid-playback (chorus effect on
//     long sounds) seeing our flag from a different thread's
//     PlayCue3D body.

#pragma once

namespace acc::audio::pitch {

// Begin/end a scoped window where any source the engine creates
// synchronously on this thread should have its pitch variance
// neutralised. Pair the calls 1:1 — Begin before the engine call,
// End immediately after, even on failure paths. Nestable (use a
// counter if you ever fold this into helper code that nests).
void BeginScopedZero();
void EndScopedZero();

// Test the per-thread flag from the detour handler. Returns true when
// the calling thread is currently inside a BeginScopedZero / EndScopedZero
// window — i.e., the engine is computing variance for a source we just
// asked it to create.
bool IsScopedZeroActive();

}  // namespace acc::audio::pitch

extern "C" int __cdecl OnCalculatePitchVarianceFrequency(void* source);
