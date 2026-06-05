// virtual mod-settings submenu.
//
// See menus_modsettings.h for the design summary. This TU owns the
// sentinel pointer, the toggle bits, and the input router.

#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "menus_modsettings.h"

#include "audio_bus.h"        // global cue-volume slider get/set
#include "audio_cues.h"       // NavCue + GetNavCueResref
#include "audio_loop.h"       // LoopSource — glossary audition via CExoSoundSource
#include "engine_input.h"
#include "engine_manager.h"  // IsPanelInManager for close-time rebind guard
#include "engine_panels.h"
#include "intro_skip.h"      // SkipIntros toggle — filesystem-backed state
#include "log.h"
#include "menus_chain.h"
#include "mod_settings_store.h"  // persist toggles across launches
#include "prism.h"
#include "strings.h"

namespace acc::menus::modsettings {

namespace {

// Sentinel: address-of a never-deref'd static byte. Casting to void*
// gives a stable, unique pointer that we can use as the chain entry's
// `control` field without ever pointing at an engine-allocated control.
char s_rootSentinel = 0;

// Toggle state. In-memory only — persistence is a follow-up.
//
// Defaults per user 2026-05-27:
//   - ExtendedCycling = OFF. `,` and `.` are inert in-world; only the
//     map cycle path responds. User opts in if they want world cycle.
//   - RoomShapes      = ON. Preserves the current corridor / junction /
//     Platz announce flow on cluster transitions.
//   - WallSounds      = ON. Preserves the Pillar 1 wall-cue beats.
//   - AudioGlossary   = unused slot (RowKind::Submenu, not a toggle).
// Index order MUST match the Option enum.
bool s_toggles[static_cast<int>(Option::Count)] = {
    /* ExtendedCycling */ false,
    /* RoomShapes      */ true,
    /* WallSounds      */ true,
    /* HumanSubtitles  */ false,  // default off — humans have VO, TTS over
                                  // the same line clashes; aliens still
                                  // read because the suppression filter
                                  // gates on appearance_type, not on this
                                  // toggle alone.
    /* TurretAutoAim   */ false,  // "Autoaiming" — OFF by default. When ON, the
                                  // turret PERMANENTLY locks aim onto the
                                  // selected fighter (full auto-track), so the
                                  // player just selects + fires — for players
                                  // who want no challenge or have stronger
                                  // hearing impairments. The DEFAULT (toggle
                                  // off) is the always-on slight aim-magnetism
                                  // in turret_game.cpp, which keeps the minigame
                                  // skill-based but playable by ear.
    /* SkipIntros      */ false,  // unused — state lives in filesystem
                                  // (biologo.bik vs biologo.bik.disabled);
                                  // StateText + Enter both special-case
                                  // this index and call into intro_skip.
    /* CueVolume       */ false,  // unused — RowKind::Slider; value lives in
                                  // audio_bus (Get/SetGlobalCueVolumePercent).
    /* UrgentVolume    */ false,  // unused — RowKind::Slider; value lives in
                                  // prism (Get/SetUrgentVolumePercent).
    /* AudioGlossary   */ false,  // unused — RowKind::Submenu
};

// Submenu state. `s_open` flips on OpenSubMenu / Close; `s_focused`
// is the currently-selected option index while open. `s_parentPanel`
// is the panel the user came from so Close can rebind the chain.
// `s_fgAtOpen` is the engine-foreground panel captured at OpenSubMenu
// time — used as the divergence baseline so we don't false-fire on
// stack configurations that are stable from the user's perspective.
// (In-game Optionen leaves the InGameMenu strip at panels[top]; the
// strip never equals our parent but the user is still "in" the panel
// they selected.)
//
// s_glossaryOpen flips when the user presses Enter on the
// AudioGlossary row. While true, HandleInput routes Up/Down/Enter/Esc
// to the glossary-specific handlers; Esc returns to the root submenu
// rather than the parent panel. s_glossaryFocused is the per-mode
// focus index (independent of the root focus so the user returns to
// the AudioGlossary row when they Esc out).
bool   s_open            = false;
int    s_focused         = 0;
void*  s_parentPanel     = nullptr;
void*  s_fgAtOpen        = nullptr;
bool   s_glossaryOpen    = false;
int    s_glossaryFocused = 0;

// Pending glossary playback. Stamped by Enter on a glossary row;
// drained by Tick() once GetTickCount() ≥ s_pendingFireAt. The pause
// exists so the row name announcement ("Wand") finishes before the cue
// plays — without it the speech bleeds over the sample and the user
// can't cleanly correlate name with sound. 750 ms tuned by feel
// 2026-05-27: long enough to clear the speech tail at default rate,
// short enough that it doesn't feel laggy.
bool                s_pendingValid  = false;
DWORD               s_pendingFireAt = 0;
acc::audio::NavCue  s_pendingCue    = acc::audio::NavCue::DoorOpen;
constexpr DWORD     kGlossaryDelayMs = 750;

// Audition playback handle. The glossary fires its sample through a
// self-managed CExoSoundSource (non-looping, 2D) rather than
// CExoSound::PlayOneShotSound. The one-shot mixer path is muted while the
// in-game menu pauses the world (engine SetSoundMode); a directly-driven
// source uses the same channel the GUI's own click sounds use, which stays
// audible under that pause. Reused across fires — each Start() stops the
// prior sample first; Close() stops any in-flight tail.
acc::audio::LoopSource s_glossaryPreview;

enum class RowKind {
    Toggle,    // Enter flips s_toggles[idx]; speech reads "Name: state"
    Submenu,   // Enter opens a nested view; speech reads "Name" only
    Slider,    // Left/Right adjust a percent; speech reads "Name: N Prozent"
};

// Cue-volume slider step (percent per Left/Right press) and the cue used
// for the audible preview. The preview rides priority group 0xb so it
// survives the in-game Optionen pause (SetSoundMode mutes everything else),
// and plays at the slider-scaled per-source volume so the user hears the
// level they just dialled in. BeaconActive (gui_check) is a short, clean
// affirmative blip representative of the hint-sound vocabulary.
constexpr int kCueVolumeStep            = 10;
constexpr int kCueVolumePreviewGroup    = 0x0b;
constexpr acc::audio::NavCue kCueVolumePreviewCue = acc::audio::NavCue::BeaconActive;

// Slider value plumbing. Each Slider row owns its percent in a different
// subsystem (cue volume → audio_bus, spoken-announcement volume → prism);
// these two helpers route by Option so the focus/announce/adjust code paths
// stay generic. Non-slider options return 0 / no-op.
int GetSliderPercent(Option opt) {
    switch (opt) {
        case Option::CueVolume:    return acc::audio::GetGlobalCueVolumePercent();
        case Option::UrgentVolume: return prism::GetUrgentVolumePercent();
        default:                   return 0;
    }
}

void SetSliderPercent(Option opt, int percent) {
    switch (opt) {
        case Option::CueVolume:    acc::audio::SetGlobalCueVolumePercent(percent); break;
        case Option::UrgentVolume: prism::SetUrgentVolumePercent(percent);         break;
        default:                   break;
    }
}

struct OptionSpec {
    Option              option;
    acc::strings::Id    label;
    RowKind             kind;
};

constexpr OptionSpec k_options[] = {
    { Option::ExtendedCycling, acc::strings::Id::ModSettingExtendedCycling, RowKind::Toggle  },
    { Option::RoomShapes,      acc::strings::Id::ModSettingRoomShapes,      RowKind::Toggle  },
    { Option::WallSounds,      acc::strings::Id::ModSettingWallSounds,      RowKind::Toggle  },
    { Option::HumanSubtitles,  acc::strings::Id::ModSettingHumanSubtitles,  RowKind::Toggle  },
    { Option::TurretAutoAim,   acc::strings::Id::ModSettingTurretAutoAim,   RowKind::Toggle  },
    { Option::SkipIntros,      acc::strings::Id::ModSettingSkipIntros,      RowKind::Toggle  },
    { Option::CueVolume,       acc::strings::Id::ModSettingCueVolume,       RowKind::Slider  },
    { Option::UrgentVolume,    acc::strings::Id::ModSettingUrgentVolume,    RowKind::Slider  },
    { Option::AudioGlossary,   acc::strings::Id::ModSettingAudioGlossary,   RowKind::Submenu },
};
constexpr int k_optionCount = static_cast<int>(
    sizeof(k_options) / sizeof(k_options[0]));
static_assert(k_optionCount == static_cast<int>(Option::Count),
              "k_options must cover every Option enumerator");

// Stable acc_settings.ini keys for the toggles that persist across launches.
// SkipIntros is filesystem-backed (the .bik rename IS its state), and
// CueVolume / AudioGlossary aren't toggles — those return nullptr (not
// persisted through this path; CueVolume persists via audio_bus).
const char* PersistKey(Option opt) {
    switch (opt) {
        case Option::ExtendedCycling: return "ExtendedCycling";
        case Option::RoomShapes:      return "RoomShapes";
        case Option::WallSounds:      return "WallSounds";
        case Option::HumanSubtitles:  return "HumanSubtitles";
        case Option::TurretAutoAim:   return "TurretAutoAim";
        default:                      return nullptr;  // not persisted here
    }
}

// Pull persisted toggle values into s_toggles once, on first access. The
// scaffold defaults in s_toggles[] act as the fallback for absent keys, so
// behaviour on a fresh install (no file) is unchanged.
void EnsureTogglesLoaded() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < k_optionCount; ++i) {
        const char* key = PersistKey(k_options[i].option);
        if (!key) continue;
        s_toggles[i] = acc::settings::GetBool(key, s_toggles[i]);
    }
}

