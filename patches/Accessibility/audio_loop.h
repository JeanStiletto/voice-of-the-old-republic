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
    //
    // looping/spatial default to the sustained-3D behaviour every existing
    // caller relies on. Pass looping=false for a one-shot, spatial=false for
    // a centred 2D play (no listener bias, no 3D position).
    //
    // priorityGroup < 0 (default) leaves the source in the engine ctor's
    // group (0x17). Pass an explicit group to override — the audio glossary
    // passes 0xb (the GUI-click group) so its audition survives the in-game
    // menu's SetSoundMode(4) pause, which mutes group 0x17 but exempts 0xb.
    //
    // volumeByte < 0 (default) leaves the engine ctor's full per-source
    // volume (0x7f). Pass 0..127 to set the per-source level — used by the
    // cue-volume slider preview so the audition plays at the level a real
    // cue would. (Engine clamps >127 to 127; the byte is one factor in
    // group_volume × source_volume × master_SFX_slider.)
    bool Start(const char* resref, const Vector& worldPosition,
               bool looping = true, bool spatial = true, int priorityGroup = -1,
               int volumeByte = -1);

    void UpdatePosition(const Vector& worldPosition);  // safe no-op if inactive
    void Stop();                                       // idempotent

    bool IsActive() const { return source_ != nullptr; }

private:
    void* source_ = nullptr;  // CExoSoundSource* (opaque)
};

}  // namespace acc::audio
