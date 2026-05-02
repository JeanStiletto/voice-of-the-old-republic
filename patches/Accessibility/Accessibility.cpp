// KOTOR Accessibility — DLL entry point + hook handlers.
//
// Layering:
//   log.{h,cpp}    file/debug logging primitives
//   tolk.{h,cpp}   screen-reader bridge (LoadLibrary'd lazily)
//   this file      DllMain + the OnXxx detour entry points
//
// Tolk is initialized lazily on the first hook fire — NOT inside DllMain —
// because Tolk_Load loads driver DLLs and initializes COM, both unsafe under
// the loader lock.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "tolk.h"

namespace {

// Keep in sync with manifest.toml [patch].version. We could parse the manifest
// at runtime, but a single literal is simpler and the build will fail loudly
// if the two ever drift on a release.
constexpr const char* kModVersion = "0.1.0";

char g_versionSha[128] = "(unset)";

// Lazy Tolk init. First hook to fire runs it; subsequent calls are no-ops.
// Speaks a one-line "loaded, version X" greeting on the first successful init
// so the user knows the patch is active even when no focus events have fired.
void EnsureTolkInitialized() {
    static bool done = false;
    if (done) return;
    done = true;
    if (tolk::Init()) {
        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 "KOTOR accessibility mod loaded, version %s", kModVersion);
        tolk::Speak(greeting, /*interrupt=*/true);
    }
}

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

}  // namespace

extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    static bool fired = false;
    if (fired) return;
    fired = true;
    EnsureTolkInitialized();
    acclog::Write("first CSWRules construction; detour active");
}

// Read CSWGuiControl name fields at known offsets (no engine re-entry).
// Returns true if the control had non-empty tooltip text.
//   0x28: tooltip_string (CExoString = char* c_string; uint32 length)
//   0x50: id (int)
static bool ReadControlNameFields(void* control, const char*& outTip,
                                  uint32_t& outTipLen, int& outId) {
    auto* base = reinterpret_cast<unsigned char*>(control);
    outTip    = *reinterpret_cast<const char**>(base + 0x28);
    outTipLen = *reinterpret_cast<uint32_t*>   (base + 0x2c);
    outId     = *reinterpret_cast<int*>        (base + 0x50);
    return outTip && outTipLen > 0;
}

// Vtable indices in GuiControlMethods (from docs/llm-docs/re/swkotor.exe.h).
// These are RTTI-style downcasts: each returns the same `this` cast to the
// concrete subclass, or nullptr if the control isn't of that subclass. They
// are trivial implementations (no engine state mutation, no allocation), so
// calling them from inside our hook is safe.
//
// Verified against the SARIF GuiControlMethods struct (offset 80/84/88/92).
constexpr int kVtableAsLabel        = 20;
constexpr int kVtableAsLabelHilight = 21;
constexpr int kVtableAsButton       = 22;
constexpr int kVtableAsButtonToggle = 23;

// Generic vtable downcast caller: invokes vtable[index](control) as __thiscall.
typedef void* (__thiscall* PFN_Downcast)(void* this_);
static void* CallDowncast(void* control, int vtableIndex) {
    if (!control) return nullptr;
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) return nullptr;
    auto fn = reinterpret_cast<PFN_Downcast>(vtable[vtableIndex]);
    if (!fn) return nullptr;
    return fn(control);
}

// Read a CExoString at (base + offset) into outBuf. Returns true if non-empty
// and length is sane (< bufSize). CExoString = { char* c_string; uint32 length }.
static bool ReadCExoString(void* base, size_t offset,
                           char* outBuf, size_t bufSize) {
    auto* p = reinterpret_cast<unsigned char*>(base) + offset;
    const char* s   = *reinterpret_cast<const char**>(p);
    uint32_t    len = *reinterpret_cast<uint32_t*>(p + 4);
    if (!s || len == 0 || len >= bufSize) return false;
    memcpy(outBuf, s, len);
    outBuf[len] = '\0';
    return true;
}

// Try to extract an announceable label for the control.
// Lookup order: tooltip → CSWGuiButton/ButtonToggle text → CSWGuiLabel/LabelHilight text.
// Returns the source ("tooltip", "button", "buttontoggle", "label",
// "labelhilight", or nullptr if none).
//
// Subclass text offsets (verified against the SARIF datatypes):
//   CSWGuiButton:        navigable(0x6c)+border(0x74)+border(0x74)+text(0x70) = 0x1c4
//                        → text at 0x154, text_params at +0x18 → CExoString at 0x16c
//                        → str_ref at +0x08 within text_params → 0x174
//   CSWGuiButtonToggle:  embeds CSWGuiButton at offset 0; offsets unchanged.
//   CSWGuiLabel:         control(0x5c)+border(0x74)+text(0x70) = 0x140
//                        → text at 0xd0, text_params at +0x18 → CExoString at 0xe8
//                        → str_ref at +0x08 within text_params → 0xf0
//   CSWGuiLabelHilight:  embeds CSWGuiLabel at offset 0; offsets unchanged.
constexpr size_t kButtonTextOffset    = 0x16c;
constexpr size_t kButtonStrRefOffset  = 0x174;
constexpr size_t kLabelTextOffset     = 0xe8;
constexpr size_t kLabelStrRefOffset   = 0xf0;

// CTlkTable::GetSimpleString — resolves a TLK str_ref to a localized string.
// Many KOTOR UI controls (e.g. Options screen "Annehmen"/"Abbrechen", certain
// chargen labels) leave their CExoString empty and store only a str_ref; the
// engine renders by resolving the str_ref through dialog.tlk every frame.
//
// Signature (from SARIF):
//   CExoString * __thiscall GetSimpleString(CExoString * out, ulong strref)
// Address  : 0x0041e8f0
// Global   : the live CTlkTable* lives at 0x007a3a08 (one indirection — the
//            address holds a pointer to the table). Confirmed by decompiling
//            the first GetSimpleString caller at 0x0418b29:
//              MOV ECX, [0x007a3a08]   ; this = *g_pTlkTable
//              CALL GetSimpleString
struct CExoString {
    char*    c_string;
    uint32_t length;
};
typedef CExoString* (__thiscall* PFN_GetSimpleString)(void* this_,
                                                      CExoString* out,
                                                      uint32_t strref);
constexpr uintptr_t kAddrGetSimpleString = 0x0041e8f0;
constexpr uintptr_t kAddrTlkTablePtr     = 0x007a3a08;

// Resolve a strref via the engine's TLK lookup, with SEH guard.
//
// The engine's GetSimpleString has its own SEH frame (it can raise on
// out-of-range / corrupt indices). A previous attempt to call it from inside
// a hook handler caused our patch to go silent partway through Options —
// presumably the engine's exception unwound through our trampoline and the
// framework disabled the hook on subsequent fires. We now wrap the call in
// __try/__except so any raised exception is contained.
//
// Out-arg: the engine copy-constructs `out` from a stack-local CExoString,
// allocating a fresh c_string via its own CRT. We copy the string into our
// caller's buffer, then deliberately leak `tmp.c_string` — calling the
// engine's CExoString destructor from our DLL would risk heap mismatch, and
// focus events are low-frequency enough that the leak is negligible across
// a session.
//
// Sanity bounds: we reject strref values that look invalid (-1, >0x100000)
// before invoking the engine, to reduce the rate of expected exceptions.
static bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize) {
    if (strref == 0 || strref == 0xFFFFFFFF) return false;
    if (strref > 0x100000) return false;  // KOTOR's TLK is well below this

    void* tlk = *reinterpret_cast<void**>(kAddrTlkTablePtr);
    if (!tlk) return false;

    auto fn = reinterpret_cast<PFN_GetSimpleString>(kAddrGetSimpleString);
    CExoString tmp = {nullptr, 0};
    bool ok = false;
    __try {
        fn(tlk, &tmp, strref);
        if (tmp.c_string && tmp.length > 0 && tmp.length < bufSize) {
            memcpy(outBuf, tmp.c_string, tmp.length);
            outBuf[tmp.length] = '\0';
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("TLK lookup raised SEH exception for strref=%u", strref);
        ok = false;
    }
    return ok;
}

// Read a uint32 at base+offset.
static uint32_t ReadU32(void* base, size_t offset) {
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<unsigned char*>(base) + offset);
}