// Glossary entries — one row per NavCue. Order follows the NavCue
// enum so a sighted reader can cross-reference against audio_cues.h.
// Labels reuse the cycle Category* strings where the vocabulary
// already exists (Door / Npc / Container / Item / Landmark /
// Transition); Wall + the four special cues have glossary-specific
// strings since the announce paths don't name them.
struct GlossaryEntry {
    acc::audio::NavCue  cue;
    acc::strings::Id    label;
};

constexpr GlossaryEntry k_glossary[] = {
    { acc::audio::NavCue::DoorOpen,                 acc::strings::Id::GlossaryEntryDoorOpen         },
    { acc::audio::NavCue::DoorClosedMetal,          acc::strings::Id::GlossaryEntryDoorClosedMetal  },
    { acc::audio::NavCue::DoorClosedWood,           acc::strings::Id::GlossaryEntryDoorClosedWood   },
    { acc::audio::NavCue::DoorClosedStone,          acc::strings::Id::GlossaryEntryDoorClosedStone  },
    { acc::audio::NavCue::NpcCreature,              acc::strings::Id::CategoryNpc                   },
    { acc::audio::NavCue::ContainerPlaceable,       acc::strings::Id::CategoryContainer             },
    { acc::audio::NavCue::Item,                     acc::strings::Id::CategoryItem                  },
    // Landmark omitted — cue is silent (TTS-only, 2026-05-27); a glossary
    // row would play nothing on Enter and confuse the user.
    { acc::audio::NavCue::TransitionExit,           acc::strings::Id::CategoryTransition            },
    { acc::audio::NavCue::Wall,                     acc::strings::Id::GlossaryEntryWall             },
    { acc::audio::NavCue::HazardLedge,              acc::strings::Id::GlossaryEntryHazard           },
    { acc::audio::NavCue::Collision,                acc::strings::Id::GlossaryEntryCollision        },
    { acc::audio::NavCue::BeaconActive,             acc::strings::Id::GlossaryEntryBeaconActive    },
    { acc::audio::NavCue::BeaconWaypointReached,    acc::strings::Id::GlossaryEntryBeaconWaypoint  },
    { acc::audio::NavCue::BeaconDestinationReached, acc::strings::Id::GlossaryEntryBeaconDestination },
    { acc::audio::NavCue::SwoopAccelTick,           acc::strings::Id::GlossaryEntrySwoopAccelTick     },
    { acc::audio::NavCue::SwoopAccelpadBoost,       acc::strings::Id::GlossaryEntrySwoopAccelpadBoost },
    { acc::audio::NavCue::SwoopObstacleWarn,        acc::strings::Id::GlossaryEntrySwoopObstacleWarn  },
    { acc::audio::NavCue::SwoopWallImpact,          acc::strings::Id::GlossaryEntrySwoopWallImpact    },
};
constexpr int k_glossaryCount = static_cast<int>(
    sizeof(k_glossary) / sizeof(k_glossary[0]));

