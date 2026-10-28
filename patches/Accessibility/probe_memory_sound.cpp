#include "probe_memory_sound.h"

#include <windows.h>
#include <cmath>
#include <vector>

#include "audio_bus.h"
#include "audio_loop.h"
#include "audio_wav.h"
#include "eloquence_render.h"
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

// The SAPI (urgent voice) and OneCore (Stefan) blocks that used to lead this
// table passed in game on 2026-10-27 and were retired; the renderers stay in
// prism.cpp. What remains under test is Eloquence.
struct Phrase {
    float       yawOffsetDeg;  // from the view direction, CCW positive
    const char* text;          // ASCII only: rendered by a German voice
};
const Phrase kPhrases[] = {
    { 90.0f, "Eloquence links. Diese Stimme sollte von links kommen."},
    {-90.0f, "Eloquence rechts. Diese Stimme sollte von rechts kommen."},
    {  0.0f, "Eloquence vorne. Diese Stimme sollte von vorne kommen."},
    {180.0f, "Eloquence hinten. Diese Stimme sollte von hinten kommen."},
};
constexpr int kPhraseCount = sizeof(kPhrases) / sizeof(kPhrases[0]);

acc::audio::LoopSource g_source;
int   g_presses     = 0;
int   g_nextPhrase  = 0;
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

// volumeByte: per-source game volume 0..127; < 0 leaves the engine's full 127.
void Play(const char* what, const std::vector<unsigned char>& wav, float durationSec,
          float yawOffsetDeg, int volumeByte = -1) {
    Vector pos;
    if (!PositionAround(yawOffsetDeg, pos)) {
        acclog::Write("ProbeMemSound", "%s ignored — no viewpoint", what);
        return;
    }
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
        return;
    }
    g_checkAt    = GetTickCount() + static_cast<DWORD>(durationSec * 1000.0f) + kAfterPlayMarginMs;
    g_checkArmed = true;
}

void PlayTone() {
    static const std::vector<unsigned char> wav = BuildToneWav();
    Play("tone", wav, kToneSeconds, 90.0f);
}

void PlayPhrase() {
    const Phrase& p = kPhrases[g_nextPhrase];
    g_nextPhrase = (g_nextPhrase + 1) % kPhraseCount;

    prism::RenderedSpeech speech;
    if (!acc::eloquence::RenderToMemory(p.text, speech)) {
        acclog::Write("ProbeMemSound", "render failed for [%s]", p.text);
        prism::Speak("memory probe failed", /*interrupt=*/true);
        return;
    }
    // Same headroom for every line and voice, played at the game's full
    // source volume.
    const acc::audio::Levels before = acc::audio::MeasureLevels(speech.samples);
    const float gain = acc::audio::NormalizePeak(speech.samples, kSpeechPeakDb);
    const int volumeByte = static_cast<int>(kFullSourceVolume);
    acclog::Write("ProbeMemSound",
                  "levels: peak %.1f dBFS, rms %.1f dBFS -> gain x%.2f to %.1f dBFS peak; "
                  "source volume %d/127",
                  before.peakDb, before.rmsDb, gain, kSpeechPeakDb, volumeByte);

    const size_t frames = speech.samples.size() / speech.channels;
    std::vector<unsigned char> wav;
    acc::audio::BuildMonoPcm16Wav(speech.samples.data(), frames, speech.channels,
                                  speech.sampleRate, wav);
    Play("speech", wav, static_cast<float>(frames) / speech.sampleRate, p.yawOffsetDeg,
         volumeByte);
}

}  // namespace

void PollWin32() {
    namespace hk = acc::hotkeys;

    if (g_checkArmed && static_cast<int>(GetTickCount() - g_checkAt) >= 0) {
        g_checkArmed = false;
        g_source.LogPlayState("after-finish");
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
    if (speech) PlayPhrase();
}

}  // namespace acc::probe_memory_sound
