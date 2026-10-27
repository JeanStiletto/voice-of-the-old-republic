#include "positional_speech.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")  // RegOpenKeyExA — NVDA voice token names

#include "audio_bus.h"         // GetCuePriorityGroup
#include "audio_loop.h"
#include "audio_wav.h"
#include "eloquence_render.h"
#include "engine_area.h"
#include "engine_player.h"
#include "log.h"
#include "menus_modsettings.h"
#include "nvda_config.h"
#include "prism.h"

namespace acc::positional_speech {

namespace {

// ---- Tuning ----------------------------------------------------------------
// Every line is peak-normalised to this, then the voice's own volume setting
// is applied as the per-source game volume (127 = engine full). Same chain the
// probe settled on (2026-10-27): levels matched NVDA's.
constexpr float kPeakDb           = -1.0f;
constexpr float kFullSourceVolume = 127.0f;
// 3D attenuation band (CExoSoundSource::SetDistance): full volume within
// kFullVolumeM, fading out by kSilentM. Wider than the cue default (10 / 20 m)
// so a blaster exchange across a room stays audible; beyond kSilentM the
// engine refuses the source and the line falls back to centred speech.
constexpr float kFullVolumeM = 4.0f;
constexpr float kSilentM     = 30.0f;
// Pause between two lines of the same channel.
constexpr DWORD kLineGapMs = 120;
// How long the screen-reader voice choice (nvda.ini + registry) is reused.
constexpr DWORD kVoiceChoiceCacheMs = 2000;

// ---- Voice choice --------------------------------------------------------
enum class VoiceKind { Eloquence, Sapi, OneCore, Urgent };

struct VoiceChoice {
    VoiceKind   kind = VoiceKind::Urgent;
    std::string name;          // display name for Sapi / OneCore
    int         rate   = -1;   // NVDA percent, -1 = voice default
    int         volume = -1;   // NVDA percent, -1 = full
};

const char* KindName(VoiceKind k) {
    switch (k) {
        case VoiceKind::Eloquence: return "Eloquence";
        case VoiceKind::Sapi:      return "SAPI";
        case VoiceKind::OneCore:   return "OneCore";
        case VoiceKind::Urgent:    return "urgent";
    }
    return "?";
}

// NVDA stores a SAPI / OneCore voice as its registry token path
// ("HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens\...").
// The token key's default value is the voice's display name. The game is
// 32-bit, so the read lands in WOW6432Node — where both kinds of token are
// registered for 32-bit clients.
std::string TokenDisplayName(const char* tokenPath) {
    if (!tokenPath) return std::string();
    const char* kHklm = "HKEY_LOCAL_MACHINE\\";
    if (strncmp(tokenPath, kHklm, strlen(kHklm)) != 0) return std::string();
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, tokenPath + strlen(kHklm), 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return std::string();
    }
    char value[256] = {};
    DWORD size = sizeof(value) - 1;
    DWORD type = 0;
    const LONG rc = RegQueryValueExA(key, nullptr, nullptr, &type,
                                     reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return (rc == ERROR_SUCCESS && type == REG_SZ) ? std::string(value) : std::string();
}

// What voice the normal channel imitates right now. Only NVDA is imitated:
// it is the one reader whose voice and settings the mod can read.
VoiceChoice ChooseNormalVoice() {
    VoiceChoice v;
    const wchar_t* reader = prism::ActiveScreenReader();
    if (!reader || wcscmp(reader, L"NVDA") != 0) return v;
    nvda_config::Config cfg;
    if (!nvda_config::Load(cfg)) return v;
    const char* synth = cfg.Find("speech", "", "synth");
    if (!synth) return v;
    if (strcmp(synth, "ibmeci") == 0) {
        if (eloquence::IsNvdaSynth()) v.kind = VoiceKind::Eloquence;
        return v;
    }
    const bool sapi    = strcmp(synth, "sapi5") == 0;
    const bool oneCore = strcmp(synth, "oneCore") == 0;
    if (!sapi && !oneCore) return v;
    std::string name = TokenDisplayName(cfg.Find("speech", synth, "voice"));
    if (name.empty()) return v;
    v.kind   = sapi ? VoiceKind::Sapi : VoiceKind::OneCore;
    v.name   = name;
    v.rate   = cfg.FindInt("speech", synth, "rate", -1);
    v.volume = cfg.FindInt("speech", synth, "volume", -1);
    return v;
}

const VoiceChoice& CachedNormalVoice() {
    static VoiceChoice s_choice;
    static DWORD       s_at    = 0;
    static bool        s_valid = false;
    const DWORD now = GetTickCount();
    if (!s_valid || now - s_at >= kVoiceChoiceCacheMs) {
        VoiceChoice next = ChooseNormalVoice();
        if (!s_valid || next.kind != s_choice.kind || next.name != s_choice.name) {
            acclog::Write("PosSpeech", "normal voice: %s%s%s", KindName(next.kind),
                          next.name.empty() ? "" : " ", next.name.c_str());
        }
        s_choice = next;
        s_at     = now;
        s_valid  = true;
    }
    return s_choice;
}

// ---- Subject lookup ------------------------------------------------------
// Trim trailing spaces / dots, as the combat log parser and party cache do.
void TrimName(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = '\0';
}

float DistSq(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Anything a combat line can be about: creatures, and the doors and
// placeables (security doors, containers, terminals) the party can attack.
// The first test round shot a security door — creatures alone found nothing.
bool IsCombatSubjectKind(int kind) {
    using acc::engine::GameObjectKind;
    return kind == static_cast<int>(GameObjectKind::Creature) ||
           kind == static_cast<int>(GameObjectKind::Door) ||
           kind == static_cast<int>(GameObjectKind::Placeable);
}

// Does `obj` go by `name` in the combat log? The engine's display-name
// accessor is what the log prints for creatures; doors and placeables are
// also checked against their own localized name (GetObjectBaseName).
bool HasName(void* obj, uint32_t handle, const char* name) {
    char shown[96] = {};
    if (acc::engine::GetObjectDisplayNameByHandle(handle, shown, sizeof(shown))) {
        TrimName(shown);
        if (strcmp(shown, name) == 0) return true;
    }
    shown[0] = '\0';
    if (acc::engine::GetObjectBaseName(obj, shown, sizeof(shown))) {
        TrimName(shown);
        if (strcmp(shown, name) == 0) return true;
    }
    return false;
}

// The object named `name` closest to `ref`. Server handle 0 when none is
// found.
uint32_t FindSubject(const char* name, const Vector& ref, Vector& outPos) {
    if (!name || !*name) return 0;
    void* area = acc::engine::GetCurrentArea();
    if (!area) return 0;
    uint32_t best = 0;
    float    bestD = 0.0f;
    acc::engine::AreaObjectIterator it(area);
    for (void* obj = it.Next(); obj; obj = it.Next()) {
        if (!IsCombatSubjectKind(acc::engine::GetObjectKind(obj))) continue;
        const uint32_t handle = acc::engine::GetObjectHandle(obj);
        if (!handle || !HasName(obj, handle, name)) continue;
        Vector p;
        if (!acc::engine::GetObjectPosition(obj, p)) continue;
        const float d = DistSq(p, ref);
        if (!best || d < bestD) {
            best   = handle;
            bestD  = d;
            outPos = p;
        }
    }
    return best;
}

// ---- Lines and channels ------------------------------------------------------
enum class LineState { Rendering, Ready, Failed };

struct Line {
    uint32_t              ticket = 0;
    Channel               channel = Channel::Normal;
    std::string           text;
    uint32_t              subject = 0;    // server handle; position read at play time
    Vector                lastPos = {};   // position at enqueue, the fallback
    LineState             state = LineState::Rendering;
    prism::RenderedSpeech speech;
};

struct ChannelState {
    std::deque<Line>       queue;
    acc::audio::LoopSource source;
    DWORD                  busyUntil = 0;   // GetTickCount when the playing line ends
    bool                   busy      = false;
    int                    flip      = 0;   // alternates the resref per line
};

ChannelState g_channels[2];
uint32_t     g_nextTicket = 1;

ChannelState& StateOf(Channel c) { return g_channels[c == Channel::Urgent ? 1 : 0]; }

// The centred fallback — the path these lines took before this feature.
void SpeakCentred(const Line& line, const char* why) {
    acclog::Write("PosSpeech", "fallback (%s) %s: [%s]", why,
                  line.channel == Channel::Urgent ? "urgent" : "normal", line.text.c_str());
    if (line.channel == Channel::Urgent) prism::SpeakUrgent(line.text.c_str());
    else                                 prism::Speak(line.text.c_str(), /*interrupt=*/false);
}

// Synchronous render for every voice but Eloquence (SAPI / OneCore take
// ~16 ms). A named voice that fails falls back to the urgent voice.
bool RenderNow(Channel channel, const VoiceChoice& voice, const char* text,
               prism::RenderedSpeech& out) {
    if (channel == Channel::Normal && voice.kind == VoiceKind::Sapi &&
        prism::RenderSapiVoiceToMemory(voice.name.c_str(), voice.rate, text, out)) {
        out.volume = voice.volume >= 0 ? voice.volume / 100.0f : 1.0f;
        return true;
    }
    if (channel == Channel::Normal && voice.kind == VoiceKind::OneCore &&
        prism::RenderOneCoreToMemory(voice.name.c_str(), voice.rate, text, out)) {
        out.volume = voice.volume >= 0 ? voice.volume / 100.0f : 1.0f;
        return true;
    }
    return prism::RenderUrgentToMemory(text, out);
}

// Starts `line` on the channel's source. False when the engine did not take
// it (the caller falls back).
bool Play(ChannelState& ch, Line& line) {
    Vector pos = line.lastPos;
    if (void* obj = acc::engine::ResolveServerObjectHandle(line.subject)) {
        Vector live;
        if (acc::engine::GetObjectPosition(obj, live)) pos = live;
    }
    prism::RenderedSpeech& sp = line.speech;
    acc::audio::NormalizePeak(sp.samples, kPeakDb);
    const size_t frames = sp.samples.size() / sp.channels;
    std::vector<unsigned char> wav;
    if (!acc::audio::BuildMonoPcm16Wav(sp.samples.data(), frames, sp.channels,
                                       sp.sampleRate, wav)) {
        return false;
    }
    const int volumeByte = static_cast<int>(kFullSourceVolume * sp.volume + 0.5f);
    char resref[17];
    snprintf(resref, sizeof(resref), "acc_tts_%c%d",
             line.channel == Channel::Urgent ? 'u' : 'n', ch.flip);
    ch.flip ^= 1;
    if (!ch.source.StartFromMemory(resref, wav.data(), wav.size(), pos,
                                   acc::audio::GetCuePriorityGroup(), volumeByte,
                                   kFullVolumeM, kSilentM) ||
        !ch.source.IsVoiceAttached()) {
        ch.source.Stop();
        return false;
    }
    const DWORD durationMs = static_cast<DWORD>(1000.0 * frames / sp.sampleRate);
    ch.busy      = true;
    ch.busyUntil = GetTickCount() + durationMs + kLineGapMs;
    acclog::Write("PosSpeech", "play %s at (%.1f,%.1f,%.1f) vol=%d %lu ms: [%s]",
                  line.channel == Channel::Urgent ? "urgent" : "normal",
                  pos.x, pos.y, pos.z, volumeByte, durationMs, line.text.c_str());
    prism::NoteSpokenElsewhere(line.text.c_str());  // log, Ctrl+R, braille
    return true;
}

void TickChannel(ChannelState& ch) {
    const DWORD now = GetTickCount();
    if (ch.busy && static_cast<int>(now - ch.busyUntil) < 0) return;
    ch.busy = false;
    while (!ch.queue.empty()) {
        Line& front = ch.queue.front();
        if (front.state == LineState::Rendering) return;  // keep order: wait for it
        if (front.state == LineState::Failed) {
            SpeakCentred(front, "render failed");
        } else if (!Play(ch, front)) {
            SpeakCentred(front, "no 3D voice");
        } else {
            ch.queue.pop_front();
            return;  // playing; the next line waits for this one
        }
        ch.queue.pop_front();
    }
}

void CollectEloquence() {
    uint32_t ticket = 0;
    prism::RenderedSpeech out;
    bool ok = false;
    while (acc::eloquence::TakeFinished(ticket, out, ok)) {
        for (Line& line : StateOf(Channel::Normal).queue) {
            if (line.ticket != ticket) continue;
            if (ok) {
                line.speech = std::move(out);
                line.state  = LineState::Ready;
            } else if (RenderNow(Channel::Normal, VoiceChoice{}, line.text.c_str(),
                                 line.speech)) {
                line.state = LineState::Ready;  // Eloquence failed: urgent voice
            } else {
                line.state = LineState::Failed;
            }
            break;
        }
    }
}

}  // namespace

bool IsEnabled() {
    return acc::menus::modsettings::GetToggle(
        acc::menus::modsettings::Option::PositionalCombatSpeech);
}

bool Speak(Channel channel, const char* text, const char* subjectIn, const char* nearIn) {
    if (!text || !*text || !subjectIn || !IsEnabled()) return false;
    Vector ref;
    if (!acc::engine::GetPlayerPosition(ref)) return false;
    // Callers may hand raw slices of a log line ("Name " before an anchor).
    char subject[96], nearName[96];
    snprintf(subject, sizeof(subject), "%s", subjectIn + strspn(subjectIn, " "));
    TrimName(subject);
    snprintf(nearName, sizeof(nearName), "%s", nearIn ? nearIn + strspn(nearIn, " ") : "");
    TrimName(nearName);
    Vector nearPos;
    if (nearName[0] && FindSubject(nearName, ref, nearPos)) ref = nearPos;

    Line line;
    line.ticket   = g_nextTicket++;
    line.channel  = channel;
    line.text     = text;
    line.subject = FindSubject(subject, ref, line.lastPos);
    if (!line.subject) {
        acclog::Write("PosSpeech", "nothing named [%s] to place; centred: [%s]",
                      subject ? subject : "", text);
        return false;
    }

    const VoiceChoice& voice = channel == Channel::Normal ? CachedNormalVoice() : VoiceChoice{};
    if (voice.kind == VoiceKind::Eloquence && acc::eloquence::Submit(text, line.ticket)) {
        line.state = LineState::Rendering;
    } else {
        line.state = RenderNow(channel, voice, text, line.speech) ? LineState::Ready
                                                                  : LineState::Failed;
    }
    acclog::Write("PosSpeech", "queue %s subject=[%s] handle=0x%08X voice=%s state=%d: [%s]",
                  channel == Channel::Urgent ? "urgent" : "normal", subject, line.subject,
                  channel == Channel::Urgent ? "urgent" : KindName(voice.kind),
                  static_cast<int>(line.state), text);
    StateOf(channel).queue.push_back(std::move(line));
    TickChannel(StateOf(channel));  // start at once when the channel is idle
    return true;
}

void Tick() {
    CollectEloquence();
    TickChannel(g_channels[0]);
    TickChannel(g_channels[1]);
}

}  // namespace acc::positional_speech