const char* StateText(int optionIdx) {
    // SkipIntros: state is the filesystem (biologo.bik vs .disabled),
    // not s_toggles. Read the canonical state every call so the UI
    // reflects external changes (installer apply between sessions,
    // manual user rename, etc.).
    if (optionIdx >= 0 && optionIdx < static_cast<int>(Option::Count) &&
        k_options[optionIdx].option == Option::SkipIntros) {
        auto st = acc::intro_skip::CurrentState();
        // Unknown maps to Off so the toggle UI still works — the Enter
        // handler will report failure if the rename can't go through.
        bool on = (st == acc::intro_skip::State::Disabled);
        return acc::strings::Get(on ? acc::strings::Id::ModSettingStateOn
                                    : acc::strings::Id::ModSettingStateOff);
    }
    bool on = s_toggles[optionIdx];
    return acc::strings::Get(on ? acc::strings::Id::ModSettingStateOn
                                : acc::strings::Id::ModSettingStateOff);
}

// Speak the current focused option. Toggle rows read as "Name: state";
// Submenu rows read just "Name" (no toggle state to compose with).
// Always uses Speak(interrupt=true) — normal NVDA / screen-reader speech
// with previous-utterance interrupt, NOT SAPI urgent. Per-feedback
// 2026-05-26: SpeakUrgent is reserved for cross-cancel events
// (compass turns, etc.); UI navigation should stay on the normal speech
// bus so it batches with chain step speech and respects the user's
// screen-reader rate / voice.
void SpeakFocusedOption() {
    if (s_focused < 0 || s_focused >= k_optionCount) return;
    const auto& row = k_options[s_focused];
    const char* name = acc::strings::Get(row.label);
    char line[160];
    if (row.kind == RowKind::Toggle) {
        const char* st = StateText(s_focused);
        snprintf(line, sizeof(line),
                 acc::strings::Get(acc::strings::Id::FmtModSettingOption),
                 name, st);
    } else if (row.kind == RowKind::Slider) {
        snprintf(line, sizeof(line),
                 acc::strings::Get(acc::strings::Id::FmtModSettingSlider),
                 name, GetSliderPercent(row.option));
    } else {
        snprintf(line, sizeof(line), "%s", name);
    }
    prism::Speak(line, /*interrupt=*/true);
    acclog::Write("ModSettings", "speak focused idx=%d \"%s\"",
                  s_focused, line);
}