// Resolve the visible text of a button/label-like subclass: try the inline
// CExoString first, then fall back to TLK str_ref lookup.
static bool ExtractTextOrStrRef(void* control,
                                size_t cexoOffset, size_t strRefOffset,
                                char* outBuf, size_t bufSize) {
    if (ReadCExoString(control, cexoOffset, outBuf, bufSize)) return true;
    uint32_t strref = ReadU32(control, strRefOffset);
    return LookupTlk(strref, outBuf, bufSize);
}

static const char* ExtractAnnounceableText(void* control,
                                           char* outBuf, size_t bufSize) {
    if (!control || bufSize < 2) return nullptr;

    // 1. Tooltip on the base class — works for any control that has one.
    const char* tip;
    uint32_t    tipLen;
    int         id;
    if (ReadControlNameFields(control, tip, tipLen, id) &&
        tipLen > 0 && tipLen < bufSize) {
        memcpy(outBuf, tip, tipLen);
        outBuf[tipLen] = '\0';
        return "tooltip";
    }

    // 2. CSWGuiButton (most common — also covers CharButton, ActivatedButton,
    //    ButtonToggle since those embed Button at offset 0 AND the engine's
    //    AsButton override returns `this` for them). Try the literal
    //    CExoString first, then the TLK str_ref fallback.
    if (void* btn = CallDowncast(control, kVtableAsButton)) {
        if (ExtractTextOrStrRef(btn, kButtonTextOffset, kButtonStrRefOffset,
                                outBuf, bufSize)) {
            return "button";
        }
    }

    // 3. CSWGuiButtonToggle — defensive fallback if AsButton misses it.
    //    Same offsets because ButtonToggle.button is at offset 0.
    if (void* tgl = CallDowncast(control, kVtableAsButtonToggle)) {
        if (ExtractTextOrStrRef(tgl, kButtonTextOffset, kButtonStrRefOffset,
                                outBuf, bufSize)) {
            return "buttontoggle";
        }
    }

    // 4. CSWGuiLabel.
    if (void* lbl = CallDowncast(control, kVtableAsLabel)) {
        if (ExtractTextOrStrRef(lbl, kLabelTextOffset, kLabelStrRefOffset,
                                outBuf, bufSize)) {
            return "label";
        }
    }

    // 5. CSWGuiLabelHilight — same offsets (Label embedded at 0).
    if (void* hil = CallDowncast(control, kVtableAsLabelHilight)) {
        if (ExtractTextOrStrRef(hil, kLabelTextOffset, kLabelStrRefOffset,
                                outBuf, bufSize)) {
            return "labelhilight";
        }
    }

    // CSWGuiEditbox / CSWGuiSlider / CSWGuiListBox — the engine doesn't
    // expose AsX accessors for these in GuiControlMethods, so we have no
    // safe way to detect them by downcast. Reading at speculative offsets
    // would risk AVs on smaller controls. Instead, OnSetActiveControl logs
    // the vtable pointer for any control we can't extract — we'll
    // accumulate vtable addresses across sessions, map them to classes via
    // SARIF, and add per-class extraction as a separate step.

    return nullptr;
}

// Multi-line "blob" listbox readout. The Options-Gameplay settings list is
// the canonical case: CSWGuiListBox.controls.size == 1, the single child is a
// CSWGuiLabel whose CExoString contains all visible setting names joined by
// Speak `text` only if it differs from what we last spoke on this channel.
// Dedup is the only filter: in the first session we used interrupt=true and
// NVDA went fully silent in chargen (every utterance got cut off mid-word
// because focus events fire ~10/sec while panels initialize). Switching to
// interrupt=false (queued) lets NVDA finish each line at its own pace; the
// user can still skip forward with NVDA's own ctrl-key shortcut.
//
// Channels keep dedup state independent so a listbox row update doesn't
// silence the parent panel's announcement and vice-versa:
//   0 = panel SetActiveControl
//   1 = listbox row SetActiveControl
static void SpeakIfChanged(int channel, const char* text) {
    static char s_last[2][256] = {{0}, {0}};
    if (channel < 0 || channel >= 2 || !text) return;
    if (strncmp(s_last[channel], text, sizeof(s_last[channel])) == 0) return;
    strncpy_s(s_last[channel], text, _TRUNCATE);
    tolk::Speak(text, /*interrupt=*/false);
}

// Read vtable[0..7] from a control. Used as a diagnostic: dumping the vtable
// pointer + a few entries lets us correlate unknown controls back to specific
// CSWGui subclasses via the SARIF (each class has a unique vtable address).
// Caller is responsible for null-checks.
static void DumpControlVtable(void* control, char* out, size_t outSize) {
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) {
        snprintf(out, outSize, "vtable=NULL");
        return;
    }
    snprintf(out, outSize,
             "vtable=%p [0]=%p [4]=%p [20]=%p [22]=%p",
             vtable, vtable[0], vtable[4], vtable[20], vtable[22]);
}

// Container offsets verified against Lane's SARIF (DATATYPE entries for
// CSWGuiPanel and CSWGuiListBox). CExoArrayList layout:
//   +0x00  T**      data         (heap array of element pointers)
//   +0x04  int      size
//   +0x08  int      capacity
//
// CSWGuiPanel.activeControl is at +0x1c — current focused child (read by
// our SetActiveControl mid-function hook before the SET).
// CSWGuiPanel.controls is at +0x20 — list of every direct child control.
// CSWGuiListBox.controls is at +0x29c — list of row controls. Listbox cursor
// state is in three shorts immediately after the controls array:
//   +0x2c4  short    items_per_page
//   +0x2c6  short    selection_index   ← which row is "current"
//   +0x2c8  short    top_visible_index ← scroll offset
constexpr size_t kPanelActiveControlOffset      = 0x1c;
constexpr size_t kPanelControlsOffset           = 0x20;
constexpr size_t kListBoxControlsOffset         = 0x29c;
constexpr size_t kListBoxBitFlagsOffset         = 0x2bc;
constexpr size_t kListBoxItemsPerPageOffset     = 0x2c4;
constexpr size_t kListBoxSelectionIndexOffset   = 0x2c6;
constexpr size_t kListBoxTopVisibleIndexOffset  = 0x2c8;

// CSWGuiControl.extent is an inline CSWGuiExtent (16 bytes) at +0x4:
//   +0x0  left    int
//   +0x4  top     int
//   +0x8  width   int
//   +0xC  height  int
constexpr size_t kControlExtentOffset = 0x4;

struct CExoArrayList {
    void** data;
    int    size;
    int    capacity;
};

// ============================================================================
// Unified-cursor menu navigation (Phase 1+2 — see docs/menu-nav-design.md).
// ============================================================================

// CSWGuiManager surfaces verified via Lane's SARIF database.
// `*kAddrGuiManagerPtr` holds the live GuiManager singleton; `MoveMouseToPosition`
// is the single primitive that does cursor + hover refresh in one call.
constexpr uintptr_t kAddrGuiManagerPtr        = 0x007A39F4;
constexpr uintptr_t kAddrMoveMouseToPosition  = 0x0040c790;
typedef void (__thiscall* PFN_MoveMouseToPosition)(void* gm, int x, int y);

// CSWGuiPanel::SetActiveControl @ 0x40a630 — committing selection to a panel.
// MoveMouseToPosition only updates hover state; panel.activeControl lags
// behind the cursor unless we explicitly set it. Enter / F1 activates
// panel.activeControl, so without this call the engine activates the
// previously-clicked button instead of the cursor target.
constexpr uintptr_t kAddrPanelSetActiveControl = 0x0040a630;
typedef void (__thiscall* PFN_PanelSetActiveControl)(void* panel, void* control);

