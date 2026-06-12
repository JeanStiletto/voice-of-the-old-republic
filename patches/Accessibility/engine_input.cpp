#include "engine_input.h"

#include <windows.h>
#include <atomic>
#include <cstdint>

#include "log.h"

namespace acc::engine {

namespace {

// Engine global holding the CExoInput facade pointer (the same symbol
// HideLoadScreen reads before calling SetActive). Verified in Lane's DB:
// SYMBOL ExoInput @0x007a39e4.
constexpr uintptr_t kAddrExoInputGlobal = 0x007a39e4;

// CExoInput::SetActive(this, int active) @0x005df540. Sets the facade +
// CExoInputInternal active flags and forwards to
// CExoRawInputInternal::SetActive, which Acquire()s the DirectInput
// keyboard/mouse/joystick devices on the 0->1 transition.
constexpr uintptr_t kAddrCExoInputSetActive = 0x005df540;

typedef void(__thiscall* PFN_CExoInputSetActive)(void* this_, int active);

}  // namespace

// Names for the InputIndices enum (size 132, definition lifted from Lane's
// Ghidra SARIF: /KotOR Types/Enums/InputIndices). Index = enum value;
// covers MOUSE_*, KEYBOARD_*, JOYSTICK_*. -1 / 0xFFFFFFFF is INPUTDEVICE_NONE.
const char* InputIndexName(int code) {
    static const char* const k_names[] = {
        "MOUSE_BUTTON0", "MOUSE_BUTTON1", "MOUSE_BUTTON2",
        "MOUSE_XAXIS", "MOUSE_YAXIS", "MOUSE_ZAXIS",
        "KEYBOARD_RETURN",
        "KEYBOARD_LEFT_ARROW", "KEYBOARD_RIGHT_ARROW",
        "KEYBOARD_UP_ARROW", "KEYBOARD_DOWN_ARROW",
        "KEYBOARD_NUMPAD1", "KEYBOARD_NUMPAD2", "KEYBOARD_NUMPAD3",
        "KEYBOARD_NUMPAD4", "KEYBOARD_NUMPAD5", "KEYBOARD_NUMPAD6",
        "KEYBOARD_NUMPAD7", "KEYBOARD_NUMPAD8", "KEYBOARD_NUMPAD9",
        "KEYBOARD_NUMPAD0",
        "KEYBOARD_NUMPADDECIMAL", "KEYBOARD_NUMPADMINUS", "KEYBOARD_NUMPADPLUS",
        "KEYBOARD_LEFTSHIFT", "KEYBOARD_RIGHTSHIFT",
        "KEYBOARD_LEFTALT", "KEYBOARD_RIGHTALT",
        "KEYBOARD_LEFTCTRL", "KEYBOARD_RIGHTCTRL",
        "KEYBOARD_TAB", "KEYBOARD_ESC",
        "KEYBOARD_HOME", "KEYBOARD_END",
        "KEYBOARD_PAGEUP", "KEYBOARD_PAGEDOWN",
        "KEYBOARD_INSERT", "KEYBOARD_DELETE",
        "KEYBOARD_PRINTSCREEN",
        "KEYBOARD_F1", "KEYBOARD_F2", "KEYBOARD_F3", "KEYBOARD_F4",
        "KEYBOARD_F5", "KEYBOARD_F6", "KEYBOARD_F7", "KEYBOARD_F8",
        "KEYBOARD_F9", "KEYBOARD_F10", "KEYBOARD_F11", "KEYBOARD_F12",
        "KEYBOARD_A", "KEYBOARD_B", "KEYBOARD_C", "KEYBOARD_D",
        "KEYBOARD_E", "KEYBOARD_F", "KEYBOARD_G", "KEYBOARD_H",
        "KEYBOARD_I", "KEYBOARD_J", "KEYBOARD_K", "KEYBOARD_L",
        "KEYBOARD_M", "KEYBOARD_N", "KEYBOARD_O", "KEYBOARD_P",
        "KEYBOARD_Q", "KEYBOARD_R", "KEYBOARD_S", "KEYBOARD_T",
        "KEYBOARD_U", "KEYBOARD_V", "KEYBOARD_W", "KEYBOARD_X",
        "KEYBOARD_Y", "KEYBOARD_Z",
        "KEYBOARD_1", "KEYBOARD_2", "KEYBOARD_3", "KEYBOARD_4",
        "KEYBOARD_5", "KEYBOARD_6", "KEYBOARD_7", "KEYBOARD_8",
        "KEYBOARD_9", "KEYBOARD_0",
        "KEYBOARD_SPACE", "KEYBOARD_NUMPADENTER",
        "KEYBOARD_CAPSLOCK", "KEYBOARD_PAUSE",
        "KEYBOARD_F13", "KEYBOARD_F14", "KEYBOARD_F15",
        "KEYBOARD_MINUS", "KEYBOARD_EQUALS", "KEYBOARD_BACK",
        "KEYBOARD_LBRACKET", "KEYBOARD_RBRACKET",
        "KEYBOARD_SEMICOLON", "KEYBOARD_APOSTROPHE",
        "KEYBOARD_GRAVE", "KEYBOARD_BACKSLASH",
        "KEYBOARD_COMMA", "KEYBOARD_PERIOD", "KEYBOARD_SLASH",
        "KEYBOARD_MULTIPLY", "KEYBOARD_NUMPADCOMMA", "KEYBOARD_DIVIDE",
        "KEYBOARD_OEM_102",
        "JOYSTICK_XAXIS", "JOYSTICK_YAXIS", "JOYSTICK_HAT",
        "JOYSTICK_SLIDER0", "JOYSTICK_SLIDER1", "JOYSTICK_SLIDER2",
        "JOYSTICK_BUTTON0", "JOYSTICK_BUTTON1", "JOYSTICK_BUTTON2",
        "JOYSTICK_BUTTON3", "JOYSTICK_BUTTON4", "JOYSTICK_BUTTON5",
        "JOYSTICK_BUTTON6", "JOYSTICK_BUTTON7", "JOYSTICK_BUTTON8",
        "JOYSTICK_BUTTON9", "JOYSTICK_BUTTON10", "JOYSTICK_BUTTON11",
        "JOYSTICK_BUTTON12", "JOYSTICK_BUTTON13", "JOYSTICK_BUTTON14",
    };
    if (code == -1) return "INPUTDEVICE_NONE";
    if (code >= 0 && code < (int)(sizeof(k_names) / sizeof(k_names[0]))) {
        return k_names[code];
    }
    // Engine logical-action codes that don't translate to an InputIndices
    // value (i.e. ManagerTranslateCode passes them through unchanged) and
    // are therefore neither in the InputIndices array nor in our translator.
    // Naming them inline keeps the input log readable instead of "?(206)".
    if (code == 0xCE) return "LOGICAL_TAB";
    return "?";
}

// CSWGuiManager::HandleInputEvent receives KOTOR-internal "logical action"
// codes for navigation / system keys and translates them to InputIndices
// values via two inline switch statements before dispatching to the active
// panel's per-class override. Mapping recovered by decompiling the function
// at 0x0040c8e0:
//
//   0xb4 (180), 0xdf (223)   → 0x28 = KEYBOARD_F2  (cancel / back)
//   0xb5 (181), 0xbb (187)   → 0x27 = KEYBOARD_F1  (confirm / activate)
//   0xb6 (182)               → 0x3d
//   0xb7 (183)               → 0x3e
//   0xb8 (184)               → 0x3f
//   0xb9 (185)               → 0x40
//
// Codes outside this set pass through unchanged. Naming the four
// 0x3d-0x40 outputs is impossible without knowing the user's `[Keymapping]`
// — they're the InputIndices values the user has bound to nav-up/nav-down/
// next/prev (default keys vary by user). Just print "→ KEYBOARD_X(N)" and
// let the caller correlate against the user's keybindings.
int ManagerTranslateCode(int code) {
    switch (code) {
    case 0xb4: case 0xdf: return 0x28;
    case 0xb5: case 0xbb: return 0x27;
    case 0xb6:            return 0x3d;
    case 0xb7:            return 0x3e;
    case 0xb8:            return 0x3f;
    case 0xb9:            return 0x40;
    default:              return code;
    }
}

bool EnsureInputAcquired() {
    __try {
        void* exoInput = *reinterpret_cast<void**>(kAddrExoInputGlobal);
        if (!exoInput) {
            acclog::Write("EngineInput",
                "EnsureInputAcquired: ExoInput global is null; skipped");
            return false;
        }
        auto setActive = reinterpret_cast<PFN_CExoInputSetActive>(
            kAddrCExoInputSetActive);
        setActive(exoInput, 1);
        acclog::Write("EngineInput",
            "EnsureInputAcquired: CExoInput::SetActive(%p, 1) dispatched "
            "(cold-start DirectInput wake)", exoInput);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("EngineInput",
            "EnsureInputAcquired: exception during SetActive; skipped");
        return false;
    }
}

bool ForceReacquireInput() {
    __try {
        void* exoInput = *reinterpret_cast<void**>(kAddrExoInputGlobal);
        if (!exoInput) {
            acclog::Write("EngineInput",
                "ForceReacquireInput: ExoInput global is null; skipped");
            return false;
        }
        auto setActive = reinterpret_cast<PFN_CExoInputSetActive>(
            kAddrCExoInputSetActive);
        // Drive a real 0->1 edge. SetActive(1) alone no-ops when the active
        // flag is already (stale) 1; the leading SetActive(0) forces the
        // transition so the engine re-Acquires the DirectInput devices.
        setActive(exoInput, 0);
        setActive(exoInput, 1);
        acclog::Write("EngineInput",
            "ForceReacquireInput: CExoInput::SetActive(%p, 0)->(1) cycle "
            "dispatched (post-load DirectInput re-acquire)", exoInput);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("EngineInput",
            "ForceReacquireInput: exception during SetActive cycle; skipped");
        return false;
    }
}

bool ReleaseInput() {
    __try {
        void* exoInput = *reinterpret_cast<void**>(kAddrExoInputGlobal);
        if (!exoInput) {
            acclog::Write("EngineInput",
                "ReleaseInput: ExoInput global is null; skipped");
            return false;
        }
        auto setActive = reinterpret_cast<PFN_CExoInputSetActive>(
            kAddrCExoInputSetActive);
        // SetActive(0) unacquires the DirectInput devices. KOTOR acquires its
        // keyboard at background level (it keeps reading even when another
        // window is foreground — harmless in fullscreen, how the game shipped).
        // Windowed, that means menu nav keys bleed into the game while the user
        // is in another window (e.g. a screen reader). Releasing on focus-loss
        // makes input mirror foreground: the game holds the keyboard only while
        // it owns the foreground. The regain edge re-Acquires via
        // ForceReacquireInput.
        setActive(exoInput, 0);
        acclog::Write("EngineInput",
            "ReleaseInput: CExoInput::SetActive(%p, 0) dispatched "
            "(lost foreground — unacquire so keys don't bleed into the "
            "background game)", exoInput);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("EngineInput",
            "ReleaseInput: exception during SetActive; skipped");
        return false;
    }
}

namespace {
// Pending input-acquisition change, set from focus-event wndprocs and drained
// on the next clean tick. 0 = nothing, 1 = acquire (gained foreground),
// 2 = release (lost foreground). Last writer wins, so a rapid loss->gain (or
// gain->loss) flip collapses to its final state at drain — input ends up
// matching the foreground state that actually settled.
std::atomic<int> g_pendingInputState{0};
}  // namespace

void RequestInputReacquire() {
    g_pendingInputState.store(1, std::memory_order_relaxed);
}

void RequestInputRelease() {
    g_pendingInputState.store(2, std::memory_order_relaxed);
}

void DrainPendingReacquire() {
    int want = g_pendingInputState.exchange(0, std::memory_order_relaxed);
    if (want == 1) {
        acclog::Write("EngineInput",
            "DrainPendingReacquire: focus gain flagged; forcing DirectInput "
            "re-grab");
        ForceReacquireInput();
    } else if (want == 2) {
        acclog::Write("EngineInput",
            "DrainPendingReacquire: focus loss flagged; releasing DirectInput");
        ReleaseInput();
    }
}

}  // namespace acc::engine
