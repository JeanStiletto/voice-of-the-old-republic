// Gamepad input — see pad_input.h for why this is one module and not a pad
// case scattered through every handler, and for why KOTOR 1 and KOTOR 2 share
// every binding but not their event source.

#include "pad_input.h"

#include <windows.h>
#include <cmath>
#include <cstdint>

#include "combat_queue.h"
#include "cycle_input.h"
#include "engine_game.h"
#include "engine_input.h"
#include "engine_keymap.h"   // GameActionScancode — KOTOR 1 fires engine
                             // actions by synthesising the player's own bind
#include "engine_panels.h"
#include "engine_player.h"
#include "examine_view.h"
#include "help.h"
#include "hotkeys.h"         // PadPress — reuse the keyboard actions' pollers
#include "interact_dispatch.h"
#include "key_inject.h"      // Send / Tap — the scancode route into the engine
#include "log.h"
#include "map_ui_cursor.h"   // IsActive — the map screen owns the left stick
#include "menus_chain.h"     // g_chain* — which Quick Menu entry is focused
#include "narrated_target.h" // TryGet — is there a target to open the menu on?
#include "pad_quickmenu.h"   // the KOTOR 1 Y menu (K2 uses the engine's own)
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

// XInput's own button bits. Mirrored rather than included for the same reason
// the structures above are.
constexpr WORD kBtnDpadUp    = 0x0001;
constexpr WORD kBtnDpadDown  = 0x0002;
constexpr WORD kBtnDpadLeft  = 0x0004;
constexpr WORD kBtnDpadRight = 0x0008;
constexpr WORD kBtnStart     = 0x0010;
constexpr WORD kBtnBack      = 0x0020;
constexpr WORD kBtnThumbL    = 0x0040;
constexpr WORD kBtnThumbR    = 0x0080;
constexpr WORD kBtnShoulderL = 0x0100;
constexpr WORD kBtnShoulderR = 0x0200;
constexpr WORD kBtnA         = 0x1000;
constexpr WORD kBtnB         = 0x2000;
constexpr WORD kBtnX         = 0x4000;
constexpr WORD kBtnY         = 0x8000;

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

// The three trigger jobs, claimed by Tick() and drained by PollTriggers().
// Tick() runs in OnUpdate, and LT now OPENS the unified action menu — which
// the menu's hard rule forbids from that context (arming from a tick made the
// engine synthesise a phantom confirm one tick later that fired the menu's
// first entry). So Tick() only decides that the job is owed; PollTriggers()
// pays it from the input-poll slot, the same one the keyboard's Shift+Enter
// and the pad's own A button open the menu from.
bool g_pendingLtSolo = false;
bool g_pendingRtSolo = false;
bool g_pendingHelp   = false;

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

// The right stick, same convention. Rotate Camera on the shipped chart; on
// KOTOR 1 pad_movement holds the bound camera-turn key from it.
bool  g_rstickActive = false;
float g_rstickX      = 0.0f;
float g_rstickY      = 0.0f;

// The button word, sampled by Tick() and edge-detected by PollButtons(). Two
// stages rather than one because they run at different points in the tick:
// Tick() has to be early (its trigger state is a modifier other consumers
// read), while the dispatch belongs next to the keyboard poll.
WORD g_btnNow  = 0;
WORD g_btnLast = 0;

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

    // The KOTOR 1 quick menu is the mod's own overlay and equally global while
    // it is up — on KOTOR 2 the same surface is an engine panel and the
    // navigation chain walks it instead, so this declines there.
    if (acc::pad::quickmenu::HandleNavCode(logical)) return true;

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
// The D-Pad, and what it is for
// ---------------------------------------------------------------------------
// The D-Pad is not free in the world — it drives the engine's own
// CSWGuiActionMenuIos, whose level 1 (category) is D-Pad left/right and whose
// level 2 (entry) is D-Pad up/down, with A confirming. The earlier reading
// that it was inert was a blind test round watching a silent, purely visual
// highlight move; every round since has consumed the codes outright, which is
// why Pad.ActionMenu never logged a line.
//
// So the four codes are ours, and in the world they are the object cycle:
// left/right steps the object, up/down the category. That is the D-Pad's ONE
// job. It used to have two, switched by the left trigger — cycle and a
// permanently-open "live" action menu — and the second one ate the A button:
// with the mode on, A always belonged to the menu, so interacting with
// anything meant switching the mode back first. The action menu is a menu the
// player OPENS now (the left trigger, below), and while it is up it is an
// overlay, so RouteToOverlay claims the D-Pad before this function is reached.