// CSWGuiManager click-sim primitives (Phase 3 — see docs/menu-nav-design.md).
//
// Decompilation summary (from Lane's gzf):
//
//   HandleLMouseDown(this, int press) @ 0x40c570
//     - XORs `press & 1` into bit 0 of state field at +0x1c.
//     - If no control is currently held: broadcasts event 0x1f9 to panels
//       (input class 3 only), runs UpdateMouseOverControl + tooltip-disable,
//       then dispatches via the engine's mouseOverControl (manager+0x8) ->
//       vtable[6] = control's HandleLMouseDown. Button-class HandleLMouseDown
//       calls CaptureMouse, setting mouseHeldControl (manager+0x10).
//     - Returns 1 if dispatched, 0 if a click is already in progress.
//
//   HandleLMouseUp(this) @ 0x40a170
//     - If a control is held (manager+0x10 non-null AND manager.mouse_held==1):
//       dispatches mouseHeldControl -> vtable[7] = control's HandleLMouseUp.
//       That's the function that fires the actual activate event.
//     - Clears bit 0 of manager+0x1c. Returns 1 if dispatched, 0 otherwise.
//
// Calling them in sequence after MoveMouseToPosition has settled the cursor
// runs the engine's natural click pipeline end-to-end. This is the
// replacement for the SetActiveControl-based path that crashed at mgr+5
// (see docs/tab-crash-investigation.md): we no longer skip the prelude
// HandleLMouseDown writes, so whatever invariant the engine maintains
// across press+release stays intact.
constexpr uintptr_t kAddrManagerLMouseDown = 0x0040c570;
constexpr uintptr_t kAddrManagerLMouseUp   = 0x0040a170;
typedef int (__thiscall* PFN_ManagerLMouseDown)(void* gm, int press);
typedef int (__thiscall* PFN_ManagerLMouseUp)(void* gm);

// CSWGuiControl::HandleInputEvent — vtable slot 15 (offset 0x3C) per the
// GuiControlMethods struct in docs/llm-docs/re/swkotor.exe.h. Direct fire of
// event 0x27 (KEYBOARD_F1 / "activate") on a control invokes its onClick
// pipeline without going through the click-sim path. Needed when the button
// we want to activate is rendered behind/under a CSWGuiListBox whose extent
// covers the button's hit area: the engine's hit-test resolves the cursor
// to the listbox instead of the button, and click-sim ends up clicking the
// wrong control. By firing 0x27 directly on the button we bypass hit-test
// entirely. Confirmed safe for non-tab buttons (Schliess, OK, Standard,
// chain targets in sub-dialogs).
constexpr int kVtableHandleInputEvent = 15;
typedef void (__thiscall* PFN_ControlHandleInputEvent)(void* this_, int code, int state);
constexpr int kInputActivate = 0x27;   // KEYBOARD_F1, the engine's activate code

static void FireActivate(void* control) {
    if (!control) return;
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) return;
    auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
        vtable[kVtableHandleInputEvent]);
    if (!fn) return;
    fn(control, kInputActivate, 1);
}

// Logical input codes received pre-translation by CSWGuiManager::HandleInputEvent.
// 0xb6 / 0xb7 are the engine's "nav-prev / nav-next" actions, emitted on arrow
// presses inside menus. Consuming them prevents the engine's broken `.gui`
// focus-cycle from running. Other key codes (Tab, Enter, mouse, F-keys) pass
// through normally and reach the engine's existing handlers.
constexpr int kInputNavUp   = 0xb6;
constexpr int kInputNavDown = 0xb7;

// 0xb5 / 0xbb both translate to KEYBOARD_F1 (the engine's "confirm/activate"
// virtual code). We don't consume Enter — the engine's normal activation
// pipeline runs the button's onClick — but we force panel.activeControl to
// match the chain target on press so the right button gets activated.
// MoveMouseToPosition's hover→active path is unreliable (no event in popups,
// snaps back to default tab in the Options panel), so a one-shot commit on
// Enter press is the cleanest way to close that gap without the per-frame
// oscillation an explicit setActive in OnUpdate caused.
constexpr int kInputEnter1 = 0xb5;
constexpr int kInputEnter2 = 0xbb;

// LOGICAL_ESC / "back/cancel" — both translate to 0x28 (KEYBOARD_F2) via
// ManagerTranslateCode @0x0040c8e0. The engine's natural Esc handling for
// Options sub-dialogs (CSWGuiOptionsXxx::HandleInputEvent → PopModalPanel)
// is silently failing for reasons we couldn't pin down — Esc reaches our
// hook as logical(223), translates to 0x28, but no panel close fires. We
// route it explicitly via FireActivate(Schliess) instead. See Esc handler
// in OnHandleInputEvent for trigger conditions.
constexpr int kInputEsc1 = 0xb4;
constexpr int kInputEsc2 = 0xdf;

// Chain state. g_currentPanel is updated in OnSetActiveControl. g_chainPanel
// is rebound lazily per arrow press if the focused panel has changed since the
// last navigation. A null current panel disables chain nav (fall through to
// the engine for unsupported screens — e.g. the title-screen K/L cycle still
// works, per Decision 7).
//
// The chain is FLAT: each entry is one navigable button, even if that button
// lives inside a CSWGuiListBox (Options-style sub-dialogs put their settings
// as button children of a listbox at panel.controls[N]). Building the chain
// recurses one level into listboxes when their controls.size > 1 and their
// children are buttons — sub-dialogs in KOTOR don't nest deeper than that.
// Entries are sorted by extent.top ascending so arrow-down walks visually
// top-to-bottom (panel.controls order doesn't always match visual order:
// in Feedback-Optionen, Schliess+Standard come before the settings listbox
// in panel.controls but render below it).
struct ChainEntry {
    void* control;
    int   cx;
    int   cy;
};
constexpr int kMaxChainEntries = 64;
static ChainEntry g_chain[kMaxChainEntries];
static void* g_currentPanel = nullptr;
static void* g_chainPanel   = nullptr;
static int   g_chainIndex   = 0;
static int   g_chainCount   = 0;

// Deferred MoveMouseToPosition. Called from OnHandleInputEvent would recurse
// through HandleMouseMove → UpdateMouseOverControl mid-input-dispatch — same
// class of toxicity as the listbox-entry hooks from session 4. Defer to the
// next CSWGuiManager::Update tick (~16ms at 60fps; inaudible). Tolk speech
// still fires synchronously from the input hook so the audible response feels
// instantaneous.
static bool  g_pendingCursorMove = false;
static int   g_pendingX = 0;
static int   g_pendingY = 0;
static void* g_pendingTarget = nullptr;   // for self-dedup in OnSetActiveControl

// Deferred click-sim. When set, OnUpdate dispatches click directly to
// g_pendingClickTarget via its vtable[6] (HandleLMouseDown) and vtable[7]
// (HandleLMouseUp). We bypass the manager's HandleLMouseDown wrapper because
// its UpdateMouseOverControl misidentifies the cursor's hit target on tabbed
// panels (consistent 45-px shift in Options panel — see chat investigation
// in patch-20260502-114830.log). The button's own HandleLMouseDown still
// runs CaptureMouse + state setup, and HandleLMouseUp fires the actual
// onClick — that's what the manager's wrapper would have called once it
// resolved the right button. We just provide the target ourselves.
//
// Distinct from g_pendingActivate (vtable[15] FireActivate path): tabs gate
// on `is_active` which only HandleLMouseDown sets, so direct activate no-ops
// for tabs (see comment block below).
static bool  g_pendingClick       = false;
static void* g_pendingClickTarget = nullptr;

// Deferred direct-activate via vtable[15].HandleInputEvent(0x27, 1). Used for
// targets whose click hit-area is covered by an overlapping CSWGuiListBox
// (Schliess. and Standard inside Options sub-dialogs, chain-navigated
// settings buttons, etc.) — click-sim would land on the listbox instead of
// the button. Direct activate bypasses hit-test entirely.
static bool  g_pendingActivate       = false;
static void* g_pendingActivateTarget = nullptr;

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// Tabbed-panel detection state (Options-menu style: a CSWGuiListBox at
// controls[0] holds the current tab's content, button cluster after [0] = tabs).
// Detection runs in OnListBoxSetActiveControl and identifies the layout so the
// per-line virtual-cursor mode can engage on arrow keys. Tab cycling itself is
// no longer an explicit handler — the click-sim primitive (Phase 3) replaces
// the SetActiveControl-based path that crashed at mgr+5 (see
// docs/tab-crash-investigation.md).
//
// Detection deliberately keys off "controls[0] is a non-empty CSWGuiListBox"
// rather than "panel has buttons" — the main menu also has buttons but no
// active listbox, and arrow-keys must continue to drive chain navigation
// there. kVtableListBox is the observed Steam-1.0.3 vtable for CSWGuiListBox;
// future builds will need updating if Lane's SARIF reports a different value.
constexpr uintptr_t kVtableListBox  = 0x0073E840;

static void*      g_tabbedPanel       = nullptr;  // panel currently in tabbed mode
static int        g_tabsStart         = -1;       // first tab-button index in panel.controls
static int        g_tabsCount         = 0;        // number of contiguous tab buttons