// Speak the current focused glossary entry — just the localised name.
// State / cue resref aren't spoken; the user hears the cue itself one
// second after Enter, which is the meaningful signal.
void SpeakFocusedGlossaryEntry() {
    if (s_glossaryFocused < 0 || s_glossaryFocused >= k_glossaryCount) return;
    const char* name =
        acc::strings::Get(k_glossary[s_glossaryFocused].label);
    prism::Speak(name, /*interrupt=*/true);
    acclog::Write("ModSettings", "glossary speak focused idx=%d \"%s\"",
                  s_glossaryFocused, name);
}

// Cancel any pending delayed glossary playback. Called from every
// glossary navigation / close path so a queued cue doesn't fire after
// the user has moved on.
void CancelPendingGlossaryCue() {
    if (!s_pendingValid) return;
    acclog::Write("ModSettings", "glossary cancel pending cue=%d",
                  static_cast<int>(s_pendingCue));
    s_pendingValid  = false;
    s_pendingFireAt = 0;
}

// True iff the engine pushed a new panel on top of whatever was
// foreground when the submenu opened — typically a MessageBoxModal
// raised by Alt+F4 / quit-confirm, but also any other engine modal
// the user hits while the submenu is logically focused. Also true if
// the saved parent panel itself disappeared (engine teardown after
// Spiel beenden, save reload, etc.).
//
// Baseline is the snapshot taken at OpenSubMenu time, NOT s_parentPanel:
// the In-Game Optionen flow keeps the InGameMenu strip pinned at
// panels[top] so `GetForegroundPanel != parent` is the steady state
// inside that panel. We only care about the foreground CHANGING after
// the user entered the submenu (modal push, parent dismissed, etc.).
bool ForegroundDivergedFromParent() {
    if (!s_parentPanel) return false;
    if (!acc::engine::IsPanelInManager(s_parentPanel)) return true;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!fg) return false;
    if (s_fgAtOpen == nullptr) return false;  // no baseline yet
    return fg != s_fgAtOpen;
}

}  // namespace

void* GetRootAnchor() {
    return &s_rootSentinel;
}

bool IsRootAnchor(void* control) {
    return control == &s_rootSentinel;
}