// Open the unified action menu on whatever is focused — the pad's Shift+Enter.
// Returns true when it is up afterwards.
//
// With no focused target the target categories are empty, so fall through to
// the personal block — medpacs and stims are exactly what a player wants when
// nothing is targeted, and refusing to open would be the wrong answer.
bool OpenActionMenu() {
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
    acclog::Write("Pad", "action menu open target=%d up=%d",
                  haveTarget ? 1 : 0, up ? 1 : 0);
    return up;
}

// LT — open the action menu, or close it if it is already up. The pad's whole
// action-menu vocabulary: one button, both directions, exactly what Shift+Enter
// and Esc are on the keyboard.
//
// Closing routes Esc into the menu rather than calling ForceDisarm, so the
// pause release, the queue commit and the close announcement all stay in
// CloseFromEsc — the same close B and the keyboard's Esc run. A menu the
// KEYBOARD opened closes here too: it is one menu, and a toggle that refused
// to close some of them would be a rule the user has to know.
void ToggleActionMenu() {
    if (acc::unified_menu::IsActive() && !acc::unified_menu::IsSuspended()) {
        acclog::Write("Pad", "LT -> close action menu");
        acc::unified_menu::HandleInputEvent(kInputEsc1, 1);
        return;
    }
    acclog::Write("Pad", "LT -> open action menu");
    OpenActionMenu();
}

// In-world D-Pad dispatch: the object cycle. The action menu never reaches
// here — while it is up it is an overlay and RouteToOverlay claims the code
// first. Returns true when the binding fired (the caller consumes).
bool DispatchWorldDpad(int code) {
    using PA = acc::cycle_input::PadAction;

    switch (code) {
        case kPadDpadLeft:  return acc::cycle_input::DispatchPadAction(PA::ItemPrev);
        case kPadDpadRight: return acc::cycle_input::DispatchPadAction(PA::ItemNext);
        case kPadDpadUp:    return acc::cycle_input::DispatchPadAction(PA::CategoryPrev);
        case kPadDpadDown:  return acc::cycle_input::DispatchPadAction(PA::CategoryNext);
        default:            return false;
    }
}

// The map screen's D-Pad.
//
// Every control on the map panel is filtered out of the chain (menus_chain
// drops the close button and both note-stepper buttons), so the D-Pad had
// nothing of ours to drive there and fell through to the engine — whose
// InGameMap steps prev / next map note on UP and DOWN, and does nothing at all
// with left and right. The hints were therefore on the vertical axis while the
// world's objects are on the horizontal one, and they are all one category:
// exactly what the horizontal axis is for everywhere else in the mod.
//
// So the map screen gets the same D-Pad the world does, through the mod's own
// Map cycle context — which is also what the keyboard's `,` / `.` already do
// here. Left and right step the hints (spoken name, bearing and distance, with
// the virtual cursor panning to each); up and down step the category, of which
// the map has one.
//
// Gated on HasActiveMapPanel, the same predicate DispatchPadAction uses to
// pick Map over World context, so the two can never disagree about which
// cycle state the press lands in.
bool DispatchMapDpad(int code) {
    if (!acc::engine::HasActiveMapPanel()) return false;
    using PA = acc::cycle_input::PadAction;
    switch (code) {
        case kPadDpadLeft:  return acc::cycle_input::DispatchPadAction(PA::ItemPrev);
        case kPadDpadRight: return acc::cycle_input::DispatchPadAction(PA::ItemNext);
        case kPadDpadUp:    return acc::cycle_input::DispatchPadAction(PA::CategoryPrev);
        case kPadDpadDown:  return acc::cycle_input::DispatchPadAction(PA::CategoryNext);
        default:            return false;
    }
}