// Cursor-y offset to compensate for the engine's hit-test shift in tab-cluster
// panels. MoveMouseToPosition(x, y) on the Options panel hit-tests at the
// button whose center is at (y - tabSpacing) — consistently 45 px on Steam
// 1.0.3 (matches the tab pitch). Cause is unverified (best guess: cursor
// hotspot coord-system mismatch — see chat investigation in
// patch-20260502-122734.log line 91 where before=NULL after=Gameplay confirms
// MoveMouseToPosition itself produces the shifted hit-test result, not stale
// engine state). Real mouse usage is unaffected, so the engine ships fine for
// sighted players. We compensate by adding this offset to y when warping the
// cursor to a tab button. Computed in RebindChain from the chain's tab-cluster
// spacing; 0 for non-tabbed panels (main menu, popups, sub-dialogs) — those
// panels' MoveMouseToPosition already hits where it should.
static int        g_tabClickOffsetY   = 0;

// Virtual-line cursor over the listbox's multi-line blob. The Options listbox
// has controls.size == 1 with all settings concatenated by '\n' into a single
// CSWGuiLabel row. We can't activate individual lines (engine has no per-line
// click target — see project_listbox_click_flow.md) but we can present them as
// readable navigable items.
constexpr int kMaxVirtualLines  = 32;
constexpr int kMaxVirtualLineLen = 256;
static char g_virtualLines[kMaxVirtualLines][kMaxVirtualLineLen];
static int  g_virtualLineCount = 0;
static int  g_virtualLineIdx   = -1;  // -1 = not yet entered (cursor at tab level)

// Read panel.activeControl @ +0x1c. Used to anchor the chain index when we
// rebind to a panel — start at the engine's current selection so the first
// arrow press doesn't snap the cursor away from where the user was looking.
static void* ReadPanelActiveControl(void* panel) {
    if (!panel) return nullptr;
    return *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(panel) + kPanelActiveControlOffset);
}

// Center pixel of a control's hit area. Returns false on null control or
// degenerate extent (zero/negative width/height — sometimes seen on hidden
// panels and templated control prototypes).
static bool GetControlCenter(void* control, int& outCx, int& outCy) {
    if (!control) return false;
    auto* ext = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlExtentOffset);
    int width  = ext[2];
    int height = ext[3];
    if (width <= 0 || height <= 0) return false;
    outCx = ext[0] + width  / 2;
    outCy = ext[1] + height / 2;
    return true;
}

// True if the control is button-like (CSWGuiButton or its subclasses
// CharButton / ActivatedButton / ButtonToggle). MoveMouseToPosition's
// hover→active promotion path is safe for buttons but crashes when the
// active control is a label (verified: navigating onto the main-menu
// "Neue Inhalte verfügbar…" label froze the game). Used to filter chain
// navigation to selectable controls only — Decision 3's "filter from
// evidence" trigger met by that crash.
//
// Long-term: replace with a proper CSWGuiControl::GetIsSelectable call
// (vtable lookup at 0x4189d0) to also include editbox / listbox / etc.
// The button-only filter is a conservative starting point that handles
// menus correctly.
static bool IsChainNavigable(void* control) {
    if (!control) return false;
    if (CallDowncast(control, kVtableAsButton)        != nullptr) return true;
    if (CallDowncast(control, kVtableAsButtonToggle)  != nullptr) return true;
    return false;
}

// Linear scan g_chain for `control`. Returns the chain index or -1.
// Used to anchor the chain at the engine's currently active control on
// rebind so the first arrow press doesn't snap the cursor to chain[0].
static int FindChainEntry(void* control) {
    if (!control) return -1;
    for (int i = 0; i < g_chainCount; ++i) {
        if (g_chain[i].control == control) return i;
    }
    return -1;
}

// Pull the announceable text of a control (tooltip → button → label → ...);
// fall back to "control N" using the control's id field. Never silently
// drops — per feedback_never_silence_fallback_announcement.md.
static void AnnounceControl(void* control) {
    if (!control) return;
    char text[256];
    const char* source = ExtractAnnounceableText(control, text, sizeof(text));
    if (source) {
        tolk::Speak(text, /*interrupt=*/false);
        return;
    }
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + 0x50);
    char placeholder[64];
    snprintf(placeholder, sizeof(placeholder), "control %d", id);
    tolk::Speak(placeholder, /*interrupt=*/false);
}

// First focus into a panel speaks the panel's "title" — the first label-like
// child we can find — so the user knows which menu they're in. Subsequent
// per-control announcements still fire from OnSetActiveControl as the user
// navigates, so this is just the entry banner, not a layout dump.
//
// Heuristic: walk panel.controls in order, return the text of the first
// CSWGuiLabel / CSWGuiLabelHilight child with announceable text. Buttons and
// other interactive controls are skipped — they get announced through the
// regular focus path. If no label exists we stay silent and rely on the
// SetActiveControl announcement of the focused child to orient the user.
static void AnnouncePanelTitle(void* panel) {
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* child = list->data[i];
        if (!child) continue;
        if (CallDowncast(child, kVtableAsLabel) == nullptr &&
            CallDowncast(child, kVtableAsLabelHilight) == nullptr) {
            continue;
        }
        char text[256];
        if (ExtractAnnounceableText(child, text, sizeof(text))) {
            acclog::Write("Panel title parent=%p label=%p text=\"%s\"",
                          panel, child, text);
            tolk::Speak(text, /*interrupt=*/false);
            return;
        }
    }
}

// Detect whether `panel` is laid out as Options-style: a CSWGuiListBox at
// controls[0] (currently displayed tab content) followed by a contiguous
// cluster of buttons (the tab strip). Returns true and fills outStart/outCount
// with the cluster's first index and length on success.
//
// Refusing the detection when the listbox is empty keeps main-menu-style
// panels (which also have a CSWGuiListBox at [0], for the news scroller, but
// no tab content) on the existing chain-navigation path.
static bool DetectTabsCluster(void* panel, int& outStart, int& outCount) {
    outStart = -1;
    outCount = 0;
    if (!panel) return false;

    auto* panelList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!panelList->data || panelList->size < 2) return false;

    void* lb = panelList->data[0];
    if (!lb) return false;
    void** vt = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(vt) != kVtableListBox) return false;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList->data || lbList->size <= 0) return false;

    int n = panelList->size > 256 ? 256 : panelList->size;
    int start = -1, end = -1;
    for (int i = 1; i < n; ++i) {
        void* c = panelList->data[i];
        if (c && IsChainNavigable(c)) {
            if (start < 0) start = i;
            end = i;
        } else if (start >= 0) {
            break;  // first non-navigable after the cluster ends it
        }
    }
    if (start < 0 || (end - start + 1) < 2) return false;
    outStart = start;
    outCount = end - start + 1;
    return true;
}

// Reset all tabbed-mode state. Called when the focused panel changes to a
// different one — the new panel may or may not be tabbed; OnListBoxSetActive-
// Control re-runs detection lazily on its first listbox event.
static void ResetTabbedState() {
    g_tabbedPanel      = nullptr;
    g_tabsStart        = -1;
    g_tabsCount        = 0;
    g_virtualLineCount = 0;
    g_virtualLineIdx   = -1;
}

// Split `text` on '\n' into g_virtualLines[]. Truncates oversize lines to fit;
// caps total line count at kMaxVirtualLines (Options Gameplay tab has 8, no
// observed listbox blob > 16 in any KOTOR menu — 32 is comfortable headroom).
static void ParseVirtualLines(const char* text) {
    g_virtualLineCount = 0;
    g_virtualLineIdx   = -1;
    if (!text) return;
    const char* p = text;
    while (*p && g_virtualLineCount < kMaxVirtualLines) {
        const char* end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= kMaxVirtualLineLen) len = kMaxVirtualLineLen - 1;
        memcpy(g_virtualLines[g_virtualLineCount], p, len);
        g_virtualLines[g_virtualLineCount][len] = '\0';
        ++g_virtualLineCount;
        if (!end) break;
        p = end + 1;
    }
}

