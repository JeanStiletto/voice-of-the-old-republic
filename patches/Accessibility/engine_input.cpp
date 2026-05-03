#include "engine_input.h"

namespace acc::engine {

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

}  // namespace acc::engine
