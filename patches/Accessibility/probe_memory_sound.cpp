#include "probe_memory_sound.h"

#include <windows.h>
#include <cmath>
#include <vector>

#include "audio_bus.h"
#include "audio_loop.h"
#include "audio_wav.h"
#include "engine_player.h"
#include "hotkeys.h"
#include "log.h"
#include "prism.h"
#include "view_mode.h"

namespace acc::probe_memory_sound {

namespace {

constexpr float kPi             = 3.14159265358979f;
constexpr float kProbeDistance  = 3.0f;
// Logged once the sound should be over, to see the engine release the
// demand and the source still holding its (now idle) resource.
constexpr DWORD kAfterPlayMarginMs = 500;

// Tone: 3 s of 200 ms 440 Hz pulses with 100 ms gaps — pulses are easier to
// place by ear than a steady sine, and 3 s leaves time to turn the camera.
constexpr size_t kToneRate      = 22050;
constexpr float  kToneHz        = 440.0f;
constexpr float  kToneSeconds   = 3.0f;
constexpr float  kTonePulseOn   = 0.2f;
constexpr float  kTonePulseCycle = 0.3f;
constexpr float  kToneFade      = 0.005f;   // click-free pulse edges
constexpr float  kToneAmplitude = 0.5f;

// Rendered speech is peak-normalised to this before the voice's own volume
// is applied as the per-source game volume (0..127, 127 = engine full).
constexpr float kSpeechPeakDb     = -1.0f;
constexpr float kFullSourceVolume = 127.0f;

// Two resrefs alternate so consecutive presses also exercise "new name,
// fresh CResWave" against "same name as two presses ago".
const char* const kResrefs[2] = {"acc_memtest_a", "acc_memtest_b"};

// Behind-you cue A/B (2026-10-28). An exit-style line once per variant,
// straight behind at positional speech's placement (kPlaceRadiusM from the
// listener), in the voice exit lines use, differing only in the rear
// high-shelf cut. Round 1 (none / 3 kHz -10 / 1 kHz -18): the shipped cut too
// subtle, 1 kHz -18 "really strongly muffled ... very deep". Round 2 moves
// one knob at a time between them. The Eloquence phrase cycle that used to
// live here passed on 2026-10-27 and was retired.
constexpr float kRearProbeRadiusM = 5.0f;
constexpr DWORD kRearProbeGapMs   = 700;
struct RearVariant {
    const char* text;      // ASCII only: rendered by a German voice
    float       cornerHz;  // high-shelf corner
    float       cutDb;     // 0 = untouched
};
const RearVariant kRearVariants[] = {
    {"Eins. Ausgang Nord-West.", 3000.0f, -10.0f},  // shipped (Dark Vision)
    {"Zwei. Ausgang Nord-West.", 1500.0f, -10.0f},  // lower corner, same depth
    {"Drei. Ausgang Nord-West.", 1500.0f, -14.0f},  // lower corner, deeper
    {"Vier. Ausgang Nord-West.", 1000.0f, -18.0f},  // round 1's planned
};
constexpr int kRearVariantCount = sizeof(kRearVariants) / sizeof(kRearVariants[0]);

acc::audio::LoopSource g_source;
int   g_presses     = 0;
int   g_rearNext    = kRearVariantCount;  // next variant to play; count = idle
DWORD g_rearAt      = 0;                  // GetTickCount when it starts
DWORD g_checkAt     = 0;   // GetTickCount deadline for the after-play log
bool  g_checkArmed  = false;

std::vector<unsigned char> BuildToneWav() {
    const size_t frames = static_cast<size_t>(kToneSeconds * kToneRate);
    std::vector<float> samples(frames);
    for (size_t i = 0; i < frames; ++i) {
        const float t     = static_cast<float>(i) / kToneRate;
        const float phase = std::fmod(t, kTonePulseCycle);
        float env = 0.0f;
        if (phase < kTonePulseOn) {
            env = 1.0f;
            if (phase < kToneFade)                 env = phase / kToneFade;
            if (kTonePulseOn - phase < kToneFade)  env = (kTonePulseOn - phase) / kToneFade;
        }
        samples[i] = kToneAmplitude * env * std::sin(2.0f * kPi * kToneHz * t);
    }
    std::vector<unsigned char> wav;
    acc::audio::BuildMonoPcm16Wav(samples.data(), frames, 1, kToneRate, wav);
    return wav;
}

// World position kProbeDistance metres from the viewpoint, yawOffsetDeg off
// the view direction (engine yaw: 0 = +X, CCW positive, so +90 = left).
bool PositionAround(float yawOffsetDeg, Vector& out) {
    Vector origin;
    float  yawDeg = 0.0f;
    if (!acc::view_mode::GetEffectiveViewpoint(origin, yawDeg)) return false;
    const float rad = (yawDeg + yawOffsetDeg) * (kPi / 180.0f);
    out = Vector{origin.x + kProbeDistance * std::cos(rad),
                 origin.y + kProbeDistance * std::sin(rad),
                 origin.z};
    return true;
}

// Straight behind the engine's listener (the camera, or the view-mode
// cursor), where positional speech puts a line about something behind.
bool PositionBehindListener(Vector& out) {
    Vector origin;
    float  yawDeg = 0.0f;
    if (!acc::view_mode::GetEffectiveViewpoint(origin, yawDeg)) return false;
    if (!acc::view_mode::IsActive()) acc::engine::GetCameraPosition(origin);
    const float rad = (yawDeg + 180.0f) * (kPi / 180.0f);
    out = Vector{origin.x + kRearProbeRadiusM * std::cos(rad),
                 origin.y + kRearProbeRadiusM * std::sin(rad),
                 origin.z};
    return true;
}

// volumeByte: per-source game volume 0..127; < 0 leaves the engine's full 127.
// False when nothing started.
bool PlayAt(const char* what, const std::vector<unsigned char>& wav, float durationSec,
            const Vector& pos, float yawOffsetDeg, int volumeByte = -1) {
    const char* resref = kResrefs[g_presses % 2];
    ++g_presses;
    const bool ok = !wav.empty() &&
        g_source.StartFromMemory(resref, wav.data(), wav.size(), pos,
                                 acc::audio::GetCuePriorityGroup(), volumeByte);
    acclog::Write("ProbeMemSound",
                  "press %d: %s resref=%s bytes=%zu dur=%.2fs yaw_off=%.0f pos=(%.2f,%.2f,%.2f) ok=%d",
                  g_presses, what, resref, wav.size(), durationSec, yawOffsetDeg,
                  pos.x, pos.y, pos.z, ok ? 1 : 0);
    if (!ok) {
        prism::Speak("memory probe failed", /*interrupt=*/true);
        g_checkArmed = false;
        return false;
    }
    g_checkAt    = GetTickCount() + static_cast<DWORD>(durationSec * 1000.0f) + kAfterPlayMarginMs;
    g_checkArmed = true;
    return true;
}

void Play(const char* what, const std::vector<unsigned char>& wav, float durationSec,
          float yawOffsetDeg) {
    Vector pos;
    if (!PositionAround(yawOffsetDeg, pos)) {
        acclog::Write("ProbeMemSound", "%s ignored — no viewpoint", what);
        return;
    }
    PlayAt(what, wav, durationSec, pos, yawOffsetDeg);
}

void PlayTone() {
    static const std::vector<unsigned char> wav = BuildToneWav();
    Play("tone", wav, kToneSeconds, 90.0f);
}

// Plays the next variant: rendered, normalised, then shelved, in the order
// positional_speech's Play uses.
void PlayRearVariant() {
    const RearVariant& v = kRearVariants[g_rearNext++];
    prism::RenderedSpeech speech;
    if (!prism::RenderUrgentToMemory(v.text, prism::GetUrgentRatePercent(), speech) ||
        speech.samples.empty() || speech.channels == 0) {
        acclog::Write("ProbeMemSound", "rear probe: render failed for [%s]", v.text);
        prism::Speak("memory probe failed", /*interrupt=*/true);
        g_rearNext = kRearVariantCount;
        return;
    }
    acc::audio::NormalizePeak(speech.samples, kSpeechPeakDb);
    const acc::audio::Levels before = acc::audio::MeasureLevels(speech.samples);
    acc::audio::ApplyHighShelf(speech.samples, speech.channels, speech.sampleRate, v.cornerHz,
                               v.cutDb);
    const acc::audio::Levels after = acc::audio::MeasureLevels(speech.samples);
    acclog::Write("ProbeMemSound",
                  "rear probe %d/%d: shelf %.0f Hz %.1f dB at %u Hz -> rms %.1f -> %.1f dBFS "
                  "(%.1f dB), peak %.1f -> %.1f dBFS: [%s]",
                  g_rearNext, kRearVariantCount, v.cornerHz, v.cutDb,
                  static_cast<unsigned>(speech.sampleRate), before.rmsDb, after.rmsDb,
                  after.rmsDb - before.rmsDb, before.peakDb, after.peakDb, v.text);

    const size_t frames = speech.samples.size() / speech.channels;
    std::vector<unsigned char> wav;
    acc::audio::BuildMonoPcm16Wav(speech.samples.data(), frames, speech.channels,
                                  speech.sampleRate, wav);
    Vector pos;
    if (!PositionBehindListener(pos)) {
        acclog::Write("ProbeMemSound", "rear probe ignored — no viewpoint");
        g_rearNext = kRearVariantCount;
        return;
    }
    const float seconds = static_cast<float>(frames) / speech.sampleRate;
    if (!PlayAt("rear", wav, seconds, pos, 180.0f, static_cast<int>(kFullSourceVolume))) {
        g_rearNext = kRearVariantCount;
        return;
    }
    g_rearAt = GetTickCount() + static_cast<DWORD>(seconds * 1000.0f) + kRearProbeGapMs;
}

}  // namespace

void PollWin32() {
    namespace hk = acc::hotkeys;

    if (g_checkArmed && static_cast<int>(GetTickCount() - g_checkAt) >= 0) {
        g_checkArmed = false;
        g_source.LogPlayState("after-finish");
    }

    if (g_rearNext < kRearVariantCount && static_cast<int>(GetTickCount() - g_rearAt) >= 0) {
        PlayRearVariant();
    }

    const bool tone   = hk::Pressed(hk::Action::ProbeMemSoundTone);
    const bool speech = hk::Pressed(hk::Action::ProbeMemSoundSpeech);
    if (!tone && !speech) return;

    Vector player;
    if (!acc::engine::GetPlayerPosition(player)) {
        acclog::Write("ProbeMemSound", "press ignored — no player loaded");
        return;
    }
    if (tone)   PlayTone();
    if (speech) {
        g_rearNext = 0;  // a press restarts the sequence
        g_rearAt   = GetTickCount();
    }
}

}  // namespace acc::probe_memory_sound