// Find the "close" button on a panel — the back/Schliess button we'd click
// to dismiss the panel. Used by the Esc handler to route Esc to the same
// FireActivate primitive the user triggers manually by Enter-ing Schliess.
//
// Match heuristic: scan panel.controls for a navigable button whose text
// starts with "Schliess" (German "Schließen"), "Close" (English), or "OK"
// (universal "accept and dismiss" in confirmation dialogs). KOTOR ships
// German + English localizations; both texts present here cover the cases
// we care about. Returns the control pointer or nullptr if none found.
static void* FindCloseButton(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!IsChainNavigable(c)) continue;
        char text[256];
        if (!ExtractAnnounceableText(c, text, sizeof(text))) continue;
        if (strncmp(text, "Schliess", 8) == 0 ||
            strncmp(text, "Close",    5) == 0 ||
            strncmp(text, "OK",       2) == 0) {
            return c;
        }
    }
    return nullptr;
}

// True if `control` is one of the current panel's tab-cluster buttons.
// Used to gate the cursor-y offset (g_tabClickOffsetY) — only tab buttons
// suffer the engine's hit-test shift; close/standard buttons in the same
// panel are in a different x column or row layout and don't need the offset.
static bool IsTabButton(void* control) {
    if (!control || !g_tabbedPanel || g_tabsCount < 2) return false;
    auto* tlist = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
    if (!tlist || !tlist->data) return false;
    for (int i = g_tabsStart;
         i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
        if (tlist->data[i] == control) return true;
    }
    return false;
}

// Append a navigable control to the chain (skipping null/degenerate-extent).
// Internal helper for RebindChain.
static void AppendChainEntry(void* control) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!IsChainNavigable(control))       return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    g_chain[g_chainCount++] = { control, cx, cy };
}

// (Re)bind the chain to the currently focused panel.
//
// Walks panel.controls; for each entry:
//   - direct navigable button → append.
//   - CSWGuiListBox with controls.size > 1 → recurse one level into its
//     children, appending any navigable buttons found there.
// Then sorts entries by extent.top ascending (visual top-to-bottom order)
// because panel.controls order doesn't match visual order in sub-dialogs.
//
// Finally anchors g_chainIndex on the engine's current activeControl when
// present, so the first arrow press moves one step from where the user
// was, not from chain[0].
static void RebindChain(void* panel) {
    g_chainPanel  = panel;
    g_chainIndex  = 0;
    g_chainCount  = 0;
    if (!panel) return;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;

    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;

        // Direct navigable button — typical case (tabs, OK/Cancel, menu items).
        if (IsChainNavigable(c)) {
            AppendChainEntry(c);
            continue;
        }

        // Listbox with size > 1 — sub-dialogs put their settings here as
        // button children. Recurse one level. Listboxes with size == 1
        // are descriptive multi-line label blobs (the inline tab preview
        // or the per-setting hint pane); their single child is a label,
        // not a button, so AppendChainEntry would skip it anyway.
        void** vt = *reinterpret_cast<void***>(c);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableListBox) {
            auto* lbList = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(c) + kListBoxControlsOffset);
            if (lbList && lbList->data && lbList->size > 1) {
                int lbN = lbList->size > 256 ? 256 : lbList->size;
                for (int j = 0; j < lbN; ++j) {
                    AppendChainEntry(lbList->data[j]);
                }
            }
        }
    }

    // Insertion sort by cy ascending. Stable; n^2 is fine for n<=64.
    for (int i = 1; i < g_chainCount; ++i) {
        for (int j = i; j > 0 && g_chain[j].cy < g_chain[j-1].cy; --j) {
            ChainEntry tmp = g_chain[j];
            g_chain[j]   = g_chain[j-1];
            g_chain[j-1] = tmp;
        }
    }

    // Compute g_tabClickOffsetY from adjacent tab entries' visual spacing.
    // The chain is now sorted top-to-bottom, so the first two consecutive
    // tab-cluster entries give us the pitch directly. Non-tabbed panels (no
    // g_tabbedPanel set, or only one tab) keep offset=0 — their hit-test
    // works without compensation.
    g_tabClickOffsetY = 0;
    if (g_tabbedPanel == panel && g_tabsCount >= 2) {
        int firstTabIdx = -1;
        for (int i = 0; i < g_chainCount; ++i) {
            if (!IsTabButton(g_chain[i].control)) continue;
            if (firstTabIdx < 0) {
                firstTabIdx = i;
            } else {
                int spacing = g_chain[i].cy - g_chain[firstTabIdx].cy;
                if (spacing > 0) g_tabClickOffsetY = spacing;
                break;
            }
        }
    }

    // Anchor at active. ReadPanelActiveControl reads panel.activeControl
    // (only direct panel children); listbox-internal selection isn't
    // exposed there, so when the user enters a sub-dialog with focus on
    // a listbox child the anchor falls through to chain[0].
    void* active = ReadPanelActiveControl(panel);
    int   idx    = FindChainEntry(active);
    g_chainIndex = (idx >= 0) ? idx : 0;

    acclog::Write("Chain rebind panel=%p count=%d index=%d active=%p tabOffsetY=%d",
                  panel, g_chainCount, g_chainIndex, active, g_tabClickOffsetY);
    for (int i = 0; i < g_chainCount; ++i) {
        char text[256];
        const char* src = ExtractAnnounceableText(g_chain[i].control,
                                                  text, sizeof(text));
        acclog::Write("Chain   [%d] %p (%d,%d) %s text=\"%s\"",
                      i, g_chain[i].control, g_chain[i].cx, g_chain[i].cy,
                      src ? src : "?", src ? text : "");
    }
}

// Walk a CExoArrayList<CSWGuiControl*> embedded at parent+offset and log every
// child. Used as a diagnostic when the focused panel/listbox changes — gives us
// the full set of widgets on the screen, not just whatever arrow keys reach.
//
// `label` is a short tag that prefixes every line (e.g. "Panel", "ListBox").
// Iteration is capped at 256 entries to limit damage from a corrupt size field
// (defensive: the SARIF datatypes are authoritative but a struct-layout
// regression on a future engine version would otherwise spin forever).
static void WalkChildren(const char* label, void* parent, size_t offset) {
    if (!parent) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(parent) + offset);
    if (!list->data || list->size <= 0) {
        acclog::Write("%s walk parent=%p children=0", label, parent);
        return;
    }
    int count = list->size;
    if (count > 256) {
        acclog::Write("%s walk parent=%p size_oob=%d (capped)", label, parent, count);
        count = 256;
    }
    acclog::Write("%s walk parent=%p children=%d", label, parent, list->size);
    for (int i = 0; i < count; ++i) {
        void* child = list->data[i];
        if (!child) {
            acclog::Write("%s   [%d]=NULL", label, i);
            continue;
        }
        int id = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(child) + 0x50);
        char text[256];
        const char* source = ExtractAnnounceableText(child, text, sizeof(text));
        if (source) {
            acclog::Write("%s   [%d] %p id=%d src=%s text=\"%s\"",
                          label, i, child, id, source, text);
        } else {
            char vtbl[160];
            DumpControlVtable(child, vtbl, sizeof(vtbl));
            acclog::Write("%s   [%d] %p id=%d src=none %s",
                          label, i, child, id, vtbl);
        }
    }
}

