// KOTOR 2 gamepad input — see pad_input.h for why this is one module and not
// a pad case scattered through every handler.

#include "pad_input.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "combat_queue.h"
#include "cycle_input.h"
#include "engine_game.h"
#include "engine_input.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "examine_view.h"
#include "help.h"
#include "hotkeys.h"         // PadPress — reuse the keyboard actions' pollers
#include "interact_dispatch.h"
#include "log.h"
#include "map_ui_cursor.h"   // IsActive — the map screen owns the left stick
#include "menus_chain.h"     // g_chain* — which Quick Menu entry is focused
#include "narrated_target.h" // TryGet — is there a target to open the menu on?
#include "prism.h"           // Silence — the cancel a pad press has no
                             // keystroke to trigger
#include "strings.h"
#include "unified_action_menu.h"

namespace acc::pad {

namespace {

// ---------------------------------------------------------------------------
// XInput, loaded at run time
// ---------------------------------------------------------------------------
// Minimal local mirrors of the two XInput structures. Declared here rather
// than including <XInput.h> so the patch keeps building against any SDK
// arrangement and so nothing can drag in a static import — the whole point of
// resolving this at run time is that a missing xinput DLL must degrade to "no
// triggers", never to a loader failure.
struct XiGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct XiState {
    DWORD     dwPacketNumber;
    XiGamepad Gamepad;
};
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XiState* pState);

// Firmer than XInput's own XINPUT_GAMEPAD_TRIGGER_THRESHOLD (30 of 255): the
// triggers here are a MODIFIER, and a modifier that engages on a resting
// finger would silently reroute the D-Pad. A deliberate pull clears this
// easily.
constexpr BYTE kTriggerHeld = 60;

// While no pad is found, retry at this cadence rather than every frame —
// XInputGetState on an empty slot is a comparatively expensive call.
constexpr ULONGLONG kScanIntervalMs = 2000;

HMODULE            g_xinputDll   = nullptr;
PFN_XInputGetState g_xinputGet   = nullptr;
bool               g_xinputTried = false;
int                g_padSlot     = -1;   // last slot that answered
ULONGLONG          g_lastScanMs  = 0;

bool g_ltHeld = false;
bool g_rtHeld = false;

// A trigger does two jobs: alone it is a button, held it is a modifier for the
// shoulder next to it (and for the other trigger). Both cannot fire on the
// PRESS — LT+LB would then also switch the D-Pad mode on the way in. So each
// trigger's solo job fires on RELEASE, and any chord taken during the hold
// cancels it. There is no timing rule and nothing for the user to learn: LT
// switches the D-Pad, LT+LB drops a beacon, and the two never collide.
bool g_ltChorded = false;
bool g_rtChorded = false;

// Sticky: once a pad has been seen this session we keep advertising it. The
// engine enumerates game controllers exactly once at input init, so a pad that
// was present at launch stays the relevant one even if XInput briefly stops
// answering; and a pad that never reaches XInput at all still announces itself
// through the manager events (see NoteSeen).
bool g_padSeen = false;

// Only the two engine-native-discriminated event shapes are allowed to set
// this: a stick axis carrying val = +/-1 (the engine's own preamble keys on
// exactly that), and a D-Pad code (F9..F12, unbound in stock [Keymapping], and
// twin-checked on top). The face and shoulder codes are shared with C, D and
// the function keys — codes an ordinary keyboard-only KOTOR 2 session really
// does produce — and letting those claim a controller would put a Controller
// section in the F1 help of a player who has no controller.
void NoteSeen() { g_padSeen = true; }

void EnsureXInput() {
    if (g_xinputTried) return;
    g_xinputTried = true;
    // Newest first. 9_1_0 is the one that ships in-box on every supported
    // Windows, so it is the guaranteed fallback.
    static const wchar_t* const kDlls[] = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll",
    };
    for (const wchar_t* name : kDlls) {
        HMODULE h = LoadLibraryW(name);
        if (!h) continue;
        auto fn = reinterpret_cast<PFN_XInputGetState>(
            GetProcAddress(h, "XInputGetState"));
        if (fn) {
            g_xinputDll = h;
            g_xinputGet = fn;
            acclog::Write("Pad", "XInput bound via %ls", name);
            return;
        }
        FreeLibrary(h);
    }
    acclog::Write("Pad", "no XInput DLL resolved — triggers unavailable "
                         "(D-Pad bindings still work)");
}