void ForEachRootAnchor(void* panel,
                       bool (*callback)(void* sentinel, int sortCx, int sortCy,
                                        void* userData),
                       void* userData) {
    if (!panel || !callback) return;
    auto kind = acc::engine::IdentifyPanel(panel);
    if (kind != acc::engine::PanelKind::InGameOptions &&
        kind != acc::engine::PanelKind::MainMenuOptions) {
        return;
    }
    // sortCy = a very large synthetic y. The chain's insertion sort is
    // by cy ascending; placing the virtual entry at y=9000 lands it at
    // the end of the chain — below every real sub-screen button and
    // (on InGameOptions) below the bottom-row "Schliess." / "Spiel
    // beenden" buttons. User hears the existing options first on
    // Down-arrow, then "Mod settings" as the last stop. Top-of-chain
    // wrap (Home) still works.
    //
    // sortCx = 180 (matches the left-aligned column the engine paints
    // every other Options button in — observed in the chain dump at
    // patch-20260520-115529.log). Keeps the cursor warp on chain step
    // visually consistent for a sighted observer.
    callback(&s_rootSentinel, /*sortCx=*/180, /*sortCy=*/9000, userData);
}

bool ExtractRootLabel(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    const char* label =
        acc::strings::Get(acc::strings::Id::ModSettingsRootLabel);
    if (!label) {
        outBuf[0] = '\0';
        return false;
    }
    size_t i = 0;
    for (; i + 1 < bufSize && label[i]; ++i) outBuf[i] = label[i];
    outBuf[i] = '\0';
    return i > 0;
}

bool IsOpen() {
    return s_open;
}

void OpenSubMenu(void* parentPanel) {
    EnsureTogglesLoaded();
    s_open        = true;
    s_focused     = 0;
    s_parentPanel = parentPanel;
    // Snapshot the engine-foreground panel right now. Subsequent
    // HandleInput calls compare against this baseline so we only
    // auto-close on a real foreground push (modal popup, parent
    // teardown) and not on the stable strip-on-top arrangement the
    // In-Game Optionen panel runs under.
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    s_fgAtOpen = mgr ? acc::engine::GetForegroundPanel(mgr) : nullptr;
    acclog::Write("ModSettings",
                  "submenu opened (parent=%p fgAtOpen=%p)",
                  parentPanel, s_fgAtOpen);
    // Two-part open speech: "Mod settings opened" then the first
    // option. Speak(interrupt=true) on the title cuts the chain-step
    // tail without going through SAPI urgent (reserved for cross-
    // cancel cues); the option follow-up uses interrupt=false so it
    // queues right after.
    prism::Speak(
        acc::strings::Get(acc::strings::Id::ModSettingsOpened),
        /*interrupt=*/true);
    const char* name = acc::strings::Get(k_options[s_focused].label);
    const char* st   = StateText(s_focused);
    char line[160];
    snprintf(line, sizeof(line),
             acc::strings::Get(acc::strings::Id::FmtModSettingOption),
             name, st);
    prism::Speak(line, /*interrupt=*/false);
}

void Close() {
    if (!s_open) return;
    acclog::Write("ModSettings", "submenu closed (parent=%p)",
                  s_parentPanel);
    s_open            = false;
    s_focused         = 0;
    s_glossaryOpen    = false;
    s_glossaryFocused = 0;
    CancelPendingGlossaryCue();
    void* parent = s_parentPanel;
    s_parentPanel = nullptr;
    s_fgAtOpen    = nullptr;
    prism::Speak(
        acc::strings::Get(acc::strings::Id::ModSettingsClosed),
        /*interrupt=*/true);
    // Rebind the chain on the parent panel so the next arrow press
    // walks the real options again. ValidateChainPanel-style guard:
    // skip the rebind if the parent has been freed under us (extremely
    // unlikely while the submenu is open, but cheap to check).
    if (parent && acc::engine::IsPanelInManager(parent)) {
        acc::menus::chain::RebindChain(parent);
    }
}

// Silent auto-close used when the engine pushes a new panel
// (MessageBoxModal etc.) in front of our parent. Unlike Close(), this
// path doesn't speak the "closed" cue — the surfacing panel will
// announce itself through the existing menu-monitor speech path, and
// we don't want to step on its cue. Also skips the parent-rebind
// because the chain rebuilder is going to fire anyway when the new
// panel becomes foreground.
void AutoCloseSilent() {
    if (!s_open) return;
    acclog::Write("ModSettings",
                  "auto-close (foreground diverged from parent=%p)",
                  s_parentPanel);
    s_open            = false;
    s_focused         = 0;
    s_glossaryOpen    = false;
    s_glossaryFocused = 0;
    CancelPendingGlossaryCue();
    s_parentPanel = nullptr;
    s_fgAtOpen    = nullptr;
}