// CSWGuiPanel::SetActiveControl — hooked mid-function at 0x0040a638.
// At hook entry: EDI = this (the panel), ESI = param_1 (the new active
// control, possibly null when the panel is deactivating selection).
//
// This is the canonical focus-change signal: fires once per actual change,
// covers arrow-key nav + mouse + programmatic. Speaks the new control's
// tooltip text or, as a placeholder, "control <id>" while we work out how
// to extract subclass-specific labels.
//
// Logging policy:
//   * Resolved events (text extracted) are throttled — they're noisy when
//     the user is just navigating.
//   * Unresolved events (src=none) are ALWAYS logged with the control's
//     vtable pointer, because that's the data we need to identify which
//     subclasses fall through (Slider, Editbox, ListBox row, etc.).
//   * NULL newControl events are also throttled.
extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl) {
    EnsureTolkInitialized();

    static int n = 0;
    ++n;

    // Track the currently-focused panel for the chain-navigation handler.
    // Even NULL newControl events update this — what matters is which panel
    // the manager is dispatching focus on.
    void* prevPanel = g_currentPanel;
    g_currentPanel = panel;

    // Tabbed-mode state survives transitions into sub-dialogs of the tabbed
    // panel. The tab strip lives on the PARENT panel (e.g. Options) and is
    // still the right thing for Tab/Shift+Tab to operate on while the user
    // is inside one of its sub-dialogs (Spieleinstellungen, Feedback-Optionen,
    // etc.); clicking a different tab from inside a sub-dialog is the
    // engine's normal "switch tabs" gesture for mouse users. So we only
    // clear the per-event virtual-line cursor on panel change; g_tabbedPanel/
    // g_tabsStart/g_tabsCount persist until DetectTabsCluster overwrites
    // them on a different tabbed panel, or a long-running session re-arms
    // them naturally.
    if (panel != prevPanel) {
        g_virtualLineCount = 0;
        g_virtualLineIdx   = -1;
    }

    // First focus event into a previously-unseen panel: dump every child
    // control on it. Lets us see widgets the user can't reach with arrow
    // keys (mouse-only labels, hidden tabs, off-cursor inputs, etc.).
    static void* s_lastPanel = nullptr;
    if (panel && panel != s_lastPanel) {
        s_lastPanel = panel;
        WalkChildren("Panel", panel, kPanelControlsOffset);
    }

    // First focus into a new panel: speak its title (label child) once.
    // The focused control's announcement below still fires, so the user
    // hears "<panel title>, <focused control>" on entry.
    if (panel && panel != g_lastTitledPanel) {
        g_lastTitledPanel = panel;
        AnnouncePanelTitle(panel);
    }

    if (!newControl) {
        acclog::Write("SetActiveControl #%d panel=%p newControl=NULL", n, panel);
        return;
    }

    // Self-dedup: if this SetActiveControl was caused by our deferred
    // MoveMouseToPosition, we already announced the target from the input
    // hook. Skip the Tolk path here and clear the pending marker. This is
    // self-suppression, not engine suppression — the engine wouldn't fire
    // SetActiveControl on consumed arrow keys at all (the wrapper JMPs past
    // dispatch). It only fires here when our own move triggered the engine
    // to reselect.
    if (newControl == g_pendingTarget) {
        acclog::Write("SetActiveControl #%d panel=%p new=%p (self-dedup; cursor sync)",
                      n, panel, newControl);
        g_pendingTarget = nullptr;
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newControl) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newControl, text, sizeof(text));

    if (source) {
        acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d src=%s text=\"%s\"",
                      n, panel, newControl, id, source, text);
        SpeakIfChanged(/*channel=*/0, text);
    } else {
        // Always log unknowns — these are the events we need to debug.
        char vtbl[160];
        DumpControlVtable(newControl, vtbl, sizeof(vtbl));
        acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d src=none %s",
                      n, panel, newControl, id, vtbl);
        // Bypass SpeakIfChanged dedup deliberately: a non-readable focus
        // change deserves *some* announcement every time, even if it's
        // nonsense. Better to hear "control 11" repeated than to silently
        // skip a focus event the user can't otherwise perceive.
        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder), "control %d", id);
        tolk::Speak(placeholder, /*interrupt=*/false);
    }
}

// CSWGuiListBox::SetActiveControl — hooked mid-function at 0x0041c16b.
// Function entry per Lane's SARIF:
//   void __thiscall CSWGuiListBox::SetActiveControl(CSWGuiControl* param_1, int param_2)
//
// Bytes from 0x0041c160 (DumpBytes.java):
//   8b 44 24 08          MOV EAX, [ESP+8]   ; param_2 (int) before push
//   56                   PUSH ESI
//   8b f1                MOV ESI, ECX       ; this → ESI
//   8b 4c 24 08          MOV ECX, [ESP+8]   ; param_1 (post-push, was [ESP+4])
//   50 51 8d 8e 9c 02 00 00     ← hook here, all three args in registers
//   50                   PUSH EAX           ; param_2
//   51                   PUSH ECX           ; param_1
//   8d 8e 9c 02 00 00    LEA  ECX, [ESI+0x29c]  ; embedded sub-object
//
// Cut covers PUSH EAX (1) + PUSH ECX (1) + complete LEA (6) = 8 bytes. All
// three instructions are position-independent → safe to relocate.
//
// Listbox row navigation does NOT bubble up to CSWGuiPanel::SetActiveControl,
// so without this hook we miss every per-row focus event inside listboxes
// (race / class / portrait pickers in chargen, save-game list, etc.).
extern "C" void __cdecl OnListBoxSetActiveControl(void* listBox, void* newRow,
                                                  int param2) {
    EnsureTolkInitialized();

    static int n = 0;
    ++n;

    // First event for a previously-unseen listbox: dump every row control.
    // Tells us whether the listbox holds N separate child widgets (one per
    // visible line) or aggregates everything into a single multi-line label
    // — the central question for the Options Gameplay panel.
    static void* s_lastListBox = nullptr;
    if (listBox && listBox != s_lastListBox) {
        s_lastListBox = listBox;
        WalkChildren("ListBox", listBox, kListBoxControlsOffset);
    }

    // Always log the listbox's internal cursor + flags state. selection_index
    // distinguishes scroll-mode (-1, set when bit_flags & 0x200) from
    // selection-mode (>=0). controls_size tells us how many real rows exist:
    // for the multi-line-blob settings listbox this is 1 even though the
    // user sees 8 visual lines.
    if (listBox) {
        auto* base = reinterpret_cast<unsigned char*>(listBox);
        short itemsPerPage = *reinterpret_cast<short*>(
            base + kListBoxItemsPerPageOffset);
        short selIdx       = *reinterpret_cast<short*>(
            base + kListBoxSelectionIndexOffset);
        short topVisible   = *reinterpret_cast<short*>(
            base + kListBoxTopVisibleIndexOffset);
        uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(
            base + kListBoxBitFlagsOffset);
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            base + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : -1;
        acclog::Write("ListBox::cursor list=%p sel=%d top=%d perPage=%d "
                      "size=%d flags=0x%x",
                      listBox, selIdx, topVisible, itemsPerPage,
                      ctrlsSize, bitFlags);
    }

    if (!newRow) {
        acclog::Write("ListBox::SetActiveControl #%d list=%p newRow=NULL p2=%d",
                      n, listBox, param2);
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newRow) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newRow, text, sizeof(text));

    if (source) {
        acclog::Write("ListBox::SetActiveControl #%d list=%p row=%p id=%d "
                      "p2=%d src=%s text=\"%s\"",
                      n, listBox, newRow, id, param2, source, text);

        // Lazy tabbed-mode detection: first listbox event after a panel
        // change probes whether the focused panel has the Options-style
        // listbox-at-[0] + button-cluster layout. If so we arm the tab/
        // virtual-line nav path and silence blob speech (the user navigates
        // lines explicitly with arrow keys instead).
        if (g_currentPanel && g_tabbedPanel != g_currentPanel) {
            int tabsStart = -1, tabsCount = 0;
            if (DetectTabsCluster(g_currentPanel, tabsStart, tabsCount)) {
                g_tabbedPanel = g_currentPanel;
                g_tabsStart   = tabsStart;
                g_tabsCount   = tabsCount;
                acclog::Write("Tabbed panel detected: panel=%p tabsStart=%d tabsCount=%d",
                              g_currentPanel, tabsStart, tabsCount);
            }
        }

        if (strchr(text, '\n')) {
            // Multi-line listbox blob (Options-style: all settings concatenated
            // by '\n' into a single CSWGuiLabel row). In tabbed mode we parse
            // the lines into a virtual cursor and speak them one-at-a-time on
            // arrow keys. In non-tabbed mode we silence them too — bulk
            // enumeration is too noisy. If a non-tabbed multi-line listbox
            // ever needs per-line nav, that's a future feature.
            if (g_tabbedPanel == g_currentPanel) {
                ParseVirtualLines(text);
                acclog::Write("ListBox blob silenced (tabbed mode); %d virtual lines parsed",
                              g_virtualLineCount);
            } else {
                int lines = 1;
                for (const char* p = text; *p; ++p) if (*p == '\n') ++lines;
                acclog::Write("ListBox blob silenced (non-tabbed); lines=%d",
                              lines);
            }
        } else {
            SpeakIfChanged(/*channel=*/1, text);
        }
    } else {
        char vtbl[160];
        DumpControlVtable(newRow, vtbl, sizeof(vtbl));
        acclog::Write("ListBox::SetActiveControl #%d list=%p row=%p id=%d "
                      "p2=%d src=none %s",
                      n, listBox, newRow, id, param2, vtbl);
        // Same rule as the panel path: never silence a fallback announcement.
        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder), "row %d", id);
        tolk::Speak(placeholder, /*interrupt=*/false);
    }
}