// ---------------------------------------------------------------------------
// Left-stick movement
// ---------------------------------------------------------------------------
// XInput's own left-stick dead zone is 7849 of 32767. This sits above it: the
// question here is "is the player commanding translation", not "has the stick
// left centre", and a resting thumb on a worn stick must not read as a walk.
constexpr int kStickDeadZone   = 12000;
constexpr int kStickDeadZoneSq = kStickDeadZone * kStickDeadZone;

bool  g_stickMoving = false;
float g_stickX      = 0.0f;   // normalised, +1 = right
float g_stickY      = 0.0f;   // normalised, +1 = up (XInput's own convention)

// ---------------------------------------------------------------------------
// Pad-vs-keyboard discrimination
// ---------------------------------------------------------------------------
bool PhysDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// The keyboard row each shared code belongs to. If that physical key is down
// right now, the event is the keyboard's, not the pad's.
//
// The engine drops unbound scancodes before our hook, and stock [Keymapping]
// binds none of F9..F12 — so in practice these codes can only be the pad. The
// physical-key test closes the remaining case (a player who bound F9..F12 in
// the game's own key-mapping screen) at the cost of one OS call per event.
int KeyboardTwinVk(int code) {
    switch (code) {
        case kPadDpadLeft:   return VK_F9;
        case kPadDpadRight:  return VK_F10;
        case kPadDpadUp:     return VK_F11;
        case kPadDpadDown:   return VK_F12;
        case kPadStickVert:  return 'A';
        case kPadStickHoriz: return 'B';
        case kPadButtonA:    return VK_F1;
        case kPadButtonB:    return VK_F2;
        case kPadButtonX:    return VK_F3;
        case kPadButtonY:    return VK_F4;
        case kPadShoulderL:  return 'C';
        case kPadShoulderR:  return 'D';
        case kPadBack:       return VK_F8;
        default:             return 0;
    }
}

// ---------------------------------------------------------------------------
// Axis release pairing
// ---------------------------------------------------------------------------
// Codes 51 / 52 send val = 0 on centring and that release carries no
// direction. Keep the last direction per axis so the release can be rewritten
// to the SAME logical code the press became — otherwise the press-release
// pairing in OnHandleInputEvent mismatches and a later real press gets
// swallowed. The D-Pad needs none of this: it sends val = 1 only, never a
// release.
int g_axisLatch[2] = {0, 0};   // [0] = vertical (51), [1] = horizontal (52)

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------
// "World" means the pad is driving the game, not a panel: the player exists
// and no engine panel owns the foreground. Everything else — title screen,
// chargen, dialog, the in-game menu strip, any modal — is "menu", where the
// pad's job is simply to be the keyboard.
bool InWorldSurface() {
    Vector p;
    if (!acc::engine::GetPlayerPosition(p)) return false;
    return !acc::engine::IsForegroundUiBlocking();
}

// The cancel a pad press has no keystroke to trigger.
//
// Every menu announcement in the mod speaks with interrupt=false — deliberately,
// because the screen reader itself cuts queued speech the moment the user
// presses a key, and letting it do that keeps multi-part announcements (title
// then focus) able to queue. A controller press produces no keystroke, so
// nothing cancels and the entries pile up: the user hears the previous row
// finish before the new one starts, worsening with every press.
//
// So issue the cancel ourselves, at the one place that knows the input came
// from a pad. This is exactly what the screen reader would have done, and it
// leaves every announce path unchanged — the alternative, flipping dozens of
// call sites to interrupt=true, would also change keyboard behaviour and break
// the announcements that intentionally queue.
void NoteNavPress() {
    prism::Silence();
}