namespace {

// Open the nested Audio glossary submenu and announce its title + first
// row. Called from HandleInput when the user presses Enter on the
// AudioGlossary row in the root submenu.
void OpenGlossarySubMenu() {
    s_glossaryOpen    = true;
    s_glossaryFocused = 0;
    CancelPendingGlossaryCue();
    acclog::Write("ModSettings", "glossary opened");
    prism::Speak(
        acc::strings::Get(acc::strings::Id::ModSettingsAudioGlossaryOpened),
        /*interrupt=*/true);
    if (k_glossaryCount > 0) {
        prism::Speak(
            acc::strings::Get(k_glossary[s_glossaryFocused].label),
            /*interrupt=*/false);
    }
}

// Close the glossary submenu, returning the user to the root. Speaks
// the root row they came from so they have an audible anchor after the
// nested Esc.
void CloseGlossarySubMenu() {
    if (!s_glossaryOpen) return;
    acclog::Write("ModSettings", "glossary closed");
    s_glossaryOpen    = false;
    s_glossaryFocused = 0;
    CancelPendingGlossaryCue();
    s_glossaryPreview.Stop();  // silence any in-flight audition on close
    // Re-announce the root focus (the AudioGlossary row) so the user
    // knows they're back at the outer level. The root focus is
    // unchanged from before — it was the AudioGlossary row that was
    // Enter'd in the first place.
    SpeakFocusedOption();
}

// Adjust the focused Slider row by delta percent (clamped 0..100),
// re-announce "Name: N Prozent", and play an audible preview at the new
// level so the user hears the loudness they just dialled in. The preview
// is per-slider:
//   CueVolume    — a hint-sound cue on priority group 0xb (survives the
//                  in-game Optionen pause), scaled to the new per-source
//                  level. Reuses the glossary preview handle.
//   UrgentVolume — a short SAPI urgent utterance, which already carries
//                  the new volume (SetUrgentVolumePercent applied it to the
//                  SAPI backend), letting the user gauge spoken-announcement
//                  loudness directly.
// No-op on a non-slider focus.
void AdjustSlider(int delta) {
    if (s_focused < 0 || s_focused >= k_optionCount) return;
    const Option opt = k_options[s_focused].option;
    if (k_options[s_focused].kind != RowKind::Slider) return;

    int next = GetSliderPercent(opt) + delta;
    if (next < 0)   next = 0;
    if (next > 100) next = 100;
    SetSliderPercent(opt, next);
    acclog::Write("ModSettings", "slider %d adjust delta=%+d -> %d%%",
                  static_cast<int>(opt), delta, next);
    SpeakFocusedOption();

    if (opt == Option::CueVolume) {
        if (next > 0) {
            int previewByte = 127 * next / 100;
            const Vector kCentre = { 0.0f, 0.0f, 0.0f };
            s_glossaryPreview.Start(
                acc::audio::GetNavCueResref(kCueVolumePreviewCue), kCentre,
                /*looping=*/false, /*spatial=*/false,
                /*priorityGroup=*/kCueVolumePreviewGroup,
                /*volumeByte=*/previewByte);
        } else {
            s_glossaryPreview.Stop();  // muted — silence any in-flight tail
        }
    } else if (opt == Option::UrgentVolume) {
        // SAPI sample at the new level. At 0% it's inaudible by design —
        // that IS the preview (the user hears nothing, confirming mute).
        prism::SpeakUrgent(
            acc::strings::Get(acc::strings::Id::ModSettingUrgentVolumePreview));
    }
}

bool HandleInputRoot(int keyCode) {
    // Left / Right: adjust the focused Slider row; ignored on non-slider
    // rows (they fall through to the parent-input block in HandleInput,
    // which consumes them so they don't leak to the engine).
    if (keyCode == kInputNavLeft || keyCode == kInputNavRight) {
        if (s_focused >= 0 && s_focused < k_optionCount &&
            k_options[s_focused].kind == RowKind::Slider) {
            AdjustSlider(keyCode == kInputNavRight ? kCueVolumeStep
                                                   : -kCueVolumeStep);
            return true;
        }
        return false;
    }
    // Up / Down: step focus with end-clamp (no wrap — sighted "list
    // box" semantics match Optionen panels above and below).
    if (keyCode == kInputNavUp) {
        if (k_optionCount <= 0) return true;
        if (s_focused > 0) --s_focused;
        SpeakFocusedOption();
        return true;
    }
    if (keyCode == kInputNavDown) {
        if (k_optionCount <= 0) return true;
        if (s_focused < k_optionCount - 1) ++s_focused;
        SpeakFocusedOption();
        return true;
    }
    // Enter: Toggle rows flip + re-announce; Submenu rows pivot to the
    // nested view.
    if (keyCode == kInputEnter1 || keyCode == kInputEnter2) {
        if (s_focused < 0 || s_focused >= k_optionCount) return true;
        const auto& row = k_options[s_focused];
        if (row.kind == RowKind::Submenu) {
            if (row.option == Option::AudioGlossary) {
                OpenGlossarySubMenu();
            }
            return true;
        }
        // Slider: Enter replays the preview at the current level (Left/
        // Right do the actual adjusting). delta=0 keeps the value put.
        if (row.kind == RowKind::Slider) {
            AdjustSlider(0);
            return true;
        }
        // SkipIntros: filesystem rename instead of s_toggles flip.
        // Speak "Name: state" first (matches other toggles' rhythm)
        // and then a side-effect cue clarifying "takes effect on next
        // launch". Failure path speaks the toggle-failed string.
        if (row.option == Option::SkipIntros) {
            auto before = acc::intro_skip::CurrentState();
            bool wantDisable =
                (before != acc::intro_skip::State::Disabled);
            bool ok = acc::intro_skip::SetDisabled(wantDisable);
            acclog::Write("ModSettings",
                "toggle SkipIntros: target=%s ok=%d",
                wantDisable ? "disabled" : "enabled", ok ? 1 : 0);
            SpeakFocusedOption();
            const char* followup = !ok
                ? acc::strings::Get(acc::strings::Id::ModSettingSkipIntrosToggleFailed)
                : (wantDisable
                    ? acc::strings::Get(acc::strings::Id::ModSettingSkipIntrosOnNextLaunch)
                    : acc::strings::Get(acc::strings::Id::ModSettingPlayIntrosOnNextLaunch));
            prism::Speak(followup, /*interrupt=*/false);
            return true;
        }
        s_toggles[s_focused] = !s_toggles[s_focused];
        if (const char* key = PersistKey(k_options[s_focused].option)) {
            acc::settings::SetBool(key, s_toggles[s_focused]);
        }
        acclog::Write("ModSettings", "toggle idx=%d new=%d",
                      s_focused, s_toggles[s_focused] ? 1 : 0);
        SpeakFocusedOption();
        return true;
    }
    // Esc: close back to the parent panel.
    if (keyCode == kInputEsc1 || keyCode == kInputEsc2) {
        Close();
        return true;
    }
    return false;
}

bool HandleInputGlossary(int keyCode) {
    if (keyCode == kInputNavUp) {
        if (k_glossaryCount <= 0) return true;
        if (s_glossaryFocused > 0) --s_glossaryFocused;
        CancelPendingGlossaryCue();
        SpeakFocusedGlossaryEntry();
        return true;
    }
    if (keyCode == kInputNavDown) {
        if (k_glossaryCount <= 0) return true;
        if (s_glossaryFocused < k_glossaryCount - 1) ++s_glossaryFocused;
        CancelPendingGlossaryCue();
        SpeakFocusedGlossaryEntry();
        return true;
    }
    // Enter: arm the 1 s delayed playback. Re-pressing Enter replaces
    // the pending deadline so a held / re-mashed key just shifts the
    // fire-at forward; the user always hears the cue exactly once,
    // 1 s after the most recent Enter.
    if (keyCode == kInputEnter1 || keyCode == kInputEnter2) {
        if (s_glossaryFocused < 0 || s_glossaryFocused >= k_glossaryCount) {
            return true;
        }
        s_pendingCue    = k_glossary[s_glossaryFocused].cue;
        s_pendingFireAt = GetTickCount() + kGlossaryDelayMs;
        s_pendingValid  = true;
        acclog::Write("ModSettings",
                      "glossary armed cue=%d delay=%lums (idx=%d)",
                      static_cast<int>(s_pendingCue),
                      (unsigned long)kGlossaryDelayMs,
                      s_glossaryFocused);
        return true;
    }
    // Esc: return to the root submenu (NOT the parent panel).
    if (keyCode == kInputEsc1 || keyCode == kInputEsc2) {
        CloseGlossarySubMenu();
        return true;
    }
    return false;
}

}  // namespace