// ---------------------------------------------------------------------------
// KOTOR 1 — making the ENGINE act
// ---------------------------------------------------------------------------
// Everything above this point is game-agnostic: it drives the MOD. What
// follows is the K1-only other half — the presses whose whole job is something
// only the engine can do (navigate one of its panels, cycle a target, switch
// party leader, pause). K2 gets those for free because the engine binds the
// pad itself; K1 has to be given the player's own key.
//
// The key goes in as a DirectInput scancode, which is all the engine's
// keyboard layer can see (a plain-VK SendInput is invisible to it). Every
// caller here is already behind the foreground gate in PollButtons.
//
// [Keymapping] action ids are keymap.2da row + 200. The defaults below are
// that file's own key for the row, used when the ini has no line for it — or
// when the bound key is one the mod refuses to inject (Caps Lock; see
// engine_keymap::InputIndexToScancode).
constexpr int kActSelectPrev = 204;   // row 4  SelectPrev   — default Q
constexpr int kActSelectNext = 205;   // row 5  SelectNext   — default E
constexpr int kActChangeChar = 206;   // row 6  ChangeChar   — default Tab
constexpr int kActOptions    = 216;   // row 16 Options      — default O
constexpr int kActFlourish   = 242;   // row 42 Flourish     — default X
constexpr int kActPrevMenu   = 243;   // row 43 PrevMenu     — default Q
constexpr int kActNextMenu   = 244;   // row 44 NextMenu     — default E
constexpr int kActPauseSpace = 241;   // row 41 Pause        — default Space

// DIK for those defaults, plus the three keys the pad plays as itself in a
// menu. The arrow cluster is behind the E0 prefix: DirectInput reports it as
// DIK_UP = 0xC8, but SendInput wants the BASE scancode with the extended flag,
// so 0x48 and `extended = true`.
constexpr int kDikQ      = 0x10;
constexpr int kDikE      = 0x12;
constexpr int kDikO      = 0x18;
constexpr int kDikX      = 0x2D;
constexpr int kDikTab    = 0x0F;
constexpr int kDikSpace  = 0x39;
constexpr int kDikReturn = 0x1C;
constexpr int kDikEscape = 0x01;
constexpr int kDikUp     = 0x48;   // extended
constexpr int kDikDown   = 0x50;   // extended
constexpr int kDikLeft   = 0x4B;   // extended
constexpr int kDikRight  = 0x4D;   // extended

// Fire a [Keymapping] game action by synthesising the key it is bound to.
void FireGameAction(int actionId, int defaultDik, const char* what) {
    int scan = acc::engine_keymap::GameActionScancode(actionId);
    if (scan == 0) scan = defaultDik;
    acclog::Write("Pad", "%s -> game action %d (scan 0x%02x)", what, actionId,
                  scan);
    acc::key_inject::Tap(scan);
}