// The Y Quick Menu's eighth entry is "Help", and what the engine opens for it
// is CSWGuiHelpPanel — a bare picture of a controller, which tells a blind
// player nothing at all. It is the one entry on that panel whose engine
// behaviour is worthless to this mod's users, and the panel is pad-only, so
// claiming it costs a keyboard player nothing. Redirect it to the mod's own
// key list, which is what the entry already promises. That also gives the pad
// a route into the help list without spending a binding on one.
//
// Identified by position: the decorative filter (menus_chain) leaves exactly
// the eight captioned entries in gamepad.txt's own order — Menus, Party
// Leader, Solo/Party, Stealth, Quick Save, Free Look, Switch Weapons, Help —
// and Help is the last. The count check is the guard: if the filter ever lets
// a decorative quad through, the indices shift, and declining is much better
// than firing the wrong entry.
constexpr int kQuickMenuEntryCount = 8;

bool QuickMenuHelpFocused() {
    void* panel = acc::menus::chain::g_chainPanel;
    if (!panel) return false;
    if (acc::engine::IdentifyPanel(panel) !=
        acc::engine::PanelKind::GamepadQuickMenu) {
        return false;
    }
    if (acc::menus::chain::g_chainCount != kQuickMenuEntryCount) {
        acclog::Trace("Pad", "Quick Menu chain has %d entries, expected %d — "
                             "leaving Help to the engine",
                      acc::menus::chain::g_chainCount, kQuickMenuEntryCount);
        return false;
    }
    return acc::menus::chain::g_chainIndex == kQuickMenuEntryCount - 1;
}

// Route a logical nav / activate / cancel code into whichever in-DLL overlay
// currently owns input. Priority mirrors input_poll_router's Win32 ordering
// exactly, so pad and keyboard resolve a contested key the same way. Returns
// true when an overlay took it.
bool RouteToOverlay(int logical) {
    // The F1 list is global (it opens over menus too) and owns its keys
    // outright while up, so it gets first refusal on every surface.
    if (acc::help::HandleNavCode(logical)) return true;

    if (!InWorldSurface()) return false;

    if (acc::examine_view::IsActive()) {
        return acc::examine_view::HandleInputEvent(logical, 1);
    }
    if (acc::combat::queue::IsActive()) {
        return acc::combat::queue::HandleInputEvent(logical, 1);
    }
    if (acc::unified_menu::IsActive() && !acc::unified_menu::IsSuspended()) {
        return acc::unified_menu::HandleInputEvent(logical, 1);
    }
    return false;
}

// ---------------------------------------------------------------------------
// The D-Pad, and what it is currently for
// ---------------------------------------------------------------------------
// The D-Pad is not free in the world — it drives the engine's own
// CSWGuiActionMenuIos, whose level 1 (category) is D-Pad left/right and whose
// level 2 (entry) is D-Pad up/down, with A confirming. The earlier reading
// that it was inert was a blind test round watching a silent, purely visual
// highlight move; every round since has consumed the codes outright, which is
// why Pad.ActionMenu never logged a line.
//
// That idiom is worth keeping, because it is a better one than the mod had:
// the action surface is always THERE, under the thumb, over a running world,
// instead of being a menu the player opens, picks from and closes. So the mod
// adopts it rather than working around it — the D-Pad drives the mod's own
// unified action menu in LIVE mode (unified_action_menu.h), which speaks and
// which already drives the engine's real action machinery.
//
// One D-Pad, two jobs that each want all four directions — FINDING a thing and
// ACTING on it. The left trigger switches between them:
//
//   Cycle mode        left/right  previous / next object
//                     up/down     previous / next category
//                     A           the default action on the focused thing
//   Action-menu mode  left/right  previous / next action category
//                     up/down     previous / next entry
//                     A           fire the selected entry
//
// Cycle mode is the default: finding a target comes before acting on one.
enum class DpadMode { Cycle, ActionMenu };
DpadMode g_dpadMode = DpadMode::Cycle;

