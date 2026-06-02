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

    // Live playback-pitch control for a playing 3D source. `multiplier` is
    // relative to the sample's natural rate: 1.0 = unchanged, 2.0 = +1 octave,
    // 0.5 = -1 octave (clamped internally). Safe no-op until the engine has
    // created the 3D voice (a tick or two after Start) and on 2D sources.
    //
    // Replicates the engine's own live-pitch path (CExoSoundSourceInternal::
    // SetPitchVariance pushes a new rate to the Miles voice via
    // AIL_set_3D_sample_playback_rate) but with an ABSOLUTE rate instead of the
    // engine's random variance — used by the turret elevation cue, where pitch
    // encodes the vertical aim error.
    void SetPitchMultiplier(float multiplier);

    bool IsActive() const { return source_ != nullptr; }

private:
    void* source_  = nullptr;  // CExoSoundSource* (opaque)
    int   base_hz_ = 0;        // sample's natural playback rate, cached at Start
};

}  // namespace acc::audio
