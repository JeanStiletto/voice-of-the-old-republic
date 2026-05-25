// Engine-managed looping/3D source — RAII wrapper around CExoSoundSource.
//
// Layer: audio/ (sibling of audio_bus). Use for sustained spatial audio
// where the one-shot PlayCue3D model doesn't fit: continuous tones,
// position-updated sources, anything that needs an explicit Stop().
//
// First consumer: swoop_race.cpp wall scrape — a continuous metal-strain
// tone while the bike is pinned, replacing the 2 s re-fire heartbeat.
//
// Lifecycle (see project_cexosoundsource_loop_api.md memory + audio_bus.h
// kAddrCExoSoundSource* constants for the underlying engine API):
//   1. Default-construct LoopSource — no engine call yet.
//   2. Start(resref, pos) — alloc + ctor + configure + Play.
//   3. UpdatePosition(pos) per tick if the source moves relative to
//      the listener.
//   4. Stop() — Stop + dtor + free. Idempotent.
//   5. Destructor auto-Stops at DLL unload (or any other RAII exit).
//
// Threading: single-threaded under the engine OnUpdate tick. No internal
// synchronisation.

#pragma once

#include "engine_offsets.h"  // Vector

namespace acc::audio {

class LoopSource {
public:
    LoopSource() = default;
    ~LoopSource();

    LoopSource(const LoopSource&)            = delete;
    LoopSource& operator=(const LoopSource&) = delete;

    // Allocate, configure, and start playing a looping 3D source at
    // worldPosition. Replaces any currently active source on this
    // handle. Returns true if the engine accepted every call in the
    // chain; false on resref/alloc/engine failure (handle stays
    // inactive).
    bool Start(const char* resref, const Vector& worldPosition);

    // Update the source position. Safe no-op if not active.
    void UpdatePosition(const Vector& worldPosition);

    // Stop playback, destruct, and free. Idempotent.
    void Stop();

    bool IsActive() const { return source_ != nullptr; }

private:
    void* source_ = nullptr;  // CExoSoundSource* (opaque to consumers)
};

}  // namespace acc::audio