// Open the unified action menu in live mode on whatever is focused. Returns
// true when it is up afterwards.
//
// With no focused target the target categories are empty, so fall through to
// the personal block — medpacs and stims are exactly what a player wants when
// nothing is targeted, and refusing to open would be the wrong answer.
bool OpenLiveActionMenu() {
    if (acc::unified_menu::IsActive() && !acc::unified_menu::IsSuspended()) {
        return true;
    }
    acc::unified_menu::RequestLiveArm(true);

    acc::narrated_target::Slot slot{};
    const bool haveTarget = acc::narrated_target::TryGet(slot) && !slot.isMapPin;
    if (haveTarget) {
        // The Shift+Enter gesture: target categories first, and Left still
        // crosses into the personal block from there.
        acc::interact::InteractNarratedTarget(/*forceRadial=*/true);
    }
    if (!acc::unified_menu::IsActive()) {
        // No populated target row (a corpse, a non-combat NPC), or no target
        // at all. The personal block is still worth opening — wherever it has
        // something, which is NOT necessarily column 0: on a droid that column
        // is Force Powers and is always empty.
        acc::unified_menu::OpenAnyPersonal();
    }
    const bool up = acc::unified_menu::IsActive();
    // Both entry points declined (every category drained). Drop the request
    // so it cannot turn a later keyboard open into a live one.
    if (!up) acc::unified_menu::RequestLiveArm(false);
    acclog::Write("Pad", "live action menu open target=%d up=%d",
                  haveTarget ? 1 : 0, up ? 1 : 0);
    return up;
}

// Action-menu mode is on but the menu is not up yet. Called from the manager
// input hook — and ONLY from there. The unified menu's hard rule is that it is
// populated from the engine's input-dispatch context and nowhere else: arming
// it from a tick made the engine synthesise a phantom confirm one tick later
// that fired the menu's first entry. The trigger poll lives in Tick(), i.e. in
// OnUpdate, so the mode switch may set the mode but must NOT open the menu.
// The first in-world pad press does that instead, here.
bool EnsureLiveMenuForMode() {
    if (g_dpadMode != DpadMode::ActionMenu) return false;
    if (acc::unified_menu::IsActive() && !acc::unified_menu::IsSuspended()) {
        return false;   // already up — the caller's normal routing handles it
    }
    return OpenLiveActionMenu();
}

// Switch what the D-Pad is for, and say so.
//
// The mode word is the WHOLE announcement, in both directions. Chaining the
// focused object onto the cycle direction was tried and does not work: the
// cycle's own announce speaks with interrupt=true (correctly — it is what the
// `-` key does), so it cancels the mode word rather than queueing under it,
// and the user hears the object but never learns the mode's name. One press,
// one cue. The action menu likewise speaks only its name here; the menu opens
// on the first D-Pad or A press and speaks its category then, for the
// input-context reason above.
void SetDpadMode(DpadMode mode) {
    g_dpadMode = mode;
    const bool actionMenu = (mode == DpadMode::ActionMenu);
    acclog::Write("Pad", "D-Pad mode -> %s",
                  actionMenu ? "action menu" : "cycle");
    if (!actionMenu && acc::unified_menu::IsLive()) {
        // Leaving action-menu mode closes the live menu — a menu nothing can
        // navigate is worse than no menu, and ForceDisarm is the quiet close
        // (live mode never held a pause, so there is nothing to resume). A
        // menu the user opened from the KEYBOARD is left alone: it is not ours
        // to close, and IsLive() is what tells the two apart.
        acc::unified_menu::ForceDisarm("pad-mode-switch");
    }
    prism::Speak(acc::strings::Get(actionMenu
                                       ? acc::strings::Id::PadModeActionMenu
                                       : acc::strings::Id::PadModeCycle),
                 /*interrupt=*/true);
}

void ToggleDpadMode() {
    SetDpadMode(g_dpadMode == DpadMode::Cycle ? DpadMode::ActionMenu
                                              : DpadMode::Cycle);
}