// CSWGuiControl::HandleFocusChange — hooked mid-function at 0x41896b.
// Demoted to log-only. The panel-level SetActiveControl hook above is the
// real announcement signal; HandleFocusChange fires twice per navigation
// (old loses focus + new gains focus) so speaking from here would echo.
extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    const char* tip; uint32_t tipLen; int id;
    ReadControlNameFields(thisPtr, tip, tipLen, id);
    acclog::Write("HandleFocusChange #%d this=%p p1=%d id=%d tip[%u]=\"%s\"",
                  n, thisPtr, param_1, id, tipLen,
                  (tip && tipLen > 0) ? tip : "");
}

// CSWGuiManager::HandleInputEvent — hooked mid-function at 0x0040c907.
// This is the GUI manager's central input dispatcher: every key / mouse event
// the engine routes to any GUI surface passes through here before being
// virtual-dispatched to the active panel's per-class override. One hook
// covers every screen (title, Options, chargen, in-game menus, dialog,
// save/load) — replaces the old CSWGuiMainMenu-only hook at 0x67b395.
//
// We hook BEFORE the param_2 == 0 early-out, so we see press AND release
// edges. param_2 is logged as `val=` (0 = release, non-zero = press).
//
// At hook entry: ECX = this, EBX = param_1 (InputIndices key/button code),
// EAX = param_2 (state).
extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;

    // Chain navigation: consume nav-up / nav-down on key-down. We only handle
    // press edges (param_2 != 0) so key-up events still pass through cleanly.
    // Other keys (Tab, Enter, mouse, F-keys) always pass through; activation
    // comes free from the engine via the normal click pipeline once the
    // cursor is over the chain target.
    bool consumed = false;

    // Enter-press activation. Two activation primitives, picked per-target:
    //
    //   * **Tab buttons** in the tabbed parent panel (Options:
    //     Gameplay/Auto-Pause/Grafik/Sound/Feedback) require the full
    //     click pipeline because their handler (CSWGuiInGameOptions::OnGraphics
    //     @0x006aad90, etc.) gates on `param_1->is_active != 0`. That flag is
    //     set by HandleLMouseDown but NOT by direct vtable[15] dispatch — so
    //     FireActivate on a tab silently no-ops. Verified via Ghidra
    //     decompilation. Click-sim (cursor warp + Down + Up) is the only way
    //     in.
    //
    //   * **Everything else** (sub-dialog setting buttons, OK/Cancel popups,
    //     main menu) uses direct vtable[15] activate. It bypasses hit-test, so
    //     buttons covered by overlapping listbox extents (chain targets in
    //     sub-dialogs) still fire — click-sim there resolves to the listbox
    //     instead of the button (Up=0, no dispatch).
    //
    // Debounce: refuse to queue another op if one is already pending.
    // Stops Enter typematic from queuing back-to-back activations on adjacent
    // OnUpdate frames — the only re-entry path left after deleting the
    // Tab-cycle two-step. Single user-paced presses always go through.
    //
    // Consume Enter either way so the engine doesn't ALSO fire F1 against
    // panel.activeControl (which can be stale or wrong).
    if (param_2 != 0 &&
        (param_1 == kInputEnter1 || param_1 == kInputEnter2) &&
        g_currentPanel != nullptr &&
        g_chainPanel == g_currentPanel &&
        g_chainCount > 0 &&
        g_chainIndex < g_chainCount)
    {
        ChainEntry& e = g_chain[g_chainIndex];

        bool isTabButton = false;
        if (g_tabbedPanel && g_tabsCount >= 2) {
            auto* tlist = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
            if (tlist && tlist->data) {
                for (int i = g_tabsStart;
                     i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
                    if (tlist->data[i] == e.control) { isTabButton = true; break; }
                }
            }
        }

        if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
            acclog::Write("Enter: op already pending; ignoring (target=%p)", e.control);
            consumed = true;
        } else if (isTabButton) {
            int cursorY = e.cy;
            if (g_tabClickOffsetY > 0) cursorY += g_tabClickOffsetY;
            g_pendingX           = e.cx;
            g_pendingY           = cursorY;
            g_pendingTarget      = e.control;
            g_pendingClickTarget = e.control;
            g_pendingCursorMove  = true;
            g_pendingClick       = true;
            acclog::Write("Enter click-sim panel=%p index=%d target=%p cursorY=%d (tab)",
                          g_currentPanel, g_chainIndex, e.control, cursorY);
            consumed = true;
        } else {
            g_pendingActivate       = true;
            g_pendingActivateTarget = e.control;
            acclog::Write("Enter activate panel=%p index=%d target=%p",
                          g_currentPanel, g_chainIndex, e.control);
            consumed = true;
        }
    }

    // Tab key falls through to the engine. We previously implemented a
    // Tab-cycle two-step here (FireActivate(Schliess) → schedule click-sim on
    // next tab), but it crashed: compressing close-old-subdialog + open-new
    // into a single ~16ms window pressured the GL driver (Grafik specifically
    // re-runs gamma-ramp via OnMoveGammaSlider in its constructor) until the
    // NVIDIA driver fast-failed mid-frame. See docs/tab-crash-investigation.md.
    //
    // Replacement UX: arrow keys walk tab buttons via the chain (they're
    // already in panel.controls of the tabbed parent), Enter opens a tab via
    // click-sim (above), Esc closes a sub-dialog via FireActivate(Schliess)
    // (handler below) — keeping every action user-paced and never collapsing
    // close+open into the same frame.

    // Arrow keys: flat chain navigation. Chain is built from panel.controls
    // + listbox children (one level) sorted by extent.top, so arrow-down
    // walks visually top-to-bottom through every navigable button — including
    // tab buttons on the parent Options panel and settings that live as
    // button children of a CSWGuiListBox in sub-dialogs.
    if (param_2 != 0 &&
        (param_1 == kInputNavUp || param_1 == kInputNavDown) &&
        g_currentPanel != nullptr)
    {
        if (g_currentPanel != g_chainPanel) {
            RebindChain(g_currentPanel);
        }
        if (g_chainCount > 0) {
            int delta = (param_1 == kInputNavDown) ? +1 : -1;
            int newIndex = g_chainIndex + delta;
            if (newIndex < 0)              newIndex = 0;
            if (newIndex >= g_chainCount)  newIndex = g_chainCount - 1;
            g_chainIndex = newIndex;

            ChainEntry& e = g_chain[g_chainIndex];
            AnnounceControl(e.control);
            int cursorY = e.cy;
            if (IsTabButton(e.control) && g_tabClickOffsetY > 0) {
                cursorY += g_tabClickOffsetY;
            }
            g_pendingX          = e.cx;
            g_pendingY          = cursorY;
            g_pendingTarget     = e.control;
            g_pendingCursorMove = true;
            acclog::Write("Chain step panel=%p index=%d/%d target=%p center=(%d,%d) cursorY=%d %s",
                          g_chainPanel, g_chainIndex, g_chainCount,
                          e.control, e.cx, e.cy, cursorY,
                          param_1 == kInputNavDown ? "DOWN" : "UP");
            // Always consume nav-up/nav-down on a panel with a non-empty chain.
            consumed = true;
        }
    }

    // Esc / Backspace (when bound to "back/cancel" via the in-game Key Mapping
    // screen): close the current sub-dialog by FireActivate-ing its Schliess
    // button. The engine's natural Esc → CSWGuiOptionsXxx::HandleInputEvent(0x28)
    // → PopModalPanel path is silently failing in our environment (Esc reaches
    // the manager and translates correctly, but no close fires — verified in
    // patch-20260502-102803.log lines 311-312). FireActivate(Schliess) is the
    // same primitive that already works when the user manually navigates to
    // Schliess and presses Enter, so routing Esc through it gives deterministic
    // close behavior.
    //
    // Gated on g_currentPanel != g_tabbedPanel: only fires inside a sub-dialog
    // of a tabbed parent. On the parent Options panel itself, Esc passes
    // through to the engine (which opens the "Möchtest du wirklich aufhören?"
    // quit confirmation — desired existing behavior).
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        g_currentPanel != nullptr &&
        g_tabbedPanel != nullptr &&
        g_currentPanel != g_tabbedPanel)
    {
        if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
            acclog::Write("Esc: op already pending; ignoring");
            consumed = true;
        } else {
            void* closeBtn = FindCloseButton(g_currentPanel);
            if (closeBtn) {
                g_pendingActivate       = true;
                g_pendingActivateTarget = closeBtn;
                acclog::Write("Esc close panel=%p Schliess=%p",
                              g_currentPanel, closeBtn);
                consumed = true;
            } else {
                acclog::Write("Esc on sub-dialog panel=%p but no close button found; "
                              "passing through", g_currentPanel);
            }
        }
    }

    int translated = ManagerTranslateCode(param_1);
    const char* tag = consumed ? " CONSUMED" : "";
    if (translated != param_1) {
        acclog::Write("HandleInputEvent #%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                      n, thisPtr, param_1,
                      InputIndexName(translated), translated, param_2, tag);
    } else {
        acclog::Write("HandleInputEvent #%d this=%p key=%s(%d) val=%d%s",
                      n, thisPtr, InputIndexName(param_1), param_1, param_2, tag);
    }
    return consumed ? 1 : 0;
}