bool HandleInput(int keyCode) {
    if (!s_open) return false;
    // Auto-close on foreground divergence — the engine pushed a new
    // panel (MessageBoxModal from Alt+F4 quit-confirm, etc.) in front
    // of our parent while we held input control. Releasing now lets
    // the press flow into the modal's own dispatcher; we'd otherwise
    // strand the user inside the submenu with no way to reach the
    // popup without Esc-closing us first. Verified scenario:
    // user-reported Alt+F4 popup unreachable until Esc.
    if (ForegroundDivergedFromParent()) {
        AutoCloseSilent();
        return false;
    }

    bool handled = s_glossaryOpen
        ? HandleInputGlossary(keyCode)
        : HandleInputRoot(keyCode);
    if (handled) return true;

    // Block every other GUI-key press from reaching the parent panel
    // while either submenu is logically focused. Without this, e.g.
    // Left / Right would dispatch through the chain's cycle-arrow path
    // and mutate state on the parent (sub-screen-button hover etc.)
    // while the user thinks they're inside Mod settings.
    switch (keyCode) {
    case kInputNavLeft:
    case kInputNavRight:
    case kInputHome:
    case kInputEnd:
    case kInputActivate:
        return true;
    default:
        // Unmapped scancodes (in-world hotkeys etc.) fall through. The
        // submenu is a UI-only state; we don't want to break the user's
        // ability to e.g. cycle volume bus or any other global hotkey
        // while it happens to be open.
        return false;
    }
}