// In-world D-Pad dispatch for CYCLE mode. Action-menu mode never reaches here:
// the live menu is an overlay, so RouteToOverlay claims the code first.
// Returns true when the binding fired (the caller consumes).
bool DispatchWorldDpad(int code) {
    using PA = acc::cycle_input::PadAction;

    // Action-menu mode with no menu up — either the mode was just switched on
    // (the trigger poll cannot open it, see EnsureLiveMenuForMode) or the user
    // fired the last entry in a category that then drained. Open on the press
    // rather than silently handing the D-Pad back to the cycle: the mode is
    // what the user last chose, and it must stay true.
    if (EnsureLiveMenuForMode()) return true;
    if (g_dpadMode == DpadMode::ActionMenu) {
        // Mode is on but nothing could be opened (every category empty). Say
        // nothing extra — OpenPersonal already spoke the empty-category line.
        return true;
    }

    switch (code) {
        case kPadDpadLeft:  return acc::cycle_input::DispatchPadAction(PA::ItemPrev);
        case kPadDpadRight: return acc::cycle_input::DispatchPadAction(PA::ItemNext);
        case kPadDpadUp:    return acc::cycle_input::DispatchPadAction(PA::CategoryPrev);
        case kPadDpadDown:  return acc::cycle_input::DispatchPadAction(PA::CategoryNext);
        default:            return false;
    }
}