// CSWGuiManager::Update — hooked mid-function at 0x40ce76. Per-frame tick run
// once after input dispatch by CClientExoAppInternal::MainLoop. Used as a safe
// callback site for the deferred MoveMouseToPosition triggered by chain
// navigation: the engine's input pipeline is NOT mid-flight here, so cursor
// updates can recurse through HandleMouseMove without re-entrancy.
//
// The cut byte is `mov eax, [ebp+0x8c]` (a panel-list field load); EBP is the
// manager pointer (the engine's `mov ebp, ecx` at 0x40ce74 happens before our
// hook). We pass EBP as the parameter for clarity even though we also have
// the global at 0x7A39F4 — both resolve to the same singleton.
extern "C" void __cdecl OnUpdate(void* /*gmFromEbp*/) {
    if (!g_pendingCursorMove && !g_pendingClick && !g_pendingActivate) return;

    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) {
        acclog::Write("Update: pending move/click/activate but GuiManager singleton is NULL");
        g_pendingCursorMove     = false;
        g_pendingClick          = false;
        g_pendingActivate       = false;
        g_pendingTarget         = nullptr;
        g_pendingActivateTarget = nullptr;
        return;
    }

    // CSWGuiManager mouseOverControl pointer at +0x8 (per the decompilation
    // in docs/menu-nav-design.md). Reading it directly lets us verify what
    // the engine's hit-test resolved the cursor to — the difference between
    // "click landed on tab T" and "click landed on the inline listbox" is
    // invisible from cursor coords alone.
    auto getMouseOver = [&]() -> void* {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gm) + 0x8);
    };

    if (g_pendingCursorMove) {
        g_pendingCursorMove = false;
        auto move = reinterpret_cast<PFN_MoveMouseToPosition>(kAddrMoveMouseToPosition);
        // Capture mouseOver BEFORE the move. Disambiguates whether
        // MoveMouseToPosition itself produces the 45-px-shifted hit-test
        // result (before==something-else, after==shifted-button) vs. the
        // engine pre-setting mouseOver during panel init (before==after,
        // never refreshed by our move at all). See chat.
        void* moBefore = getMouseOver();
        move(gm, g_pendingX, g_pendingY);
        acclog::Write("Update: MoveMouseToPosition(%d, %d) target=%p mouseOver before=%p after=%p",
                      g_pendingX, g_pendingY, g_pendingTarget,
                      moBefore, getMouseOver());
    }

    // Click-sim via manager's HandleLMouseDown/Up. Dispatches against
    // mouseOverControl, which on Options-style tabbed panels resolves to
    // the button one step above the chain target (consistent 45-px hit-test
    // shift — see chat investigation). That activates the wrong tab.
    //
    // Direct vtable[6]/[7] on the chain target was tried as a workaround
    // (commit reverted) — it crashes the game on the second tab+ click.
    // The button's own HandleLMouseDown/Up depend on manager-side state
    // (probably the +0x1c mouse_held bit and/or other setup we'd skip).
    // Need a different approach for the off-by-1; for now keep the original
    // pipeline so behavior is at least stable.
    if (g_pendingClick) {
        g_pendingClick = false;
        g_pendingClickTarget = nullptr;
        auto down = reinterpret_cast<PFN_ManagerLMouseDown>(kAddrManagerLMouseDown);
        auto up   = reinterpret_cast<PFN_ManagerLMouseUp>(kAddrManagerLMouseUp);
        void* moBefore = getMouseOver();
        int dResult = down(gm, /*press=*/1);
        void* moAfterDown = getMouseOver();
        int uResult = up(gm);
        void* moAfterUp = getMouseOver();
        acclog::Write("Update: click-sim Down=%d Up=%d at (%d,%d) target=%p "
                      "mouseOver before=%p afterDown=%p afterUp=%p",
                      dResult, uResult, g_pendingX, g_pendingY, g_pendingTarget,
                      moBefore, moAfterDown, moAfterUp);
    }

    // Direct activate via vtable[15].HandleInputEvent(0x27, 1). Bypasses
    // hit-test, so a button covered by a listbox extent (e.g. Schliess.
    // in an Options sub-dialog) still fires its onClick when we target it.
    if (g_pendingActivate) {
        g_pendingActivate = false;
        void* tgt = g_pendingActivateTarget;
        g_pendingActivateTarget = nullptr;
        acclog::Write("Update: FireActivate target=%p", tgt);
        FireActivate(tgt);
    }

}

// Read a snapshot of the listbox's cursor / flags / size into a string. Shared
// between the click and key handlers so all listbox events log the same fields.
static void DumpListBoxState(void* listBox, char* out, size_t outSize) {
    if (!listBox) {
        snprintf(out, outSize, "list=NULL");
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(listBox);
    short selIdx       = *reinterpret_cast<short*>(base + kListBoxSelectionIndexOffset);
    short topVisible   = *reinterpret_cast<short*>(base + kListBoxTopVisibleIndexOffset);
    short itemsPerPage = *reinterpret_cast<short*>(base + kListBoxItemsPerPageOffset);
    uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(base + kListBoxBitFlagsOffset);
    auto* ctrls        = reinterpret_cast<CExoArrayList*>(base + kListBoxControlsOffset);
    int ctrlsSize      = ctrls ? ctrls->size : -1;
    snprintf(out, outSize,
             "list=%p sel=%d top=%d perPage=%d size=%d flags=0x%x",
             listBox, selIdx, topVisible, itemsPerPage, ctrlsSize, bitFlags);
}

// CSWGuiListBox::HandleLMouseDown — entry hook @0x0041c4a0. Click press.
extern "C" void __cdecl OnListBoxLMouseDown(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::LMouseDown #%d %s", n, state);
}

// CSWGuiListBox::HandleLMouseUp — entry hook @0x0041a700. Click release; this
// is where the click action commits and the row's callback fires. Pair with
// the next OnListBoxSetSelectedControl / OnListBoxSetActiveControl events to
// see the full chain.
extern "C" void __cdecl OnListBoxLMouseUp(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::LMouseUp #%d %s", n, state);
}

// CSWGuiListBox::HandleInputEvent — entry hook @0x0041ce20. Per-listbox key
// dispatch. Fires only when the listbox is the focused control AND the
// engine routes the key down to it. We don't extract param_1/param_2 here
// (would need stack-source path which is broken upstream) — correlate by
// timestamp with the manager-level HandleInputEvent log line that fired
// just before.
extern "C" void __cdecl OnListBoxHandleInput(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::HandleInputEvent #%d %s", n, state);
}

// CSWGuiListBox::SetSelectedControl — entry hook @0x0041c040. Fires whenever
// the listbox's selection index changes, regardless of source (keyboard, mouse,
// programmatic). Reads the OLD selection_index pre-update; the next
// OnListBoxSetActiveControl event will reveal the new value.
extern "C" void __cdecl OnListBoxSetSelectedControl(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::SetSelectedControl #%d %s (pre-update)", n, state);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        acclog::Init(hinstDLL);
        DWORD n = GetEnvironmentVariableA(
            "KOTOR_VERSION_SHA", g_versionSha, sizeof(g_versionSha));
        if (n == 0 || n >= sizeof(g_versionSha)) {
            strncpy_s(g_versionSha, "(unset)", _TRUNCATE);
        }
        acclog::Write("DLL_PROCESS_ATTACH sha=%s", g_versionSha);
        // Tolk init is intentionally deferred to first hook fire — see header.
    }
    return TRUE;
}