// Navigate an ENGINE panel: send the arrow / Enter / Esc the keyboard would
// have sent. Nothing else in the mod needs to know a pad exists — the press
// arrives at the GUI-manager hook as an ordinary keyboard event and the
// navigation chain handles it exactly as it handles the keyboard, including
// the press/release pairing and the engine's own control focus.
//
// If the arrow cluster ever turns out not to reach the engine's DirectInput
// read (the extended-scancode question), the fallback is a direct call into
// acc::menus::chain::HandleNavStep / HandleLeftRight / HandleEnterActivation /
// HandleEsc, which are exported for exactly this shape of caller.
void InjectMenuKey(int logical) {
    switch (logical) {
        case kInputNavUp:    acc::key_inject::Tap(kDikUp,    true); break;
        case kInputNavDown:  acc::key_inject::Tap(kDikDown,  true); break;
        case kInputNavLeft:  acc::key_inject::Tap(kDikLeft,  true); break;
        case kInputNavRight: acc::key_inject::Tap(kDikRight, true); break;
        case kInputEnter1:   acc::key_inject::Tap(kDikReturn);      break;
        case kInputEsc1:     acc::key_inject::Tap(kDikEscape);      break;
        default: return;
    }
    acclog::Trace("Pad", "menu key injected for logical %d", logical);
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

// ---------------------------------------------------------------------------
// KOTOR 1 — one press, dispatched
// ---------------------------------------------------------------------------
// Each of these is the K1 twin of a branch in TranslateManagerEvent, in the
// same order of refusal, calling the same helpers. What differs is only the
// tail: where K2 rewrites the code and lets the engine's own dispatch carry it
// on, K1 synthesises the key the keyboard would have sent.

void DispatchDpad(int code) {
    const int logical = LogicalForDirection(code);
    NoteNavPress();
    if (RouteToOverlay(logical)) {
        acclog::Write("Pad", "D-Pad -> overlay logical %d", logical);
        return;
    }
    if (InWorldSurface()) {
        // In the world the D-Pad is ours outright, and the fall-through must
        // NOT reach the injection below: KOTOR 1 binds the arrow keys as the
        // movement alternates (Action285/286), so an injected Up there would
        // walk the character. A cycle that declines (empty category, no
        // context) is silence, not a step forward.
        DispatchWorldDpad(code);
        return;
    }
    if (DispatchMapDpad(code)) {
        acclog::Write("Pad", "D-Pad -> map cycle");
        return;
    }
    InjectMenuKey(logical);
}

void DispatchA() {
    NoteNavPress();
    if (RouteToOverlay(kInputEnter1)) {
        acclog::Write("Pad", "A -> overlay activate");
        return;
    }
    if (InWorldSurface()) {
        // In the world A always interacts. The action menu, when it is up, is
        // an overlay and took this press at RouteToOverlay above — so A never
        // has to be read as "which mode am I in".
        //
        // Interact with what the player was last TOLD about, not the engine's
        // last-clicked target — the promise keyboard Enter makes.
        acclog::Write("Pad", "A -> interact with narrated target");
        acc::interact::InteractNarratedTarget(/*forceRadial=*/false);
        return;
    }
    InjectMenuKey(kInputEnter1);
}

void DispatchB() {
    NoteNavPress();
    if (RouteToOverlay(kInputEsc1)) {
        acclog::Write("Pad", "B -> overlay cancel");
        return;
    }
    // With no overlay up there is nothing to cancel in the world: KOTOR 1's
    // Escape opens the in-game menu, and that is the quick menu's first entry,
    // not a button the player should hit by reflex while walking. (The action
    // menu is an overlay, so B closes it at RouteToOverlay above — the second
    // way out of it, beside LT.)
    if (InWorldSurface()) return;
    InjectMenuKey(kInputEsc1);
}

void DispatchShoulder(bool right) {
    // The chords come first — a shoulder taken during a trigger hold is the
    // chord, and marking the trigger chorded is what stops its own solo job
    // firing on release.
    if (!right && g_ltHeld) {
        g_ltChorded = true;
        acclog::Write("Pad", "LT + LB -> beacon to focus");
        acc::cycle_input::DispatchPadAction(
            acc::cycle_input::PadAction::BeaconFocus);
        return;
    }
    if (right && g_rtHeld) {
        g_rtChorded = true;
        acclog::Write("Pad", "RT + RB -> walk to focus");
        acc::cycle_input::DispatchPadAction(
            acc::cycle_input::PadAction::WalkToFocus);
        return;
    }

    // On the container and the store the keyboard's Q / E carry the mode
    // toggle (take vs give, buy vs sell). Same routing as KOTOR 2: a synthetic
    // press of the action the keyboard poller runs, so the foreground gate,
    // the pending-operation guard and the mode announcement stay in one place.
    acc::engine::UiBlockState blk;
    acc::engine::IsForegroundUiBlocking(&blk);
    const bool container = (blk.fgKind == acc::engine::PanelKind::Container);
    const bool store     = (blk.fgKind == acc::engine::PanelKind::Store);
    if (container || store) {
        NoteNavPress();
        acclog::Write("Pad", "%s -> %s mode toggle", right ? "RB" : "LB",
                      container ? "container" : "store");
        acc::hotkeys::PadPress(container
                                   ? acc::hotkeys::Action::ContainerGiveMode
                                   : acc::hotkeys::Action::StoreModeToggle);
        return;
    }

    // Otherwise the shoulders do what they do on KOTOR 2: cycle the target in
    // the world, and step the in-game menu's sub-screens everywhere else —
    // which is how a pad reaches the map, the journal and the equipment screen
    // at all. KOTOR 1 has both as ordinary [Keymapping] actions.
    if (InWorldSurface()) {
        FireGameAction(right ? kActSelectNext : kActSelectPrev,
                       right ? kDikE : kDikQ,
                       right ? "RB (cycle target right)"
                             : "LB (cycle target left)");
        return;
    }
    FireGameAction(right ? kActNextMenu : kActPrevMenu,
                   right ? kDikE : kDikQ,
                   right ? "RB (next sub-screen)" : "LB (previous sub-screen)");
}

// D-Pad auto-repeat. KOTOR 2 gets this from the engine, which gates its own
// D-Pad codes behind a 150 ms repeat; without it here a KOTOR 1 player would
// have to tap once per entry down a long list.
constexpr ULONGLONG kDpadRepeatFirstMs = 400;
constexpr ULONGLONG kDpadRepeatNextMs  = 150;

struct DpadBit { WORD mask; int code; };
constexpr DpadBit kDpadBits[4] = {
    {kBtnDpadUp,    kPadDpadUp},
    {kBtnDpadDown,  kPadDpadDown},
    {kBtnDpadLeft,  kPadDpadLeft},
    {kBtnDpadRight, kPadDpadRight},
};
ULONGLONG g_dpadNextMs[4] = {0, 0, 0, 0};

}  // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