// The logical code a pad D-Pad / stick direction stands in for.
int LogicalForDirection(int code) {
    switch (code) {
        case kPadDpadUp:    return kInputNavUp;
        case kPadDpadDown:  return kInputNavDown;
        case kPadDpadLeft:  return kInputNavLeft;
        case kPadDpadRight: return kInputNavRight;
        default:            return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

bool LeftTriggerHeld()  { return g_ltHeld; }
bool RightTriggerHeld() { return g_rtHeld; }
bool Connected()        { return g_padSeen; }

void Tick() {
    if (!acc::game::IsKotor2()) return;
    EnsureXInput();
    if (!g_xinputGet) return;

    const ULONGLONG now = GetTickCount64();

    // Slot search, throttled. Once a slot answers we stay on it; a pad that
    // stops answering drops us back into the throttled search.
    if (g_padSlot < 0) {
        if (g_lastScanMs != 0 && now - g_lastScanMs < kScanIntervalMs) return;
        g_lastScanMs = now;
        for (DWORD i = 0; i < 4; ++i) {
            XiState st = {};
            if (g_xinputGet(i, &st) == ERROR_SUCCESS) {
                g_padSlot = static_cast<int>(i);
                NoteSeen();
                acclog::Write("Pad", "XInput controller found in slot %u", i);
                break;
            }
        }
        if (g_padSlot < 0) return;
    }

    XiState st = {};
    if (g_xinputGet(static_cast<DWORD>(g_padSlot), &st) != ERROR_SUCCESS) {
        acclog::Write("Pad", "XInput slot %d stopped answering — rescanning",
                      g_padSlot);
        g_padSlot = -1;
        g_ltHeld = g_rtHeld = false;
        g_stickMoving = false;
        return;
    }

    const bool lt = st.Gamepad.bLeftTrigger  >= kTriggerHeld;
    const bool rt = st.Gamepad.bRightTrigger >= kTriggerHeld;
    if (lt != g_ltHeld || rt != g_rtHeld) {
        acclog::Write("Pad", "trigger state LT=%d RT=%d (raw %u/%u)",
                      lt ? 1 : 0, rt ? 1 : 0,
                      static_cast<unsigned>(st.Gamepad.bLeftTrigger),
                      static_cast<unsigned>(st.Gamepad.bRightTrigger));
    }
    const bool ltPressed  =  lt && !g_ltHeld;
    const bool ltReleased = !lt &&  g_ltHeld;
    const bool rtPressed  =  rt && !g_rtHeld;
    const bool rtReleased = !rt &&  g_rtHeld;
    g_ltHeld = lt;
    g_rtHeld = rt;

    if (ltPressed) g_ltChorded = false;
    if (rtPressed) g_rtChorded = false;

    // XInput answers whether or not KOTOR has focus, so the trigger ACTIONS
    // are gated on the game being foreground: a trigger pulled while the
    // player is alt-tabbed must not speak over whatever they switched to, nor
    // silently change the D-Pad's mode under them. The edge state above is
    // tracked regardless, so a chord begun in the background still cancels its
    // trigger's solo job instead of firing it on the way back — and the stick
    // sampling below stays outside the gate, because its consumers want a
    // truthful "not moving" while the game is away, not a stale "moving".
    if (acc::hotkeys::IsForegroundGame()) {
        // Both triggers — the context help ("what do the keys do on THIS
        // screen"). Fires on whichever press completes the pair, and marks
        // both holds chorded so neither release also does its solo job.
        if ((ltPressed && rt) || (rtPressed && lt)) {
            g_ltChorded = g_rtChorded = true;
            acclog::Write("Pad", "LT + RT -> context help");
            acc::help::SpeakContextHelp();
        }

        // LT alone — switch what the D-Pad is for. Only in the world: in a
        // menu the D-Pad is simply the keyboard's arrows, with nothing to
        // switch between.
        if (ltReleased && !g_ltChorded && InWorldSurface()) {
            ToggleDpadMode();
        }

        // RT alone — the exact keyboard AltGr binding: announce the facing in
        // degrees. Routed as a synthetic press so announce_degrees keeps its
        // own world-vs-map branch rather than the pad growing a second copy.
        if (rtReleased && !g_rtChorded) {
            acclog::Write("Pad", "RT -> announce facing in degrees");
            acc::hotkeys::PadPress(acc::hotkeys::Action::AnnounceDegrees);
        }
    }

    // Left stick = Move on the shipped binding chart, so its magnitude is the
    // "player is commanding translation" signal the drive-loop suppression and
    // the autowalk cancel both want. int arithmetic, not the SHORTs: the
    // squared magnitude of two full-scale axes overflows 16 bits.
    const int lx = st.Gamepad.sThumbLX;
    const int ly = st.Gamepad.sThumbLY;
    const int magSq = lx * lx + ly * ly;
    const bool moving = magSq > kStickDeadZoneSq;
    acclog::Edge("Pad.stick", moving ? 1 : 0,
                 "left stick moving=%d (x=%d y=%d)", moving ? 1 : 0, lx, ly);
    g_stickMoving = moving;
    if (moving) {
        const float mag = std::sqrt(static_cast<float>(magSq));
        g_stickX = static_cast<float>(lx) / mag;
        g_stickY = static_cast<float>(ly) / mag;
    } else {
        g_stickX = g_stickY = 0.0f;
    }
}

bool IsPadCode(int code) {
    if (!acc::game::IsKotor2()) return false;
    switch (code) {
        case kPadDpadLeft: case kPadDpadRight:
        case kPadDpadUp:   case kPadDpadDown:
        case kPadStickVert: case kPadStickHoriz:
        case kPadButtonA:  case kPadButtonB:
        case kPadButtonX:  case kPadButtonY:
        case kPadShoulderL: case kPadShoulderR:
        case kPadBack:
            return true;
        default:
            return false;
    }
}

const char* CodeHint(int code) {
    if (!acc::game::IsKotor2()) return nullptr;
    switch (code) {
        case kPadDpadLeft:   return "D-Pad left";
        case kPadDpadRight:  return "D-Pad right";
        case kPadDpadUp:     return "D-Pad up";
        case kPadDpadDown:   return "D-Pad down";
        case kPadStickVert:  return "left stick vertical";
        case kPadStickHoriz: return "left stick horizontal";
        case kPadButtonA:    return "A";
        case kPadButtonB:    return "B";
        case kPadButtonX:    return "X";
        case kPadButtonY:    return "Y";
        case kPadShoulderL:  return "LB";
        case kPadShoulderR:  return "RB";
        case kPadBack:       return "Back";
        default:             return nullptr;
    }
}

bool IsPhysicalF1() {
    if (!acc::game::IsKotor2()) return true;  // no pad path — 0x27 is F1
    return PhysDown(VK_F1);
}

bool StickMoving() { return g_stickMoving; }

bool StickVector(float& outX, float& outY) {
    if (!g_stickMoving) return false;
    outX = g_stickX;
    outY = g_stickY;
    return true;
}

bool TranslateClientEvent(int code, int value) {
    if (!acc::game::IsKotor2()) return false;
    // These three codes are ordinary engine InputIndices that a keyboard and a
    // mouse also produce. Nothing here may fire in a session that has never
    // seen a controller.
    if (!g_padSeen) return false;
    if (value == 0) return false;

    switch (code) {
        case kPadClientShoulderL:
            // Bare LB stays the engine's Cycle Target (Left); only the chord
            // is ours, and consuming it stops the engine ALSO cycling.
            if (!g_ltHeld) return false;
            g_ltChorded = true;
            acclog::Write("Pad", "LT + LB -> beacon to focus");
            acc::cycle_input::DispatchPadAction(
                acc::cycle_input::PadAction::BeaconFocus);
            return true;

        case kPadClientShoulderR:
            if (!g_rtHeld) return false;
            g_rtChorded = true;
            acclog::Write("Pad", "RT + RB -> walk to focus");
            acc::cycle_input::DispatchPadAction(
                acc::cycle_input::PadAction::WalkToFocus);
            return true;

        case kPadClientStickR:
            // R3 shares InputIndex 1 with MOUSE_BUTTON1 — the RIGHT MOUSE
            // BUTTON, i.e. mouse-look. Consuming it blindly would take
            // right-click away from every KOTOR 2 player with a pad plugged
            // in. The physical-button test is the same twin-key discriminator
            // the manager codes use: button actually down means the mouse
            // sent it, so stand down.
            if (PhysDown(VK_RBUTTON)) return false;
            acclog::Write("Pad", "right stick press -> camera orient");
            acc::hotkeys::PadPress(acc::hotkeys::Action::CameraOrient);
            return true;

        default:
            return false;
    }
}

Verdict TranslateManagerEvent(int& code, int& value) {
    if (!acc::game::IsKotor2()) return Verdict::NotPad;
    if (!IsPadCode(code)) return Verdict::NotPad;

    // A physically-held twin key means this is the keyboard's event, not the
    // pad's. Runs before anything else so a bound F9..F12 / A / B / F1 / F2
    // keeps its vanilla meaning.
    const int twinVk = KeyboardTwinVk(code);
    if (twinVk != 0 && PhysDown(twinVk)) return Verdict::NotPad;

    const int raw = code;

    // ---- Left stick -> nav ------------------------------------------------
    // The engine's own preamble keys on val == +/-1 for these two codes, so
    // that test is engine-native and cannot drift. val == 0 is the centring
    // release; it carries no direction, so it takes the latched one.
    if (code == kPadStickVert || code == kPadStickHoriz) {
        const int axis = (code == kPadStickVert) ? 0 : 1;
        int logical = 0;
        if (value == -1) {
            logical = (axis == 0) ? kInputNavUp : kInputNavLeft;
        } else if (value == 1) {
            logical = (axis == 0) ? kInputNavDown : kInputNavRight;
        } else if (value == 0 && g_axisLatch[axis] != 0) {
            // Release of a press we rewrote. Hand back the SAME logical code
            // so the press-release pairing matches, then clear the latch.
            code = g_axisLatch[axis];
            g_axisLatch[axis] = 0;
            acclog::Write("Pad", "stick axis %d release -> logical %d",
                          axis, code);
            return Verdict::Rewritten;
        } else {
            return Verdict::NotPad;   // stray centring with nothing latched
        }
        NoteSeen();
        g_axisLatch[axis] = logical;

        // In the world the left stick MOVES the character; it must never
        // double as menu navigation there. (Movement itself never reaches this
        // hook — it is the engine's own walk path.)
        if (InWorldSurface()) {
            g_axisLatch[axis] = 0;
            return Verdict::NotPad;
        }

        // On the map screen the left stick pans the virtual cursor, exactly as
        // the player's walk keys do there — that is the analog surface the map
        // most wants, and the D-Pad still covers menu navigation. Swallow the
        // axis event so the engine cannot ALSO step the panel's controls with
        // it; the pan itself comes from the XInput sample in Tick(), read by
        // map_ui_cursor.
        if (acc::map_ui_cursor::IsActive()) {
            g_axisLatch[axis] = 0;
            acclog::Trace("Pad", "stick(%d) consumed — map cursor owns it", raw);
            return Verdict::Consumed;
        }

        NoteNavPress();
        if (RouteToOverlay(logical)) return Verdict::Consumed;
        code = logical;
        acclog::Write("Pad", "stick(%d) val=%d -> logical %d", raw, value, code);
        return Verdict::Rewritten;
    }

    // ---- D-Pad ------------------------------------------------------------
    const int dpadLogical = LogicalForDirection(code);
    if (dpadLogical != 0) {
        NoteSeen();
        if (value == 0) return Verdict::NotPad;   // no release is ever sent
        NoteNavPress();
        if (RouteToOverlay(dpadLogical)) {
            acclog::Write("Pad", "D-Pad(%d) -> overlay logical %d",
                          raw, dpadLogical);
            return Verdict::Consumed;
        }
        if (InWorldSurface() && DispatchWorldDpad(code)) {
            return Verdict::Consumed;
        }
        code = dpadLogical;
        acclog::Write("Pad", "D-Pad(%d) -> logical %d", raw, code);
        return Verdict::Rewritten;
    }

    // ---- A ----------------------------------------------------------------
    if (code == kPadButtonA) {
        if (value == 0) return Verdict::NotPad;
        NoteNavPress();
        if (QuickMenuHelpFocused()) {
            acclog::Write("Pad", "A on Quick Menu Help -> mod key list");
            acc::help::ToggleMenu();
            return Verdict::Consumed;
        }
        if (RouteToOverlay(kInputEnter1)) {
            acclog::Write("Pad", "A -> overlay activate");
            return Verdict::Consumed;
        }
        if (InWorldSurface()) {
            // Action-menu mode, menu not up yet: A opens it rather than firing
            // the default action. Otherwise A would mean two different things
            // in the same mode depending on whether a D-Pad press had happened
            // to open the menu first.
            if (EnsureLiveMenuForMode()) {
                acclog::Write("Pad", "A -> opened live action menu");
                return Verdict::Consumed;
            }
            // Interact with what the player was last TOLD about, not with the
            // engine's last-clicked target — the same promise keyboard Enter
            // makes. Consumed so the engine's own default action can't also
            // fire on a stale target.
            acclog::Write("Pad", "A -> interact with narrated target");
            acc::interact::InteractNarratedTarget(/*forceRadial=*/false);
            return Verdict::Consumed;
        }
        code = kInputEnter1;
        acclog::Write("Pad", "A -> logical Enter");
        return Verdict::Rewritten;
    }

    // ---- B ----------------------------------------------------------------
    if (code == kPadButtonB) {
        if (value == 0) return Verdict::NotPad;
        NoteNavPress();
        if (RouteToOverlay(kInputEsc1)) {
            acclog::Write("Pad", "B -> overlay cancel");
            return Verdict::Consumed;
        }
        if (InWorldSurface()) return Verdict::NotPad;   // engine's own cancel
        // In a menu, B is Esc. Our close-and-announce path gets it; if it
        // declines, the engine still closes the panel off the raw 0x28 it
        // reads from its own frame.
        code = kInputEsc1;
        acclog::Write("Pad", "B -> logical Esc");
        return Verdict::Rewritten;
    }

    // Everything else (X, Y, LB, RB, Back) keeps its engine meaning. X is
    // Switch Party Leader and stays the engine's: it briefly hosted the mod's
    // action menu, which the D-Pad now carries far better — the menu belongs
    // where the navigation is, not on a button beside it.
    //
    // They are still flagged as pad events so the F1 suppression in
    // OnHandleInputEvent and the log annotation know what they are looking at.
    return Verdict::NotPad;
}

}  // namespace acc::pad
