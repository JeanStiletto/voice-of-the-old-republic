#include "positional_speech.h"

#include <windows.h>
#include <algorithm>
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
#include "mod_settings_store.h"
#include "nvda_config.h"
#include "prism.h"
#include "transitions.h"       // IsWorldSpeechGated — cuts a stale exit list
#include "view_mode.h"

namespace acc::positional_speech {

namespace {

// ---- Tuning ----------------------------------------------------------------
// Every line is peak-normalised to this, then played at the channel's volume
// setting × the distance gain as the per-source game volume (127 = engine
// full).
constexpr float kPeakDb           = -1.0f;
constexpr float kFullSourceVolume = 127.0f;
// Placement, the swoop beacons' method (minigame_swoop_audio.cpp "Decoupled
// cue"). The first build played each line AT its subject with a 4 / 30 m
// engine band: lines faded to near silence by 30 m and past it the engine
// refused the source, so the line jumped to loud centred speech — and NVDA's
// own volume on top made it all too quiet (user, 2026-10-28). Now:
//   - the source sits kPlaceRadiusM from the listener, in the subject's
//     direction, inside a flat engine band (full volume to kFlatBandFullM),
//     so the engine supplies the pan and nothing else;
//   - loudness is ours, over the REAL distance from the player: full within
//     kFullVolumeM, falling linearly to kFloorGain at kFloorAtM, and staying
//     there beyond;
//   - from kCentreFromM to kCentredAtM the direction eases to straight ahead,
//     so a far line ends centred with no step in level or direction.
// Fights rarely reach past 30 m and the game's own sounds stop there too.
constexpr float kPlaceRadiusM  = 5.0f;
constexpr float kFlatBandFullM = 20.0f;
constexpr float kFlatBandEndM  = 25.0f;
constexpr float kFullVolumeM   = 4.0f;
constexpr float kFloorAtM      = 30.0f;
constexpr float kFloorGain     = 0.4f;   // swoop's far pads: 50 / 127
constexpr float kCentreFromM   = 20.0f;
constexpr float kCentredAtM    = 30.0f;
// Exit lines (2026-10-28): the guidance beacon's soft knee
// (guidance_beacon.cpp CompressHeartbeatPosition), as our own gain since the
// source sits at kPlaceRadiusM anyway. Full within kFullVolumeM, linear down
// to kExitKneeGain at kExitKneeM, then easing asymptotically toward
// kExitFarGain, so a far exit is faint but still placed and still a little
// quieter the farther it is: ~0.47 at 50 m, ~0.41 at 100 m. The bearing is
// NEVER eased toward straight ahead (combat's kCentreFromM): a Kashyyyk exit
// 80 m off has to say which way to turn.
constexpr float kExitKneeM     = 18.0f;
constexpr float kExitKneeGain  = 0.55f;
constexpr float kExitFarGain   = 0.4f;
constexpr float kExitFarScaleM = 40.0f;
// A subject this close to the player (a hit on the player, an exit the
// player stands in) has no usable direction: straight ahead.
constexpr float kNoDirectionM  = 1.0f;
// Behind-you cue (2026-10-28), taken from Dark Vision, the blind-accessibility
// mod for Pathfinder: Wrath of the Righteous (github.com/bradjrenshaw/
// darkvision-wotr, src/Audio/Spatializer.cs), which is also where the idea of
// positional speech came from. The engine only pans, so a line straight
// behind sounds exactly like one straight ahead; even dsoal's HRTF confuses
// the two without head tracking. A line in the rear half gets a high-shelf
// CUT above kRearCornerHz, ramping linearly from 0 at the side to
// kRearMaxCutDb straight behind: darker = behind. A shelf, not a low-pass,
// so speech only darkens, never disappears. Always on (user, 2026-10-28).
// Dark Vision's 3 kHz / -10 dB was tuned for bright tone cues (where it is a
// ~10 dB drop of the whole sound); on speech, whose energy sits lower, it was
// too subtle. Picked by ear from a four-way A/B (Shift+F11 probe,
// 2026-10-28): 1.5 kHz / -14 dB. 1 kHz / -18 dB was "very deep".
constexpr float kRearCornerHz  = 1500.0f;
constexpr float kRearMaxCutDb  = 14.0f;
// Priority group for the sliders' preview: one of the groups that stay
// audible while the in-game menu pauses the world (as the cue-volume
// preview in menus_modsettings.cpp).
constexpr int kPreviewPriorityGroup = 0x0b;
// Pause between two lines of the same channel.
constexpr DWORD kLineGapMs = 120;
// How long the screen-reader voice choice (nvda.ini + registry) is reused.
constexpr DWORD kVoiceChoiceCacheMs = 2000;

// ---- Settings ----------------------------------------------------------------
// One record per channel, persisted in acc_settings.ini, loaded on first use.
struct ChannelSettings {
    const char* volumeKey;
    const char* rateKey;
    int         volume = 100;
    int         rate   = 0;
    bool        loaded = false;
};
ChannelSettings g_settings[2] = {
    { "PositionalVolumePercent",       "PositionalRateOffset" },
    { "PositionalUrgentVolumePercent", "PositionalUrgentRateOffset" },
};

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Exits shares Urgent's record: same voice, same sliders.
ChannelSettings& SettingsOf(Channel c) {
    ChannelSettings& s = g_settings[c == Channel::Normal ? 0 : 1];
    if (!s.loaded) {
        s.loaded = true;
        s.volume = Clamp(acc::settings::GetInt(s.volumeKey, 100), kMinVolumePercent, 100);
        s.rate   = Clamp(acc::settings::GetInt(s.rateKey, 0), -kMaxRateOffset, kMaxRateOffset);
    }
    return s;
}

// ---- Voice choice --------------------------------------------------------
enum class VoiceKind { Eloquence, Sapi, OneCore, Urgent };

struct VoiceChoice {
    VoiceKind   kind = VoiceKind::Urgent;
    std::string name;          // display name for Sapi / OneCore
    int         rate   = -1;   // NVDA percent, -1 = voice default
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
// Unrendered: rendered when it reaches the front of its queue (exit pieces —
// a junction's five pieces would otherwise cost five SAPI renders in one tick).
enum class LineState { Unrendered, Rendering, Ready, Failed };

struct Line {
    uint32_t              ticket = 0;
    Channel               channel = Channel::Normal;
    std::string           text;
    uint32_t              subject = 0;    // server handle; position read at play time
    Vector                lastPos = {};   // position at enqueue (a fixed point when no subject)
    LineState             state = LineState::Rendering;
    bool                  ahead   = false;  // straight ahead, no distance loss
    bool                  preview = false;  // ahead + the group audible under the menu pause
    bool                  noted   = false;  // its whole label already went to NoteSpokenElsewhere
    bool                  lead    = false;  // first piece of an exit list (LastExitLeadMs)
    prism::RenderedSpeech speech;
};

struct ChannelState {
    std::deque<Line>       queue;
    acc::audio::LoopSource source;
    DWORD                  busyUntil = 0;   // GetTickCount when the playing line ends
    bool                   busy      = false;
    int                    flip      = 0;   // alternates the resref per line
};

ChannelState g_channels[3];
uint32_t     g_nextTicket = 1;
DWORD        g_lastExitLeadMs = 0;

ChannelState& StateOf(Channel c) { return g_channels[static_cast<int>(c)]; }

const char* ChannelName(Channel c) {
    switch (c) {
        case Channel::Normal: return "normal";
        case Channel::Urgent: return "urgent";
        case Channel::Exits:  return "exits";
    }
    return "?";
}

// The centred fallback — the path these lines took before this feature.
void SpeakCentred(const Line& line, const char* why) {
    acclog::Write("PosSpeech", "fallback (%s) %s: [%s]", why, ChannelName(line.channel),
                  line.text.c_str());
    if (line.channel == Channel::Normal) prism::Speak(line.text.c_str(), /*interrupt=*/false);
    else                                 prism::SpeakUrgent(line.text.c_str());
}

// Synchronous render for every voice but Eloquence (SAPI / OneCore take
// ~16 ms). A named voice that fails falls back to the urgent voice. The
// channel's speed offset moves whichever voice's own rate is used.
bool RenderNow(Channel channel, const VoiceChoice& voice, const char* text,
               prism::RenderedSpeech& out) {
    const int offset = SettingsOf(channel).rate;
    const int rate   = prism::OffsetRatePercent(voice.rate, offset);
    if (channel == Channel::Normal && voice.kind == VoiceKind::Sapi &&
        prism::RenderSapiVoiceToMemory(voice.name.c_str(), rate, text, out)) {
        return true;
    }
    if (channel == Channel::Normal && voice.kind == VoiceKind::OneCore &&
        prism::RenderOneCoreToMemory(voice.name.c_str(), rate, text, out)) {
        return true;
    }
    return prism::RenderUrgentToMemory(
        text, prism::OffsetRatePercent(prism::GetUrgentRatePercent(), offset), out);
}

// Our loudness curve over the real distance (see Tuning).
float DistanceGain(float d) {
    if (d <= kFullVolumeM) return 1.0f;
    if (d >= kFloorAtM)    return kFloorGain;
    return 1.0f + (d - kFullVolumeM) / (kFloorAtM - kFullVolumeM) * (kFloorGain - 1.0f);
}

// The exit lines' curve, the beacon's soft knee (see Tuning).
float ExitDistanceGain(float d) {
    if (d <= kFullVolumeM) return 1.0f;
    if (d <= kExitKneeM) {
        return 1.0f + (d - kFullVolumeM) / (kExitKneeM - kFullVolumeM) * (kExitKneeGain - 1.0f);
    }
    return kExitFarGain +
           (kExitKneeGain - kExitFarGain) * std::exp(-(d - kExitKneeM) / kExitFarScaleM);
}

// Where a line about `subject` (null = straight ahead, the preview) is played
// from, and its distance gain. Direction and distance are measured from the
// player (the view-mode cursor while it is active) against the camera's yaw,
// as every spoken bearing is; the source goes next to the engine's listener
// — the camera, or the cursor in view mode, which the listener detour
// substitutes. `exitProfile`: keep the bearing at every distance and use the
// exit curve. `outRear`: how far behind the listener the line sounds, 0 (ahead
// or to the side) .. 1 (straight behind), for the behind-you cue. False when
// there is no viewpoint (no area loaded).
bool Place(const Vector* subject, bool exitProfile, Vector& outPos, float& outGain,
           float& outDist, float& outRear) {
    Vector ref;
    float  yawDeg = 0.0f;
    if (!acc::view_mode::GetEffectiveViewpoint(ref, yawDeg)) return false;
    Vector listener = ref;
    if (!acc::view_mode::IsActive()) acc::engine::GetCameraPosition(listener);

    const float yaw = yawDeg * 3.14159265f / 180.0f;
    float rel = 0.0f;  // bearing relative to the view, radians
    outDist = 0.0f;
    if (subject) {
        const float dx = subject->x - ref.x, dy = subject->y - ref.y;
        outDist = std::sqrt(dx * dx + dy * dy);
        if (outDist >= kNoDirectionM) {
            rel = std::atan2(dy, dx) - yaw;
            while (rel >  3.14159265f) rel -= 6.2831853f;
            while (rel < -3.14159265f) rel += 6.2831853f;
            if (!exitProfile) {
                float t = (outDist - kCentreFromM) / (kCentredAtM - kCentreFromM);
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                rel *= 1.0f - t;
            }
        }
    }
    const float a = yaw + rel;
    outRear  = std::cos(rel) < 0.0f ? -std::cos(rel) : 0.0f;
    outPos   = listener;
    outPos.x += std::cos(a) * kPlaceRadiusM;
    outPos.y += std::sin(a) * kPlaceRadiusM;
    outGain  = !subject    ? 1.0f
             : exitProfile ? ExitDistanceGain(outDist)
                           : DistanceGain(outDist);
    return true;
}

// Starts `line` on the channel's source. False when the engine did not take
// it (the caller falls back).
bool Play(ChannelState& ch, Line& line) {
    Vector subject = line.lastPos;
    if (line.subject) {
        if (void* obj = acc::engine::ResolveServerObjectHandle(line.subject)) {
            Vector live;
            if (acc::engine::GetObjectPosition(obj, live)) subject = live;
        }
    }
    Vector pos;
    float  gain = 1.0f, dist = 0.0f, rear = 0.0f;
    if (!Place(line.ahead ? nullptr : &subject, line.channel == Channel::Exits, pos, gain,
               dist, rear)) {
        return false;
    }
    prism::RenderedSpeech& sp = line.speech;
    acc::audio::NormalizePeak(sp.samples, kPeakDb);
    // After the normaliser, so the cut also leaves a line behind a little
    // quieter, as in Dark Vision.
    const float rearDb = -kRearMaxCutDb * rear;
    acc::audio::ApplyHighShelf(sp.samples, sp.channels, sp.sampleRate, kRearCornerHz, rearDb);
    const size_t frames = sp.samples.size() / sp.channels;
    std::vector<unsigned char> wav;
    if (!acc::audio::BuildMonoPcm16Wav(sp.samples.data(), frames, sp.channels,
                                       sp.sampleRate, wav)) {
        return false;
    }
    const float volume = SettingsOf(line.channel).volume / 100.0f;
    int volumeByte = static_cast<int>(kFullSourceVolume * volume * gain + 0.5f);
    if (volumeByte < 1) volumeByte = 1;
    char resref[17];
    snprintf(resref, sizeof(resref), "acc_tts_%c%d", ChannelName(line.channel)[0], ch.flip);
    ch.flip ^= 1;
    const int group = line.preview ? kPreviewPriorityGroup : acc::audio::GetCuePriorityGroup();
    if (!ch.source.StartFromMemory(resref, wav.data(), wav.size(), pos, group, volumeByte,
                                   kFlatBandFullM, kFlatBandEndM) ||
        !ch.source.IsVoiceAttached()) {
        ch.source.Stop();
        return false;
    }
    const DWORD durationMs = static_cast<DWORD>(1000.0 * frames / sp.sampleRate);
    if (line.lead) g_lastExitLeadMs = durationMs;
    ch.busy      = true;
    ch.busyUntil = GetTickCount() + durationMs + kLineGapMs;
    acclog::Write("PosSpeech", "play %s%s at (%.1f,%.1f,%.1f) dist=%.1f m gain=%.2f rear=%.1f dB vol=%d %lu ms: [%s]",
                  ChannelName(line.channel),
                  line.preview ? " preview" : (line.ahead ? " ahead" : ""), pos.x, pos.y, pos.z,
                  dist, gain, rearDb,
                  volumeByte, durationMs, line.text.c_str());
    if (!line.noted) prism::NoteSpokenElsewhere(line.text.c_str());  // log, Ctrl+R, braille
    return true;
}

void TickChannel(ChannelState& ch) {
    const DWORD now = GetTickCount();
    if (ch.busy && static_cast<int>(now - ch.busyUntil) < 0) return;
    ch.busy = false;
    while (!ch.queue.empty()) {
        Line& front = ch.queue.front();
        if (front.state == LineState::Unrendered) {
            front.state = RenderNow(front.channel, VoiceChoice{}, front.text.c_str(),
                                    front.speech)
                              ? LineState::Ready
                              : LineState::Failed;
        }
        if (front.state == LineState::Rendering) return;  // keep order: wait for it
        const char* why = front.state == LineState::Failed ? "render failed"
                        : !Play(ch, front)                 ? "no 3D voice"
                                                           : nullptr;
        if (why && front.channel == Channel::Exits) {
            // SpeakUrgent interrupts, so piece by piece only the last one
            // would be heard: the rest of the list goes centred as one line.
            Line rest;
            rest.channel = Channel::Exits;
            for (const Line& l : ch.queue) {
                if (!rest.text.empty()) rest.text += ", ";
                rest.text += l.text;
            }
            ch.queue.clear();
            SpeakCentred(rest, why);
            return;
        }
        if (why) {
            SpeakCentred(front, why);
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

// Renders `line` in its channel's voice (Eloquence in the background), queues
// it and starts it at once when the channel is idle.
void Enqueue(Line&& line, const char* subjectName) {
    const Channel channel = line.channel;
    const char*   text    = line.text.c_str();
    const VoiceChoice& voice = channel == Channel::Normal ? CachedNormalVoice() : VoiceChoice{};
    if (voice.kind == VoiceKind::Eloquence &&
        acc::eloquence::Submit(text, line.ticket, SettingsOf(channel).rate)) {
        line.state = LineState::Rendering;
    } else {
        line.state = RenderNow(channel, voice, text, line.speech) ? LineState::Ready
                                                                  : LineState::Failed;
    }
    acclog::Write("PosSpeech", "queue %s subject=[%s] handle=0x%08X voice=%s state=%d: [%s]",
                  ChannelName(channel), subjectName, line.subject,
                  channel == Channel::Normal ? KindName(voice.kind) : "urgent",
                  static_cast<int>(line.state), text);
    StateOf(channel).queue.push_back(std::move(line));
    TickChannel(StateOf(channel));
}

// Drops the channel's waiting lines and stops the one playing.
void Cut(Channel channel) {
    ChannelState& ch = StateOf(channel);
    ch.queue.clear();
    ch.source.Stop();
    ch.busy = false;
}

// ---- Exit lists ----------------------------------------------------------------
// One piece of a split region label: an exit (placed at `pos`) or the text
// between exits (straight ahead).
struct Piece {
    std::string text;
    bool        placed = false;
    Vector      pos    = {};
};

// A byte that continues a word: a match ending or starting next to one is
// inside a longer word ("Nord" in "Nord-Süd"). Bytes >= 0x80 are the
// codepage's letters (ü, é, Cyrillic).
bool IsWordByte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '-' || c >= 0x80;
}

// Adds the text between two exits as a straight-ahead piece, minus the list
// separators around it; nothing when only separators are left.
void AddAhead(const std::string& text, size_t from, size_t to, std::vector<Piece>& out) {
    auto isSep = [](char c) { return c == ' ' || c == ',' || c == ';' || c == '.'; };
    while (from < to && isSep(text[from])) ++from;
    while (to > from && isSep(text[to - 1])) --to;
    if (from == to) return;
    Piece p;
    p.text = text.substr(from, to - from);
    out.push_back(std::move(p));
}

// Splits `text` at the exits. Longest entries claim their span first, so a
// short entry ("Nord") can't land inside a longer one ("Übergang Nord nach X");
// a claim must be a whole entry (IsWordByte) and must not overlap an earlier
// claim. Marks the text doesn't contain are dropped with a log line.
void SplitAtExits(const std::string& text,
                  const std::vector<acc::room_topology::ExitMark>& exits,
                  std::vector<Piece>& out) {
    struct Span { size_t at, end; const acc::room_topology::ExitMark* mark; };
    std::vector<const acc::room_topology::ExitMark*> order;
    for (const auto& e : exits) {
        if (!e.text.empty()) order.push_back(&e);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto* a, const auto* b) {
        return a->text.size() > b->text.size();
    });
    std::vector<Span> spans;
    for (const auto* mark : order) {
        bool claimed = false;
        for (size_t at = text.find(mark->text); at != std::string::npos;
             at = text.find(mark->text, at + 1)) {
            const size_t end = at + mark->text.size();
            const bool whole =
                (at == 0 || !IsWordByte(static_cast<unsigned char>(text[at - 1]))) &&
                (end == text.size() || !IsWordByte(static_cast<unsigned char>(text[end])));
            bool overlaps = false;
            for (const Span& s : spans) {
                if (at < s.end && s.at < end) overlaps = true;
            }
            if (whole && !overlaps) {
                spans.push_back({at, end, mark});
                claimed = true;
                break;
            }
        }
        if (!claimed) {
            acclog::Write("PosSpeech", "exit mark not found in text: [%s] in [%s]",
                          mark->text.c_str(), text.c_str());
        }
    }
    std::sort(spans.begin(), spans.end(),
              [](const Span& a, const Span& b) { return a.at < b.at; });
    size_t cursor = 0;
    for (const Span& s : spans) {
        AddAhead(text, cursor, s.at, out);
        Piece p;
        p.text   = s.mark->text;
        p.placed = true;
        p.pos    = s.mark->pos;
        out.push_back(std::move(p));
        cursor = s.end;
    }
    AddAhead(text, cursor, text.size(), out);
}

}  // namespace

bool IsEnabled(Channel channel) {
    using acc::menus::modsettings::Option;
    return acc::menus::modsettings::GetToggle(channel == Channel::Exits
                                                  ? Option::PositionalExitSpeech
                                                  : Option::PositionalCombatSpeech);
}

bool SpeakExitList(const char* text,
                   const std::vector<acc::room_topology::ExitMark>& exits) {
    if (!text || !*text || !IsEnabled(Channel::Exits)) return false;
    if (IsSpeaking(Channel::Exits)) {
        acclog::Write("PosSpeech", "exits cut (replaced by a new label)");
    }
    Cut(Channel::Exits);

    std::vector<Piece> pieces;
    SplitAtExits(text, exits, pieces);
    int placed = 0;
    for (const Piece& p : pieces) placed += p.placed ? 1 : 0;
    acclog::Write("PosSpeech", "exits [%s] -> %d pieces, %d placed", text,
                  static_cast<int>(pieces.size()), placed);
    // A label with no exit in it still plays through this slot, straight
    // ahead: one region label must never overlap another.
    if (pieces.empty()) {
        Piece whole;
        whole.text = text;
        pieces.push_back(std::move(whole));
    }
    // The whole label, once: Ctrl+R repeats it and braille shows it, not
    // just the last piece.
    prism::NoteSpokenElsewhere(text);
    ChannelState& ch = StateOf(Channel::Exits);
    for (Piece& p : pieces) {
        Line line;
        line.ticket  = g_nextTicket++;
        line.channel = Channel::Exits;
        line.text    = std::move(p.text);
        line.lastPos = p.pos;
        line.ahead   = !p.placed;
        line.noted   = true;
        // The noun: an unplaced first piece with exits after it. A list
        // that opens on an exit word ("Nord, Süd-Ost") has no noun.
        line.lead    = (&p == &pieces.front()) && !p.placed &&
                       pieces.size() > 1;
        line.state   = LineState::Unrendered;
        ch.queue.push_back(std::move(line));
    }
    TickChannel(ch);
    return true;
}

bool IsSpeaking(Channel channel) {
    const ChannelState& ch = StateOf(channel);
    if (!ch.queue.empty()) return true;
    return ch.busy && static_cast<int>(GetTickCount() - ch.busyUntil) < 0;
}

unsigned long LastExitLeadMs() {
    return g_lastExitLeadMs;
}

bool Speak(Channel channel, const char* text, const char* subjectIn, const char* nearIn) {
    if (!text || !*text || !subjectIn || channel == Channel::Exits || !IsEnabled(channel)) {
        return false;
    }
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

    Enqueue(std::move(line), subject);
    return true;
}

void Preview(Channel channel, const char* text) {
    if (!text || !*text) return;
    Cut(channel);  // each slider press restarts the sample
    Line line;
    line.ticket  = g_nextTicket++;
    line.channel = channel;
    line.text    = text;
    line.ahead   = true;
    line.preview = true;
    Enqueue(std::move(line), "(preview)");
}

int GetVolumePercent(Channel channel) { return SettingsOf(channel).volume; }

void SetVolumePercent(Channel channel, int percent) {
    ChannelSettings& s = SettingsOf(channel);
    s.volume = Clamp(percent, kMinVolumePercent, 100);
    acc::settings::SetInt(s.volumeKey, s.volume);
}

int GetRateOffset(Channel channel) { return SettingsOf(channel).rate; }

void SetRateOffset(Channel channel, int offset) {
    ChannelSettings& s = SettingsOf(channel);
    s.rate = Clamp(offset, -kMaxRateOffset, kMaxRateOffset);
    acc::settings::SetInt(s.rateKey, s.rate);
}

void Tick() {
    CollectEloquence();
    // An exit list is about where the player stands right now: combat or a
    // blocking menu makes it stale (and the menu would pause the source
    // mid-list). Region speech stays gated until that ends.
    if (IsSpeaking(Channel::Exits) && acc::transitions::IsWorldSpeechGated()) {
        acclog::Write("PosSpeech", "exits cut (world speech gated: combat or menu)");
        Cut(Channel::Exits);
    }
    for (ChannelState& ch : g_channels) TickChannel(ch);
}

}  // namespace acc::positional_speech
