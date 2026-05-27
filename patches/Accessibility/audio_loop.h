// Engine-managed looping/3D source — RAII over CExoSoundSource.
//
// Use for sustained spatial audio where PlayCue3D's one-shot model
// doesn't fit (continuous tones, position-updated sources, anything
// needing explicit Stop).
//
// Lifecycle: Start → optional UpdatePosition per tick → Stop. Destructor
// auto-Stops. Single-threaded under OnUpdate.

#pragma once

#include "engine_offsets.h"

namespace acc::audio {

class LoopSource {
public:
    LoopSource() = default;
    ~LoopSource();

    LoopSource(const LoopSource&)            = delete;
    LoopSource& operator=(const LoopSource&) = delete;

    // True iff every engine call succeeded; false leaves the handle inactive.
    bool Start(const char* resref, const Vector& worldPosition);

    void UpdatePosition(const Vector& worldPosition);  // safe no-op if inactive
    void Stop();                                       // idempotent

    bool IsActive() const { return source_ != nullptr; }

private:
    void* source_ = nullptr;  // CExoSoundSource* (opaque)
};

}  // namespace acc::audio