bool LeftTriggerHeld()  { return g_ltHeld; }
bool RightTriggerHeld() { return g_rtHeld; }
bool Connected()        { return g_padSeen; }

void Tick() {
    // Both games. KOTOR 2 reads the pad here only for the triggers, the stick
    // magnitude and pad presence — the engine delivers its presses. KOTOR 1
    // has no engine pad path at all, so this sample is the ONLY thing that
    // knows a controller was touched; PollButtons() and pad_movement read what
    // it leaves behind.
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
        // A pad that vanishes mid-hold must read as "everything released", or
        // pad_movement would keep a synthesised walk key down forever and
        // PollButtons would see a phantom rising edge when a pad comes back.
        g_rstickActive = false;
        g_rstickX = g_rstickY = 0.0f;
        g_btnNow  = 0;
        return;
    }

    g_btnNow = st.Gamepad.wButtons;

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
    // silently open a menu under them. The edge state above is tracked
    // regardless, so a chord begun in the background still cancels its
    // trigger's solo job instead of firing it on the way back — and the stick
    // sampling below stays outside the gate, because its consumers want a
    // truthful "not moving" while the game is away, not a stale "moving".
    //
    // What the edges MEAN is decided here; when it is acted on is not. The
    // three trigger jobs are only recorded as pending and are drained by
    // PollTriggers() in the input-poll slot — LT opens the unified action menu,
    // and the menu's hard rule is that it may not be populated from OnUpdate,
    // which is where this function runs.
    if (acc::hotkeys::IsForegroundGame()) {
        // Both triggers — the context help ("what do the keys do on THIS
        // screen"). Claimed on whichever press completes the pair, and marks
        // both holds chorded so neither release also does its solo job.
        if ((ltPressed && rt) || (rtPressed && lt)) {
            g_ltChorded = g_rtChorded = true;
            g_pendingHelp = true;
        }

        // LT alone — open or close the action menu.
        if (ltReleased && !g_ltChorded) g_pendingLtSolo = true;

        // RT alone — the exact keyboard AltGr binding: announce the facing in
        // degrees.
        if (rtReleased && !g_rtChorded) g_pendingRtSolo = true;
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

    // Right stick, same dead zone and same normalisation. Unused on KOTOR 2
    // (the engine rotates its own camera from it); on KOTOR 1 it is what
    // pad_movement turns the camera with.
    const int rx = st.Gamepad.sThumbRX;
    const int ry = st.Gamepad.sThumbRY;
    const int rmagSq = rx * rx + ry * ry;
    g_rstickActive = rmagSq > kStickDeadZoneSq;
    if (g_rstickActive) {
        const float rmag = std::sqrt(static_cast<float>(rmagSq));
        g_rstickX = static_cast<float>(rx) / rmag;
        g_rstickY = static_cast<float>(ry) / rmag;
    } else {
        g_rstickX = g_rstickY = 0.0f;
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

bool InWorld() { return InWorldSurface(); }

bool StickVector(float& outX, float& outY) {
    if (!g_stickMoving) return false;
    outX = g_stickX;
    outY = g_stickY;
    return true;
}

bool RightStickVector(float& outX, float& outY) {
    if (!g_rstickActive) return false;
    outX = g_rstickX;
    outY = g_rstickY;
    return true;
}

void PollButtons() {
    // KOTOR 2's presses arrive as engine events at the two Translate* seams;
    // dispatching them a second time from here would double every binding.
    if (!acc::game::IsKotor1()) return;

    const WORD now  = g_btnNow;
    const WORD last = g_btnLast;
    // Update the edge baseline BEFORE any gate below can return: a button held
    // across an alt-tab must not read as a fresh press on the way back.
    g_btnLast = now;

    if (!g_padSeen) return;
    if (!acc::hotkeys::IsForegroundGame()) {
        for (ULONGLONG& t : g_dpadNextMs) t = 0;
        return;
    }

    const ULONGLONG nowMs = GetTickCount64();
    auto rising = [&](WORD m) { return (now & m) != 0 && (last & m) == 0; };

    // D-Pad first, and with repeat — it is the pad's primary surface in both
    // of its modes and in every menu.
    for (int i = 0; i < 4; ++i) {
        const WORD m = kDpadBits[i].mask;
        if (!(now & m)) {
            g_dpadNextMs[i] = 0;
            continue;
        }
        if (!(last & m)) {
            g_dpadNextMs[i] = nowMs + kDpadRepeatFirstMs;
            DispatchDpad(kDpadBits[i].code);
            continue;
        }
        if (nowMs >= g_dpadNextMs[i]) {
            g_dpadNextMs[i] = nowMs + kDpadRepeatNextMs;
            DispatchDpad(kDpadBits[i].code);
        }
    }

    if (rising(kBtnA)) DispatchA();
    if (rising(kBtnB)) DispatchB();
    if (rising(kBtnShoulderL)) DispatchShoulder(/*right=*/false);
    if (rising(kBtnShoulderR)) DispatchShoulder(/*right=*/true);

    // Y — the quick menu. Open only from the world (it acts on the world), but
    // closable from wherever it happens to be up.
    if (rising(kBtnY) &&
        (acc::pad::quickmenu::IsOpen() || InWorldSurface())) {
        acc::pad::quickmenu::Toggle();
    }

    // The remaining buttons are engine actions with no mod surface of their
    // own, so they are world-only: in a menu the D-Pad and A/B are the whole
    // vocabulary, and a stray party-leader switch there would be a surprise.
    if (!InWorldSurface()) return;

    if (rising(kBtnX))       FireGameAction(kActChangeChar, kDikTab,
                                            "X (switch party leader)");
    if (rising(kBtnBack))    FireGameAction(kActOptions, kDikO,
                                            "Back (options)");
    if (rising(kBtnStart))   FireGameAction(kActPauseSpace, kDikSpace,
                                            "Start (pause)");
    if (rising(kBtnThumbL))  FireGameAction(kActFlourish, kDikX,
                                            "L3 (flourish)");
    if (rising(kBtnThumbR)) {
        // No MOUSE_BUTTON1 hazard here — that trap is K2's, where the engine
        // hands R3 to us as the same InputIndex the right mouse button uses.
        // We read the physical button, so there is nothing to disambiguate.
        acclog::Write("Pad", "right stick press -> camera orient");
        acc::hotkeys::PadPress(acc::hotkeys::Action::CameraOrient);
    }
}

void PollTriggers() {
    const bool lt   = g_pendingLtSolo;
    const bool rt   = g_pendingRtSolo;
    const bool help = g_pendingHelp;
    // Clear before acting, and before any gate below can return: a job the
    // game was not foreground for by the time we got here is dropped, not
    // carried into the next tick.
    g_pendingLtSolo = g_pendingRtSolo = g_pendingHelp = false;
    if (!lt && !rt && !help) return;
    if (!acc::hotkeys::IsForegroundGame()) return;

    if (help) {
        acclog::Write("Pad", "LT + RT -> context help");
        acc::help::SpeakContextHelp();
    }
    // LT is the action menu's open AND close, so it is world-only in both
    // directions: in a menu the pad is simply the keyboard, and there is no
    // action menu to toggle over an engine panel. A suspended menu is exactly
    // that case — a blocking panel is up, so InWorldSurface() is already false.
    if (lt && InWorldSurface()) {
        NoteNavPress();
        ToggleActionMenu();
    }
    // RT — routed as a synthetic press so announce_degrees keeps its own
    // world-vs-map branch rather than the pad growing a second copy.
    if (rt) {
        acclog::Write("Pad", "RT -> announce facing in degrees");
        acc::hotkeys::PadPress(acc::hotkeys::Action::AnnounceDegrees);
    }
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
        if (DispatchMapDpad(code)) {
            acclog::Write("Pad", "D-Pad(%d) -> map cycle", raw);
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
            // In the world A always interacts. The action menu, when it is up,
            // is an overlay and took this press at RouteToOverlay above.
            //
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
        // With no overlay up (the action menu is one, and RouteToOverlay closed
        // it above if it was), the world's B stays the engine's own cancel.
        if (InWorldSurface()) return Verdict::NotPad;
        // In a menu, B is Esc. Our close-and-announce path gets it; if it
        // declines, the engine still closes the panel off the raw 0x28 it
        // reads from its own frame.
        code = kInputEsc1;
        acclog::Write("Pad", "B -> logical Esc");
        return Verdict::Rewritten;
    }

    // ---- LB / RB ----------------------------------------------------------
    // The bare shoulders are the engine's nearly everywhere: in the world they
    // cycle targets, and on the in-game menu strip they step between the
    // sub-screens — that is how the pad reaches the map at all. Two panels are
    // the exception. On the container and the store the engine gives them
    // nothing, while the keyboard's Q / E carry the mode toggle there (take vs
    // give, buy vs sell), so without this a pad player can only ever take and
    // only ever buy.
    //
    // Routed as a synthetic press of the SAME action the keyboard poller runs,
    // not as a reach into the panels: the foreground gate, the
    // operation-already-pending guard and the mode announcement then all stay
    // in their one place (menus_listbox / menus_store).
    if (code == kPadShoulderL || code == kPadShoulderR) {
        if (value == 0) return Verdict::NotPad;
        acc::engine::UiBlockState blk;
        acc::engine::IsForegroundUiBlocking(&blk);
        const bool container =
            (blk.fgKind == acc::engine::PanelKind::Container);
        const bool store = (blk.fgKind == acc::engine::PanelKind::Store);
        if (!container && !store) return Verdict::NotPad;
        NoteNavPress();
        acclog::Write("Pad", "%s -> %s mode toggle",
                      code == kPadShoulderL ? "LB" : "RB",
                      container ? "container" : "store");
        acc::hotkeys::PadPress(container
                                   ? acc::hotkeys::Action::ContainerGiveMode
                                   : acc::hotkeys::Action::StoreModeToggle);
        return Verdict::Consumed;
    }

    // Everything else (X, Y, Back) keeps its engine meaning. X is
    // Switch Party Leader and stays the engine's: it briefly hosted the mod's
    // action menu, which the left trigger carries now — a menu wants a button
    // that does nothing else, and X already had a job.
    //
    // They are still flagged as pad events so the F1 suppression in
    // OnHandleInputEvent and the log annotation know what they are looking at.
    return Verdict::NotPad;
}

}  // namespace acc::pad
