// In-memory RIFF/WAVE image builder.
//
// Produces the byte layout the engine's sound code hands to Miles
// (AIL_set_3D_sample_file takes a complete WAV file image in memory). Used
// by LoopSource::StartFromMemory to play audio that has no file behind it —
// rendered speech, synthesised test tones. See
// docs/llm-docs/in-memory-sound-resource.md.

#pragma once

#include <cstddef>
#include <vector>

namespace acc::audio {

// Interleaved float samples (any channel count, [-1, 1]) → a mono 16-bit PCM
// WAV image. Channels are averaged, because the engine refuses a 3D play of
// anything but mono (CExoSoundSourceInternal::Play releases the source
// silently). Out-of-range samples are clamped. False on empty input, zero
// channels or a zero rate; `out` is left empty then.
bool BuildMonoPcm16Wav(const float* samples, size_t frames, size_t channels,
                       size_t sampleRate, std::vector<unsigned char>& out);

// Peak and RMS of a sample block in dBFS (0 = full scale; a silent block
// reads as -inf). For logs: lets voices be compared by numbers, not by ear.
struct Levels {
    float peakDb;
    float rmsDb;
};
Levels MeasureLevels(const std::vector<float>& samples);

// Scales `samples` in place so the loudest sample sits at `targetPeakDb`
// dBFS, giving every voice and line the same headroom before the game's own
// volume chain. A silent block is left untouched. Returns the gain applied
// (1 when untouched).
float NormalizePeak(std::vector<float>& samples, float targetPeakDb);

}  // namespace acc::audio