void Tick() {
    if (!s_pendingValid) return;
    DWORD now = GetTickCount();
    // GetTickCount wraps every ~49 days. Use signed-subtract semantics
    // so the comparison is wrap-safe.
    if (static_cast<int32_t>(now - s_pendingFireAt) < 0) return;
    acc::audio::NavCue cue = s_pendingCue;
    s_pendingValid = false;
    const char* resref = acc::audio::GetNavCueResref(cue);
    // Audition through a self-managed CExoSoundSource (non-looping, 2D)
    // rather than CExoSound::PlayOneShotSound. 2D + non-spatial so the
    // sample plays centred, matching how the main-menu glossary sounds —
    // the user is auditioning the SOUND, not a positioned cue, and may want
    // to hear e.g. "Wall" even with the wall-sound toggle off (no per-fire
    // IsCueEnabled gating here).
    //
    // Priority group 0xb is the lever that makes it audible in-game: the
    // in-game menu calls SetSoundMode(4) on open, whose PauseAllSounds pass
    // mutes every source EXCEPT priority groups 1/2/0xb. 0xb is the group
    // the engine's own GUI click sounds use (CSWGuiManager::LoadGuiSounds),
    // so the audition rides the same exemption and stays audible under the
    // pause. (The default group 0x17 is muted — that was the silent bug.)
    const Vector kCentre = { 0.0f, 0.0f, 0.0f };
    constexpr int kGuiPriorityGroup = 0xb;
    bool ok = s_glossaryPreview.Start(resref, kCentre,
                                      /*looping=*/false, /*spatial=*/false,
                                      /*priorityGroup=*/kGuiPriorityGroup);
    acclog::Write("ModSettings",
                  "glossary fire cue=%d resref=\"%s\" played=%d",
                  static_cast<int>(cue), resref, ok ? 1 : 0);
}

bool GetToggle(Option option) {
    EnsureTogglesLoaded();
    int idx = static_cast<int>(option);
    if (idx < 0 || idx >= k_optionCount) return false;
    // AudioGlossary is a submenu pivot, not a toggle. Return false
    // unconditionally so a misuse from a downstream consumer doesn't
    // read the unused s_toggles slot as meaningful state.
    if (k_options[idx].kind != RowKind::Toggle) return false;
    return s_toggles[idx];
}

}  // namespace acc::menus::modsettings
