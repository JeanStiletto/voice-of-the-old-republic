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
//
// SEH-wrapped because the per-tick monitors (MonitorPanelContents,
// MonitorDialogReplies) walk panels[] and call this on every child control.
// During an engine teardown — e.g. inside FireActivate("Spielen") starting
// the new-game flow, or FireActivate("OK") on the quit-confirm — the engine
// frees a panel's child controls synchronously while the panel pointer can
// still resolve from panels[] on the next MainLoop tick. Reading the freed
// control's vtable slot then yields garbage (observed: 0xbf800000 = float
// -1.0 bits, the engine reused the page for model data) and dereferencing
// vtable[index] crashes. Treating any fault as "not this subclass" lets the
// caller skip the stale control without taking down the process.
typedef void* (__thiscall* PFN_Downcast)(void* this_);
static void* CallDowncast(void* control, int vtableIndex) {
    if (!control) return nullptr;
    __try {
        void** vtable = *reinterpret_cast<void***>(control);
        if (!vtable) return nullptr;
        auto fn = reinterpret_cast<PFN_Downcast>(vtable[vtableIndex]);
        if (!fn) return nullptr;
        return fn(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
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

// Element-state field offsets (verified via Ghidra decomp of Draw/SetSelected/
// HandleInputEvent for each class):
//
//   CSWGuiButtonToggle.field2_0x1c8 — uint32; bit 0 = on/off. HandleInputEvent
//                                     XOR's bit 0 with 1 on activate; SetSelected
//                                     masks to bit 0; Draw branches on (& 1) to
//                                     pick the rendered border.
//   CSWGuiSlider.max_value (Lane-named) at +0x70 — uint32, slider max.
//   CSWGuiSlider.cur_value (Lane-named) at +0x74 — uint32, current slider value.
//                                                  HandleInputEvent calls
//                                                  SetCurValue on inc/dec keys.
constexpr size_t kButtonToggleStateOffset = 0x1c8;
constexpr size_t kSliderMaxValueOffset    = 0x70;
constexpr size_t kSliderCurValueOffset    = 0x74;

// CSWGuiText layout (from swkotor.exe.h + decompiled CSWGuiText::Initialize
// at 0x00417310 confirmed via headless Ghidra against Lane's gzf):
//   +0x00  vtable
//   +0x04  extent (16 bytes)
//   +0x14  CAurGUIStringInternal* gui_string
//   +0x18  text_params (CSWGuiTextParams):
//             +0x00 (=0x18 in text)  CExoString text  (c_string + length)
//             +0x08 (=0x20 in text)  int str_ref
//             ...
//             +0x50 (=0x68 in text)  CSWGuiText* text_object
//
// For CSWGuiLabel: control(0x5C)+border(0x74)+text@(0xD0).
//   gui_string ptr     @ 0xD0 + 0x14 = 0xE4
//   text_params.text   @ 0xE8 (c_string + length)
//   text_params.str_ref @ 0xF0
//   text_params.text_object @ 0xE8 + 0x50 = 0x138
//
// For CSWGuiButton: navigable(0x6C)+border(0x74)+border(0x74)+text@(0x154).
//   gui_string ptr     @ 0x154 + 0x14 = 0x168
//   text_params.text   @ 0x154 + 0x18 = 0x16C
//   text_params.str_ref @ 0x174
//   text_params.text_object @ 0x16C + 0x50 = 0x1BC
//
// **gui_string is the ground-truth source.** CSWGuiText::Initialize calls
// NewCAurGUIString(text_params.text.c_string, ...) which constructs a
// CAurGUIStringInternal whose constructor copies the c_string into a
// heap-allocated buffer at offset +0x14 within CAurGUIStringInternal
// (Ghidra-named field5_0x14). CSWGuiText::Draw reads ONLY from gui_string
// (it ignores text_params at draw time). For overridden subclasses where
// the inline text_params CExoString and strref are empty (CSWGuiInGameMenu's
// 8 icon labels at vtable=0x0073E8E8 are the canonical case — verified via
// 584 speculative-read miss events in patch-20260502-190936.log on the
// previous build), gui_string still holds the rendered c_string.
constexpr size_t kLabelGuiStringPtrOffset  = 0xE4;
constexpr size_t kLabelTextObjectOffset    = 0x138;
constexpr size_t kButtonGuiStringPtrOffset = 0x168;
constexpr size_t kButtonTextObjectOffset   = 0x1BC;
constexpr size_t kTextObjectTextOffset     = 0x18;   // CSWGuiText.text_params.text
constexpr size_t kTextObjectStrRefOffset   = 0x20;   // CSWGuiText.text_params.str_ref
constexpr size_t kAurGuiStringCStrOffset   = 0x14;   // CAurGUIStringInternal.field5

// Slider class identity by vtable address. Resolved via SARIF xrefs:
// 0x0073E9D0 is referenced by CSWGuiSlider's constructor (0x41bb0d) and
// destructor (0x41bb9d) — i.e. it's the slider's vftable. Sliders have no
// AsSlider downcast accessor in GuiControlMethods, so vtable equality is
// the only safe identity check.
constexpr uintptr_t kVtableSlider = 0x0073E9D0;

// CSWGuiListBox vtable. Same identity-by-vtable pattern as the slider:
// no AsListBox accessor exists in GuiControlMethods, so we identify by
// vtable equality. Used by chain navigation (RebindChain recurses one
// level into multi-row listboxes), the tabbed-panel detector, and the
// listbox-content extraction path in ExtractAnnounceableText (which walks
// a listbox's rows when the panel walk encounters one as a child — the
// recurring `vtable=0073E840 src=none` cases in our log are listbox
// containers wrapping the actual message text).
constexpr uintptr_t kVtableListBox = 0x0073E840;

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

// Read a null-terminated c_string from CAurGUIStringInternal. Bypasses all
// the CExoString / strref / text_object indirection — goes straight to the
// engine's actually-rendered string.
//
// Confirmed by decompiling CSWGuiText::Initialize (0x00417310) and
// CAurGUIStringInternal::CAurGUIStringInternal (0x0045B990): the constructor
// allocates a heap buffer and copies the c_string into it at offset +0x14
// of CAurGUIStringInternal (Ghidra-named field5_0x14). CSWGuiText::Draw at
// 0x00416240 reads ONLY through gui_string — text_params is unused at draw
// time. So gui_string is the ground-truth source whenever a control has
// rendered visible text.
//
// `guiStringPtrOffset` is the offset within `control` to the
// `CAurGUIStringInternal*` field (i.e. CSWGuiText.gui_string). For
// CSWGuiLabel that's 0xE4; for CSWGuiButton that's 0x168.
//
// SEH-guarded because the gui_string pointer can be null in transient
// init states and the indirected c_string pointer can theoretically point
// at freed memory across module transitions.
static bool ReadGuiString(void* control, size_t guiStringPtrOffset,
                          char* outBuf, size_t bufSize) {
    if (!control || bufSize < 2) return false;
    bool got = false;
    __try {
        void* guiString = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + guiStringPtrOffset);
        if (!guiString) return false;
        char* str = *reinterpret_cast<char**>(
            reinterpret_cast<unsigned char*>(guiString) + kAurGuiStringCStrOffset);
        if (!str) return false;
        size_t len = 0;
        while (len < bufSize - 1 && str[len] != '\0') ++len;
        if (len == 0) return false;
        memcpy(outBuf, str, len);
        outBuf[len] = '\0';
        got = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("ReadGuiString SEH for control=%p offset=0x%x",
                      control, (unsigned)guiStringPtrOffset);
        got = false;
    }
    return got;
}

// Resolve text trying every known path:
//   1. CAurGUIStringInternal at gui_string (the engine's actual render
//      source — works for any control with visible rendered text)
//   2. inline CExoString (text_params.text)
//   3. strref → TLK lookup
//   4. text_object indirection (rarely used, kept as defensive fallback)
//
// gui_string is tried first because it reflects the rendered state — the
// other paths can be empty for overridden subclasses (CSWGuiInGameMenu icon
// labels are the canonical case) but gui_string is always populated when
// the control has visible text.
//
// SEH-guarded reads across the indirection paths.
static bool ExtractTextOrStrRefIndirect(void* control,
                                        size_t cexoOffset, size_t strRefOffset,
                                        size_t textObjectOffset,
                                        char* outBuf, size_t bufSize) {
    // gui_string offset for label and button differ by inline CSWGuiText
    // start offset; derive from cexoOffset to avoid threading another
    // parameter through every call site (gui_string ptr is at
    // text.+0x14 = (cexo - 0x18) + 0x14 = cexo - 4).
    size_t guiStringPtrOffset = cexoOffset - 4;
    if (ReadGuiString(control, guiStringPtrOffset, outBuf, bufSize)) {
        return true;
    }
    if (ExtractTextOrStrRef(control, cexoOffset, strRefOffset, outBuf, bufSize)) {
        return true;
    }
    bool got = false;
    __try {
        void* textObj = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(control) + textObjectOffset);
        if (textObj) {
            got = ExtractTextOrStrRef(textObj,
                                      kTextObjectTextOffset,
                                      kTextObjectStrRefOffset,
                                      outBuf, bufSize);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("text_object indirection SEH for control=%p "
                      "(textObjectOffset=0x%x)", control, (unsigned)textObjectOffset);
        got = false;
    }
    return got;
}

// Element-class identity helpers. Used by ExtractAnnounceableText to decide
// which subclass-specific field reads to perform, and by IsChainNavigable to
// decide which controls keyboard chain-navigation can land on.
static bool IsToggle(void* control) {
    return CallDowncast(control, kVtableAsButtonToggle) != nullptr;
}

static bool IsSlider(void* control) {
    if (!control) return false;
    void** vt = *reinterpret_cast<void***>(control);
    return reinterpret_cast<uintptr_t>(vt) == kVtableSlider;
}

static bool IsListBox(void* control) {
    if (!control) return false;
    void** vt = *reinterpret_cast<void***>(control);
    return reinterpret_cast<uintptr_t>(vt) == kVtableListBox;
}

// Read CSWGuiButtonToggle.field2_0x1c8's bit 0. Decompiled HandleInputEvent
// XOR's this bit on every activate, and SetSelected masks param to bit 0;
// Draw branches on (field2 & 1) to pick which border to render. So the
// rendered "checked" state is exactly bit 0 of this field.
static bool ReadToggleState(void* toggle) {
    return (ReadU32(toggle, kButtonToggleStateOffset) & 1u) != 0;
}

// Forward declarations: ExtractAnnounceableText decorates its output via
// FindSiblingLabel (slider category prefix) and LookupCycleCategory (cycle
// value-display prefix); both are defined later in the file alongside the
// chain machinery. g_currentPanel is the focused panel pointer maintained
// by OnSetActiveControl.
extern void* g_currentPanel;
static const char* FindSiblingLabel(void* panel, void* control,
                                    char* outBuf, size_t bufSize);
static const char* LookupCycleCategory(void* control);
static bool IsChainNavigable(void* control);
// Forward decl helper to keep ExtractAnnounceableText (defined here) decoupled
// from the PanelKind enum's full definition (defined later in the file). The
// per-kind hardcoded-name fallback for InGameMenu uses this wrapper.
static bool IsPanelKindInGameMenu(void* panel);

// Forward decl: find the panel in the manager's panels[] that owns `control`
// (i.e. has it in its controls[]). Used as a fallback when callers of
// ExtractAnnounceableText don't pass an explicit owner — needed because
// g_currentPanel only updates on SetActiveControl, so transient flows like
// fg-flipping-back-after-modal-close leave it pointing at the wrong panel
// while the chain rebinds against a different one (logged in
// patch-20260502-214100.log: chain dump for InGameMenu printed text="" / src=?
// because g_currentPanel was still TutorialBox).
static void* FindOwningPanel(void* control);

static const char* ExtractAnnounceableText(void* control,
                                           char* outBuf, size_t bufSize,
                                           void* ownerPanel = nullptr) {
    if (!control || bufSize < 2) return nullptr;

    const char* source = nullptr;

    // 1. Tooltip on the base class — works for any control that has one.
    //    SEH-wrapped: the field at +0x28 holds a `char*` that on a stale
    //    (freed-and-reused) control can be a bogus address; the memcpy
    //    would then fault reading the source. CallDowncast already SEH-
    //    protects steps 2-5; this covers the single read path that doesn't
    //    go through it.
    __try {
        const char* tip;
        uint32_t    tipLen;
        int         id;
        if (ReadControlNameFields(control, tip, tipLen, id) &&
            tipLen > 0 && tipLen < bufSize) {
            memcpy(outBuf, tip, tipLen);
            outBuf[tipLen] = '\0';
            source = "tooltip";
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        source = nullptr;
    }

    // 2. CSWGuiButton (most common — also covers CharButton, ActivatedButton,
    //    ButtonToggle since those embed Button at offset 0 AND the engine's
    //    AsButton override returns `this` for them). Tries inline CExoString,
    //    then TLK str_ref, then text_object indirection — the last covers
    //    classes whose text routes through CSWGuiText.text_params.text_object
    //    rather than the inline CExoString.
    if (!source) {
        if (void* btn = CallDowncast(control, kVtableAsButton)) {
            if (ExtractTextOrStrRefIndirect(btn,
                                            kButtonTextOffset,
                                            kButtonStrRefOffset,
                                            kButtonTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "button";
            }
        }
    }

    // 3. CSWGuiButtonToggle — defensive fallback if AsButton misses it.
    //    Same offsets because ButtonToggle.button is at offset 0.
    if (!source) {
        if (void* tgl = CallDowncast(control, kVtableAsButtonToggle)) {
            if (ExtractTextOrStrRefIndirect(tgl,
                                            kButtonTextOffset,
                                            kButtonStrRefOffset,
                                            kButtonTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "buttontoggle";
            }
        }
    }

    // 4. CSWGuiLabel.
    if (!source) {
        if (void* lbl = CallDowncast(control, kVtableAsLabel)) {
            if (ExtractTextOrStrRefIndirect(lbl,
                                            kLabelTextOffset,
                                            kLabelStrRefOffset,
                                            kLabelTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "label";
            }
        }
    }

    // 5. CSWGuiLabelHilight — same offsets (Label embedded at 0).
    if (!source) {
        if (void* hil = CallDowncast(control, kVtableAsLabelHilight)) {
            if (ExtractTextOrStrRefIndirect(hil,
                                            kLabelTextOffset,
                                            kLabelStrRefOffset,
                                            kLabelTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "labelhilight";
            }
        }
    }

    // 6. CSWGuiSlider — no AsSlider downcast accessor exists; detect by
    //    vtable identity. cur_value / max_value are Lane-named uint32 fields.
    //    The slider widget itself has no inline category text (its CExoString
    //    is the rendered "X von Y"); the category name lives on a sibling
    //    CSWGuiLabel rendered to the left of the slider. Look it up via
    //    FindSiblingLabel and prepend.
    if (!source && IsSlider(control)) {
        uint32_t cur = ReadU32(control, kSliderCurValueOffset);
        uint32_t max = ReadU32(control, kSliderMaxValueOffset);
        char label[128];
        if (g_currentPanel &&
            FindSiblingLabel(g_currentPanel, control, label, sizeof(label))) {
            snprintf(outBuf, bufSize, "%s %u von %u", label, cur, max);
        } else {
            snprintf(outBuf, bufSize, "%u von %u", cur, max);
        }
        source = "slider";
    }

    // 7. CSWGuiListBox content. The listbox is a container; its "text" is
    //    the concatenation of its row controls' texts. Many in-game modals
    //    (CSWGuiMessageBox-style — including the recurring 07434E40 OK/Cancel
    //    in our log, and the quit-confirmation "Möchtest du wirklich
    //    aufhören?") put their message text in a single listbox row rather
    //    than directly in a panel label, so without this path the modal
    //    appears as src=none. Recursion is bounded to one level — listbox
    //    rows are not themselves listboxes in observed layouts, so we only
    //    try button/label extraction per row, never re-enter the listbox
    //    branch.
    //
    //    Capped at 8 rows to keep the announcement digestible (long save-
    //    game lists aren't candidates for this code path; they have rows
    //    that already announce individually via OnListBoxSetActiveControl).
    if (!source && IsListBox(control)) {
        auto* lb = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(control) + kListBoxControlsOffset);
        if (lb && lb->data && lb->size > 0) {
            int n = lb->size > 8 ? 8 : lb->size;
            outBuf[0] = '\0';
            size_t off = 0;
            for (int i = 0; i < n; ++i) {
                void* row = lb->data[i];
                if (!row) continue;
                char rowText[256];
                bool got = false;
                if (void* btn = CallDowncast(row, kVtableAsButton)) {
                    got = ExtractTextOrStrRef(btn, kButtonTextOffset,
                                              kButtonStrRefOffset,
                                              rowText, sizeof(rowText));
                }
                if (!got) {
                    if (void* lbl = CallDowncast(row, kVtableAsLabel)) {
                        got = ExtractTextOrStrRef(lbl, kLabelTextOffset,
                                                  kLabelStrRefOffset,
                                                  rowText, sizeof(rowText));
                    }
                }
                if (!got) {
                    if (void* hil = CallDowncast(row, kVtableAsLabelHilight)) {
                        got = ExtractTextOrStrRef(hil, kLabelTextOffset,
                                                  kLabelStrRefOffset,
                                                  rowText, sizeof(rowText));
                    }
                }
                if (!got) continue;
                size_t rowLen = strnlen(rowText, sizeof(rowText));
                if (rowLen == 0) continue;
                size_t needed = (off > 0 ? 2 : 0) + rowLen + 1;
                if (off + needed >= bufSize) break;
                if (off > 0) {
                    outBuf[off++] = ' ';
                    outBuf[off++] = ' ';
                }
                memcpy(outBuf + off, rowText, rowLen);
                off += rowLen;
                outBuf[off] = '\0';
            }
            if (off > 0) source = "listbox";
        }
    }

    // 8. Speculative text read for known label/button vtable overrides.
    //    Some classes override AsLabel/AsButton in their vtable so that
    //    CallDowncast returns null even though the class IS label-like or
    //    button-like at the field-offset level. The InGameMenu icons are
    //    the canonical case: 8 sibling labels at vtable=0x0073E8E8 and
    //    8 image-only buttons at vtable=0x0073E658, and our standard
    //    extraction returns nullptr for all of them (panel-walk shows
    //    src=none).
    //
    //    For each entry in kKnownVtableOverrides we try a direct read at
    //    the standard label/button text offsets, guarded by SEH so that
    //    reading at an offset that's NOT a CExoString doesn't crash the
    //    game. If the structure is different, we'll get a SEH exception
    //    and silently fall through to the placeholder path.
    //
    //    Allowlist gating keeps speculative reads off random unknown
    //    vtables — we only fire on classes we've observed needing this.
    if (!source) {
        struct VtableOverrideInfo {
            uintptr_t vtable;
            bool      tryLabel;
            bool      tryButton;
            const char* tag;
        };
        static const VtableOverrideInfo k_knownOverrides[] = {
            // Sibling labels of in-game-menu icons (Equipment, Inventory,
            // Character, ...) — observed children [0..7] of CSWGuiInGameMenu.
            // Also chargen wizard step-number decorations.
            { 0x0073E8E8, true,  false, "label-spec" },
            // Image-only buttons (in-game-menu icons children [8..15],
            // chargen class icons, portrait-picker arrows).
            { 0x0073E658, false, true,  "button-spec" },
        };
        void** vt = *reinterpret_cast<void***>(control);
        uintptr_t vta = reinterpret_cast<uintptr_t>(vt);
        for (const auto& ov : k_knownOverrides) {
            if (ov.vtable != vta) continue;
            char text[256];
            bool got = false;
            if (ov.tryLabel) {
                __try {
                    // Path A: inline CExoString at standard label offset.
                    if (ReadCExoString(control, kLabelTextOffset,
                                       text, sizeof(text))) {
                        got = true;
                    }
                    // Path B: strref at standard label offset → TLK.
                    if (!got) {
                        uint32_t strref = ReadU32(control, kLabelStrRefOffset);
                        got = LookupTlk(strref, text, sizeof(text));
                    }
                    // Path C: text_object indirection. CSWGuiLabel.text.
                    // text_params.text_object is a CSWGuiText* at +0x138; if
                    // non-null, read its text_params.text (CExoString @+0x18)
                    // or text_params.str_ref (@+0x20). Many labelhilights
                    // route their rendered text through this pointer rather
                    // than the inline CExoString.
                    if (!got) {
                        void* textObj = *reinterpret_cast<void**>(
                            reinterpret_cast<unsigned char*>(control)
                            + kLabelTextObjectOffset);
                        if (textObj) {
                            if (ReadCExoString(textObj, kTextObjectTextOffset,
                                               text, sizeof(text))) {
                                got = true;
                            } else {
                                uint32_t strref = ReadU32(textObj,
                                                          kTextObjectStrRefOffset);
                                got = LookupTlk(strref, text, sizeof(text));
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("Speculative label read SEH for vtable=0x%x "
                                  "control=%p", (unsigned)vta, control);
                    got = false;
                }
            }
            if (!got && ov.tryButton) {
                __try {
                    // Path A: inline CExoString at standard button offset.
                    if (ReadCExoString(control, kButtonTextOffset,
                                       text, sizeof(text))) {
                        got = true;
                    }
                    // Path B: strref at standard button offset → TLK.
                    if (!got) {
                        uint32_t strref = ReadU32(control, kButtonStrRefOffset);
                        got = LookupTlk(strref, text, sizeof(text));
                    }
                    // Path C: text_object indirection at +0x1BC (button-side).
                    if (!got) {
                        void* textObj = *reinterpret_cast<void**>(
                            reinterpret_cast<unsigned char*>(control)
                            + kButtonTextObjectOffset);
                        if (textObj) {
                            if (ReadCExoString(textObj, kTextObjectTextOffset,
                                               text, sizeof(text))) {
                                got = true;
                            } else {
                                uint32_t strref = ReadU32(textObj,
                                                          kTextObjectStrRefOffset);
                                got = LookupTlk(strref, text, sizeof(text));
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("Speculative button read SEH for vtable=0x%x "
                                  "control=%p", (unsigned)vta, control);
                    got = false;
                }
            }
            if (got) {
                size_t tlen = strnlen(text, sizeof(text));
                if (tlen > 0 && tlen + 1 <= bufSize) {
                    memcpy(outBuf, text, tlen + 1);
                    source = ov.tag;
                    acclog::Write("Speculative read hit: vtable=0x%x control=%p "
                                  "text=\"%s\"", (unsigned)vta, control, outBuf);
                    break;
                }
                // got=true but text is empty/whitespace — log so we can see
                // the read path is wired but the field is genuinely empty.
                acclog::Write("Speculative read empty: vtable=0x%x control=%p "
                              "(read returned but text was empty)",
                              (unsigned)vta, control);
            } else {
                // Distinct from SEH: read paths returned cleanly, text just
                // wasn't there. Could mean the offset is wrong for this
                // class, the CExoString is genuinely empty, or the strref is
                // 0/0xFFFFFFFF (LookupTlk silently returns false). Logging
                // every miss helps confirm the speculative path runs and
                // that reads aren't faulting silently.
                acclog::Write("Speculative read miss: vtable=0x%x control=%p "
                              "tag=%s", (unsigned)vta, control, ov.tag);
            }
        }
    }

    // 9a. Per-kind hardcoded label fallback. Some panels have widgets whose
    //     text isn't extractable through any of the engine paths we know
    //     (inline CExoString, strref, text_object, gui_string, tooltip all
    //     verified empty for CSWGuiInGameMenu's icons in
    //     patch-20260502-192712.log — 8 labels + 8 buttons all empty).
    //     The text must be set via script-side or .gui-resource paths we
    //     haven't traced yet. For known panel structures with fixed layout
    //     (CSWGuiInGameMenu's struct definition pins the icon order), we
    //     can hardcode the names by index until the engine-side path is
    //     identified.
    //
    //     CSWGuiInGameMenu layout (per swkotor.exe.h:10145):
    //       controls[0..7]  = 8 CSWGuiLabelHilight (vtable=0x0073E8E8)
    //                          equipment, inventory, character, map,
    //                          abilities, journal, options, messages
    //       controls[8..15] = 8 CSWGuiButton       (vtable=0x0073E658)
    //                          same names, same order
    //
    //     The user-visible captions in German are different from the
    //     internal field names; the strings below are the rendered
    //     in-game captions for the German build (matches "M" / "I" / "C"
    //     hotkey conventions per the controls-and-input doc).
    // Resolve the owning panel for the perkind fallback. Caller-passed owner
    // wins (RebindChain, WalkChildren, BuildContentFingerprint, SetActiveControl
    // all know the panel). Otherwise scan panels[] — covers callers that don't
    // know the panel (AnnounceControl from chain-step, listbox-row helpers,
    // FindCloseButton from the input hook). g_currentPanel is the last-resort
    // fallback for early-attach windows where the manager isn't resolvable yet.
    void* ownerForPerkind = ownerPanel;
    if (!ownerForPerkind) ownerForPerkind = FindOwningPanel(control);
    if (!ownerForPerkind) ownerForPerkind = g_currentPanel;
    if (!source && ownerForPerkind && IsPanelKindInGameMenu(ownerForPerkind)) {
        // Localized names sourced from dialog.tlk strrefs where they exist;
        // literal fallback for the one strref we couldn't find. Strref values
        // verified by parsing the user's actual dialog.tlk (German build,
        // LangID=2 — `tlk_lookup.ps1` results pasted into git history). The
        // strrefs are stable across localizations: the engine looks up the
        // SAME entry index from the locale-specific TLK, so a French or
        // English install gets correctly translated names without any per-
        // language switch. Equipment has no standalone strref in any
        // observed TLK — the engine never asks for that text via TLK — so
        // we fall back to a literal that matches the German build.
        struct InGameMenuName {
            uint32_t    strref;   // 0xFFFFFFFF = no strref, use literal
            const char* literal;  // fallback if LookupTlk fails
        };
        static const InGameMenuName k_inGameMenuNames[8] = {
            { 0xFFFFFFFFu, "Ausr\xfcstung" },     // equipment
            { 48220u,      "Inventar"     },      // inventory
            { 48225u,      "Charakterblatt" },    // character_sheet
            { 48221u,      "Karte"        },      // map
            { 48224u,      "F\xe4higkeiten" },    // abilities
            { 48218u,      "Auftr\xe4ge" },       // journal (= "quests/orders")
            { 48222u,      "Optionen"     },      // options
            { 48223u,      "Nachrichten"  },      // messages
        };

        // Find the index of `control` within the owning panel's controls[].
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(ownerForPerkind) + kPanelControlsOffset);
        if (list && list->data && list->size > 0) {
            int n = list->size > 32 ? 32 : list->size;
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (list->data[i] == control) { idx = i; break; }
            }
            // Labels are at panel.controls[0..7]; buttons at [8..15].
            // Same name table, shifted index for buttons.
            int nameIdx = -1;
            if (idx >= 0 && idx <= 7)       nameIdx = idx;
            else if (idx >= 8 && idx <= 15) nameIdx = idx - 8;
            if (nameIdx >= 0) {
                const auto& spec = k_inGameMenuNames[nameIdx];
                bool gotTlk = false;
                if (spec.strref != 0xFFFFFFFFu) {
                    char tlkText[256];
                    if (LookupTlk(spec.strref, tlkText, sizeof(tlkText))) {
                        size_t tlen = strnlen(tlkText, sizeof(tlkText));
                        if (tlen > 0 && tlen + 1 <= bufSize) {
                            memcpy(outBuf, tlkText, tlen + 1);
                            source = "perkind-tlk";
                            gotTlk = true;
                            acclog::Write("Per-kind InGameMenu TLK: control=%p "
                                          "panelIdx=%d strref=%u -> \"%s\"",
                                          control, idx, spec.strref, outBuf);
                        }
                    }
                }
                if (!gotTlk) {
                    size_t nlen = strlen(spec.literal);
                    if (nlen + 1 <= bufSize) {
                        memcpy(outBuf, spec.literal, nlen + 1);
                        source = "perkind-literal";
                        acclog::Write("Per-kind InGameMenu literal: control=%p "
                                      "panelIdx=%d strref=%u -> \"%s\"",
                                      control, idx, spec.strref, outBuf);
                    }
                }
            }
        }
    }

    // 9. Sibling-label fallback for chain-navigable controls with no text.
    //    Image-only icon buttons (vtable=0x0073E658 in CSWGuiInGameMenu —
    //    Equipment / Inventory / Character / Map / Abilities / Journal /
    //    Options / Messages icons) genuinely have no inline text. Their
    //    visible name lives on a separately-allocated CSWGuiLabelHilight
    //    sibling at the same x-coord. FindSiblingLabel locates that sibling
    //    spatially; we then announce its text as if it were our own. Same
    //    pattern the slider extraction (step 6) uses for category labels.
    //
    //    Gated on IsChainNavigable(control) so we only fire for buttons
    //    the user can actually focus — a label on its own (already
    //    extractable elsewhere) wouldn't hit this path.
    if (!source && g_currentPanel && IsChainNavigable(control)) {
        char label[256];
        if (FindSiblingLabel(g_currentPanel, control,
                             label, sizeof(label))) {
            size_t llen = strnlen(label, sizeof(label));
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, label, llen + 1);
                source = "siblinglabel-fallback";
                acclog::Write("Sibling-label fallback hit: control=%p label=\"%s\"",
                              control, outBuf);
            }
        }
    }

    // CSWGuiEditbox — the engine doesn't expose an AsEditbox accessor in
    // GuiControlMethods, and we don't yet know its struct layout well
    // enough to read fields by speculative offsets. OnSetActiveControl
    // logs the vtable pointer for any control we can't extract; map via
    // SARIF + add per-class extraction here.

    // Cycle value-display prefix. Cycle widgets render as `[◀] value [▶]`
    // and the engine rewrites the middle button's CExoString to the new
    // value on each activate, losing the category name. We capture the
    // category at panel-walk time (in OnSetActiveControl, before any
    // activation has run); here we just look it up. Skipped for toggles
    // (whose own text already reads as "{label}, {state}") and for
    // non-cycle buttons (LookupCycleCategory returns null when control
    // isn't in the cycle map). Redundancy guard: if the captured
    // "category" is byte-identical to the current rendered value we
    // suppress the prefix — that's the failure mode where capture caught
    // the value rather than the category (timing-dependent; see
    // OnSetActiveControl), and "Normal, Normal" is worse than just "Normal".
    if (source && !IsToggle(control)) {
        const char* category = LookupCycleCategory(control);
        if (category && strcmp(category, outBuf) != 0) {
            char value[256];
            strncpy_s(value, outBuf, _TRUNCATE);
            snprintf(outBuf, bufSize, "%s, %s", category, value);
        }
    }

    // Append element-state suffix for toggles. Detected via the same downcast
    // we'd use for text extraction, so works regardless of which path
    // returned the label (most toggles are caught by AsButton at step 2).
    if (source && IsToggle(control)) {
        bool on = ReadToggleState(control);
        size_t len = strnlen(outBuf, bufSize);
        const char* suffix = on ? ", ein" : ", aus";
        size_t suffixLen = strlen(suffix);
        if (len + suffixLen + 1 <= bufSize) {
            memcpy(outBuf + len, suffix, suffixLen + 1);
        }
    }

    return source;
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

// ============================================================================
// In-game panel identity registry.
//
// CGuiInGame at offset +0x40 of CClientExoAppInternal holds named pointers to
// every persistent in-game GUI panel: tutorial_box, main_interface (HUD),
// dialog_cinematic, bark_bubble, in_game_pause, message_box, etc. By matching
// any panel pointer against these named slots we can classify it semantically
// (e.g. panel 12B04010 → TutorialBox) instead of guessing from layout.
//
// Resolution chain (per docs/llm-docs/re/swkotor.exe.h):
//   *(CAppManager**)0x7A39FC                    → CAppManager*
//   CAppManager.client            (+0x04)       → CClientExoApp*
//   CClientExoApp.internal        (+0x04)       → CClientExoAppInternal*
//   CClientExoAppInternal.gui_in_game (+0x40)   → CGuiInGame*
//
// Each step is a single indirect read; we re-resolve on every call so a
// module transition that destroys/recreates the in-game GUI doesn't leave
// us holding a stale pointer. Total cost is ~4 memory loads — cheap enough
// to call from any focus event or per-frame tick.
//
// The CGuiInGame field offsets below are derived directly from the struct
// definition in swkotor.exe.h:10219. If the engine struct layout ever
// changes (different patch level, different distribution), these offsets
// need to be re-derived. The PanelKind table is also the single point
// where we'd add a new in-game panel kind to be recognised — name +
// offset, and IdentifyPanel picks it up.
// ============================================================================

constexpr uintptr_t kAddrAppManagerPtr             = 0x007A39FC;
constexpr size_t    kAppManagerClientOff           = 0x04;
constexpr size_t    kClientExoAppInternalOff       = 0x04;
constexpr size_t    kClientExoAppGuiInGameOff      = 0x40;

enum class PanelKind {
    Unknown = 0,
    // Persistent always-on UI
    MainInterface,
    InGameMenu,
    // Modal screens accessible from the HUD
    InGameEquip,
    InGameInventory,
    InGameCharacter,
    InGameAbilities,
    InGameMessages,
    InGameJournal,
    InGameMap,
    InGameOptions,
    InGamePause,
    InGameGalaxyMap,
    // Dialogue surfaces
    DialogCinematic,
    DialogCinematicCopy,
    DialogComputer,
    DialogComputerCamera,
    DialogLetterbox1,
    DialogLetterbox2,
    DialogLetterbox3,
    BarkBubble,
    // Popups / overlays
    TutorialBox,
    MessageBox,
    SkillInfoBox,
    ControllerLossBox,
    StatusSummary,
    Examine,
    Container,
    CreateItemMenu,
    CreateItemSubMenu,
    Fade,
    LoadModuleDebugMenu,
    PowersFeatsSkillsDebugMenu,
    PartySelection,
    Store,
    SoloModeQuery,
    AreaTransition,
    // Dialogue auxiliary panels (the panels that route input during a
    // CSWGuiDialogCinematic conversation — separate from the rendering
    // panel that holds the message text).
    DialogMessagesAux,   // 0xf8: void* messages?
    DialogMessages,      // 0xfc: CGuiInGameDialogMessage*
};

struct PanelKindOffset {
    size_t      offset;
    PanelKind   kind;
    const char* name;
};

static const PanelKindOffset kPanelKindOffsets[] = {
    { 0x08, PanelKind::InGameMenu,                 "InGameMenu" },
    { 0x0c, PanelKind::InGameEquip,                "InGameEquip" },
    { 0x10, PanelKind::InGameInventory,            "InGameInventory" },
    { 0x14, PanelKind::InGameCharacter,            "InGameCharacter" },
    { 0x18, PanelKind::InGameAbilities,            "InGameAbilities" },
    { 0x1c, PanelKind::InGameMessages,             "InGameMessages" },
    { 0x20, PanelKind::InGameJournal,              "InGameJournal" },
    { 0x24, PanelKind::InGameMap,                  "InGameMap" },
    { 0x28, PanelKind::InGameOptions,              "InGameOptions" },
    { 0x3c, PanelKind::DialogCinematicCopy,        "DialogCinematicCopy" },
    { 0x40, PanelKind::DialogCinematic,            "DialogCinematic" },
    { 0x44, PanelKind::DialogComputer,             "DialogComputer" },
    { 0x48, PanelKind::DialogComputerCamera,       "DialogComputerCamera" },
    { 0x4c, PanelKind::BarkBubble,                 "BarkBubble" },
    { 0x50, PanelKind::Examine,                    "Examine" },
    { 0x54, PanelKind::Container,                  "Container" },
    { 0x58, PanelKind::CreateItemMenu,             "CreateItemMenu" },
    { 0x5c, PanelKind::CreateItemSubMenu,          "CreateItemSubMenu" },
    { 0x60, PanelKind::DialogLetterbox1,           "DialogLetterbox1" },
    { 0x64, PanelKind::DialogLetterbox2,           "DialogLetterbox2" },
    { 0x68, PanelKind::DialogLetterbox3,           "DialogLetterbox3" },
    { 0x6c, PanelKind::Fade,                       "Fade" },
    { 0x70, PanelKind::LoadModuleDebugMenu,        "LoadModuleDebugMenu" },
    { 0x74, PanelKind::PowersFeatsSkillsDebugMenu, "PowersFeatsSkillsDebugMenu" },
    { 0x78, PanelKind::PartySelection,             "PartySelection" },
    { 0x7c, PanelKind::InGamePause,                "InGamePause" },
    { 0x80, PanelKind::InGameGalaxyMap,            "InGameGalaxyMap" },
    { 0x84, PanelKind::Store,                      "Store" },
    { 0x8c, PanelKind::SoloModeQuery,              "SoloModeQuery" },
    { 0x90, PanelKind::MainInterface,              "MainInterface" },
    { 0x94, PanelKind::AreaTransition,             "AreaTransition" },
    { 0x98, PanelKind::MessageBox,                 "MessageBox" },
    { 0x9c, PanelKind::SkillInfoBox,               "SkillInfoBox" },
    { 0xa0, PanelKind::TutorialBox,                "TutorialBox" },
    { 0xa4, PanelKind::ControllerLossBox,          "ControllerLossBox" },
    { 0xa8, PanelKind::StatusSummary,              "StatusSummary" },
    // Dialogue input-routing surfaces (per CGuiInGame layout in
    // swkotor.exe.h:10282). The in-game session log shows that during
    // a CSWGuiDialogCinematic conversation, arrow-key input routes to
    // a separate foreground panel (0FDEE418 in patch-20260502-182804.log)
    // distinct from the rendering panel (DialogCinematicCopy at +0x3c).
    // Hypothesis: that routing target is one of these two — registering
    // both so the next log identifies which.
    { 0xf8, PanelKind::DialogMessagesAux,          "DialogMessagesAux" },
    { 0xfc, PanelKind::DialogMessages,             "DialogMessages" },
};
constexpr int kPanelKindOffsetCount =
    sizeof(kPanelKindOffsets) / sizeof(kPanelKindOffsets[0]);

static const char* PanelKindName(PanelKind k) {
    if (k == PanelKind::Unknown) return "Unknown";
    for (int i = 0; i < kPanelKindOffsetCount; ++i) {
        if (kPanelKindOffsets[i].kind == k) return kPanelKindOffsets[i].name;
    }
    return "?";
}

// Resolve CGuiInGame singleton via the CAppManager → CClientExoApp →
// CClientExoAppInternal indirection chain. Returns nullptr at any step's
// null — caller must handle (DLL_PROCESS_ATTACH timing, between modules,
// title screen with no game loaded, etc.).
static void* ResolveGuiInGame() {
    void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    if (!appMgr) return nullptr;
    void* exoApp = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientOff);
    if (!exoApp) return nullptr;
    void* internal = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(exoApp) + kClientExoAppInternalOff);
    if (!internal) return nullptr;
    return *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(internal) + kClientExoAppGuiInGameOff);
}

// Cache of (panel, kind) pairs we've already logged. Keeps the log tidy
// when persistent panels (HUD) get re-checked on every input event.
struct PanelKindCacheEntry {
    void*     panel;
    PanelKind kind;
};
constexpr int kPanelKindCacheSize = 32;
static PanelKindCacheEntry g_panelKindCache[kPanelKindCacheSize];
static int g_panelKindCacheCount = 0;

// Compare panel against every named slot in CGuiInGame. Returns kind on
// match, PanelKind::Unknown if no match (or if CGuiInGame isn't resolvable
// yet). Logs each distinct (panel, kind) pair on first sight.
static PanelKind IdentifyPanel(void* panel) {
    if (!panel) return PanelKind::Unknown;
    void* gui = ResolveGuiInGame();
    if (!gui) return PanelKind::Unknown;

    auto* base = reinterpret_cast<unsigned char*>(gui);
    for (int i = 0; i < kPanelKindOffsetCount; ++i) {
        void* slot = *reinterpret_cast<void**>(base + kPanelKindOffsets[i].offset);
        if (slot != panel) continue;

        PanelKind k = kPanelKindOffsets[i].kind;
        // First-sight log per (panel, kind) pair.
        for (int j = 0; j < g_panelKindCacheCount; ++j) {
            if (g_panelKindCache[j].panel == panel &&
                g_panelKindCache[j].kind  == k) {
                return k;  // already logged
            }
        }
        if (g_panelKindCacheCount >= kPanelKindCacheSize) {
            // FIFO evict oldest entry.
            memmove(g_panelKindCache, g_panelKindCache + 1,
                    sizeof(g_panelKindCache[0]) * (kPanelKindCacheSize - 1));
            g_panelKindCacheCount = kPanelKindCacheSize - 1;
        }
        g_panelKindCache[g_panelKindCacheCount++] = { panel, k };
        acclog::Write("PanelKind: panel=%p identified as %s",
                      panel, kPanelKindOffsets[i].name);
        return k;
    }
    return PanelKind::Unknown;
}

// Wrapper used by ExtractAnnounceableText (which is defined earlier in the
// file before the PanelKind enum is in scope). Forward-declared near the
// top of the file.
static bool IsPanelKindInGameMenu(void* panel) {
    return IdentifyPanel(panel) == PanelKind::InGameMenu;
}

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

// CSWGuiManager panel layout — used to resolve the *foreground* panel when
// multiple panels are walked simultaneously (e.g. character creation pre-
// instantiates the Default-vs-Custom modal AND both wizard panels in one
// frame; our chain rebind has been latching onto the LAST walked panel,
// which is not necessarily the one the user can see/interact with).
//
// Verified via Lane's SARIF (docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif,
// CSWGuiManager DT.Struct entry):
//   CSWGuiManager.panels      @ 0x88 — CExoArrayList<CSWGuiPanel*> (12 bytes)
//                                       data ptr (0x88), size (0x8c), cap (0x90)
//   CSWGuiManager.modal_stack @ 0x94 — GuiManagerModalStack (12 bytes)
//                                       unnamed (0x94..0x97), size (0x98), cap (0x9c)
//
// The first 4 bytes of GuiManagerModalStack are unnamed in the Ghidra DB
// but the structure is the same shape as CExoArrayList; PushModalPanel
// (0x0040bd90) and PopModalPanel (0x0040be00) are both RE'd, and
// GetPosInModalStack (0x0040ab70) implies the engine itself does index
// lookups against this stack — so it has to be a CSWGuiPanel** array.
// We treat the unnamed bytes as the data pointer here and emit a
// diagnostic log so the assumption can be validated against live state
// before we gate any behavior on it.
constexpr size_t kMgrPanelsDataOffset      = 0x88;
constexpr size_t kMgrPanelsSizeOffset      = 0x8c;
constexpr size_t kMgrModalStackDataOffset  = 0x94;
constexpr size_t kMgrModalStackSizeOffset  = 0x98;

// Find which panel in the manager's panels[] currently owns `control` —
// i.e. which one has it in its controls[]. Used by ExtractAnnounceableText
// when the caller didn't pass an explicit owner: g_currentPanel only updates
// on SetActiveControl, so chain rebinds and other indirect paths can land
// here while it still points at a previous focus owner. Returns nullptr if
// the manager isn't resolvable yet (early DLL_PROCESS_ATTACH) or the control
// isn't in any current panel.
//
// Cheap by design: ≤16 panels × ≤32 children, only fires from the perkind
// fallback path which itself is gated on every prior extraction step missing.
static void* FindOwningPanel(void* control) {
    if (!control) return nullptr;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(p) + kPanelControlsOffset);
        if (!list->data || list->size <= 0) continue;
        int n = list->size > 32 ? 32 : list->size;
        for (int j = 0; j < n; ++j) {
            if (list->data[j] == control) return p;
        }
    }
    return nullptr;
}

// Resolve the topmost (foreground) panel currently owned by the manager.
// Order:
//   1. If modal_stack is non-empty, return its top entry — this is the
//      modal currently capturing input.
//   2. Otherwise return the last entry in panels[] — last-pushed panel
//      is drawn on top in the engine's iteration order.
//   3. Returns nullptr if both are empty.
static void* GetForegroundPanel(void* mgr) {
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   modalSize = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
    if (modalSize > 0 && modalData) {
        void* top = modalData[modalSize - 1];
        if (top) return top;
    }
    int   panelSize = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (panelSize > 0 && panelData) {
        return panelData[panelSize - 1];
    }
    return nullptr;
}

// Diagnostic: dump every panel currently on the manager's panels array and
// modal_stack. Called at panel-walk time so the log captures the engine's
// view of "which panels exist right now" alongside our own per-panel walks.
// Also doubles as live verification that kMgrModalStackDataOffset truly
// points at a CSWGuiPanel** (the SARIF-derived layout assumption).
static void LogManagerStack(void* mgr, const char* tag) {
    if (!mgr) {
        acclog::Write("ManagerStack(%s): mgr=NULL", tag ? tag : "?");
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelSize  = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    int   modalSize  = *reinterpret_cast<int*>(base + kMgrModalStackSizeOffset);
    void** modalData = *reinterpret_cast<void***>(base + kMgrModalStackDataOffset);
    void* fg = GetForegroundPanel(mgr);
    acclog::Write("ManagerStack(%s) mgr=%p panels.size=%d modal.size=%d fg=%p",
                  tag ? tag : "?", mgr, panelSize, modalSize, fg);
    int pn = panelSize;
    if (pn < 0 || pn > 32) pn = (pn < 0) ? 0 : 32;
    for (int i = 0; i < pn && panelData; ++i) {
        acclog::Write("ManagerStack(%s)   panels[%d]=%p", tag ? tag : "?",
                      i, panelData[i]);
    }
    int mn = modalSize;
    if (mn < 0 || mn > 32) mn = (mn < 0) ? 0 : 32;
    for (int i = 0; i < mn && modalData; ++i) {
        acclog::Write("ManagerStack(%s)   modal[%d]=%p", tag ? tag : "?",
                      i, modalData[i]);
    }
}

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
// 0xb6 / 0xb7 are the engine's "nav-prev / nav-next" actions (Up/Down in
// menus); 0xb8 / 0xb9 are the horizontal-axis equivalents (Left/Right). All
// four are contiguous in ManagerTranslateCode's switch statement and translate
// to 0x3d-0x40 (KEYBOARD_K through KEYBOARD_N — the four post-translation
// codes the engine uses internally for slider / cycle adjustment).
//
// Consuming Up/Down prevents the engine's broken `.gui` focus-cycle from
// running. Left/Right are consumed selectively: on a focused slider we let
// them pass through (engine's slider HandleInputEvent at 0x0041adf0 expects
// the post-translation codes and adjusts cur_value natively), elsewhere we
// consume and dispatch to a same-row empty-text neighbour (cycle arrows).
constexpr int kInputNavUp    = 0xb6;
constexpr int kInputNavDown  = 0xb7;
constexpr int kInputNavLeft  = 0xb8;
constexpr int kInputNavRight = 0xb9;

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
// Default-linkage so ExtractAnnounceableText can forward-declare it via
// extern (function is defined earlier in the file but needs the panel
// pointer for slider sibling-label / cycle-category lookups).
void* g_currentPanel = nullptr;
static void* g_chainPanel   = nullptr;
static int   g_chainIndex   = 0;
static int   g_chainCount   = 0;

// Sub-screen drill state. The InGameMenu icon strip is kept in foreground by
// the engine: each icon's onClick (OnInvButtonPressed @0x624d10 etc.) jumps
// into CGuiInGame::SwitchToSWInGameGui @0x62cf10, which calls AddPanel for the
// new sub-screen and then SendPanelToBack on it — the strip stays on top
// (verified via SARIF xref trace). Without intervention our chain therefore
// keeps targeting the strip's 8 icons and the user can never reach the
// sub-screen's content (item rows, quest rows, settings buttons).
//
// Drill model: Enter on a strip icon arms this flag. The chain-target router
// in OnHandleInputEvent then prefers FindActiveSubScreenPanel() over the
// engine's foreground when fg is the strip — so arrows step through the
// sub-screen instead. Esc clears the flag (returns to strip nav). The flag
// also self-clears when the sub-screen leaves panels[].
//
// Override is gated on fg-is-the-strip: while a tutorial modal or an
// Options sub-tab is on top, fg is something else and we route to that
// directly (no double-override). Once the modal/sub-tab closes and fg
// returns to the strip, the override re-engages.
static bool g_drilledIntoSubScreen = false;

// Forward decl: find an InGame{X} sub-screen panel currently in panels[].
// Defined near the sub-screen spec table to share the kind set with
// AnnounceNewSubScreens. Returns the lowest-index match, or nullptr if no
// sub-screen is currently pushed.
static void* FindActiveSubScreenPanel();

// Forward decl matching the InGameSubScreenSpec table defined alongside the
// content-monitor whitelist. Used by the Esc-drill handler to test
// "is this panel one of the in-game sub-screens we drill into?" without
// depending on the spec struct's definition order.
struct InGameSubScreenSpec;
static const InGameSubScreenSpec* FindInGameSubScreenSpec(PanelKind k);

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

// Deferred slider value adjustment. Slider's HandleInputEvent at 0x0041adf0
// recognises logical codes 500 (increment) and 501 (decrement) — both run
// the full pipeline: SetCurValue + bounds clamp + the slider's gui_object
// callback (which is what actually changes the audio system's volume for
// Music/Voice/SFX/Movie sliders) + PlayGuiSound feedback. We dispatch via
// vtable[15] from OnUpdate rather than synchronously from OnHandleInputEvent
// to stay clear of mid-input-dispatch re-entrancy (same reason
// MoveMouseToPosition is deferred). Per-frame focus monitor catches the
// resulting cur_value change on the next tick and re-announces.
static bool  g_pendingSliderInput       = false;
static void* g_pendingSliderTarget      = nullptr;
static int   g_pendingSliderCode        = 0;

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// Per-frame focus state monitor. Snapshots the announceable text of the
// focused chain entry on each focus change; on every OnUpdate tick re-extracts
// and re-announces if the text has changed since the snapshot. This is the
// generic mechanism that catches every state mutation visible through
// ExtractAnnounceableText: toggle on/off flips, cycle-button value changes
// (engine rewrites the value-display button's CExoString in place when the
// user activates a flanking arrow), slider cur_value adjustments, etc. New
// widget types get the behaviour for free as soon as their text/state
// extraction lands in ExtractAnnounceableText.
static void* g_focusMonitorControl = nullptr;
static char  g_focusMonitorText[256] = {0};

// Cycle-button category cache. KOTOR cycle widgets (Difficulty etc.) are a
// CSWGuiButton flanked by two empty-text arrow buttons. The middle button's
// CExoString starts as the localized category name (e.g. "Schwierigkeitsgrad"
// in German) on first panel render — but the engine REPLACES it with the
// current value text ("Normal", "Leicht") the moment any cycle activation
// runs, including ours via FireActivate(arrow). To preserve the category for
// subsequent value-change announcements we capture it during chain rebind,
// before any activation has rewritten the field. Map is invalidated on
// every RebindChain.
struct CycleCategoryEntry {
    void* control;
    char  category[128];
};
constexpr int kMaxCycleCategoryEntries = 16;
static CycleCategoryEntry g_cycleCategories[kMaxCycleCategoryEntries];
static int g_cycleCategoryCount = 0;

static const char* LookupCycleCategory(void* control) {
    for (int i = 0; i < g_cycleCategoryCount; ++i) {
        if (g_cycleCategories[i].control == control) {
            return g_cycleCategories[i].category;
        }
    }
    return nullptr;
}

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
// there. kVtableListBox itself is declared earlier alongside kVtableSlider
// because the listbox-content extraction step in ExtractAnnounceableText
// needs it; future builds will need updating if Lane's SARIF reports a
// different value.

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
// CharButton / ActivatedButton / ButtonToggle) OR a CSWGuiSlider.
// MoveMouseToPosition's hover→active promotion path is safe for buttons but
// crashes when the active control is a label (verified: navigating onto the
// main-menu "Neue Inhalte verfügbar…" label froze the game). Sliders are
// included because Sound's Music/Voice/SFX/Movie controls are real sliders
// and we want chain navigation to land on them so we can announce their
// numeric value.
//
// Long-term: replace with a proper CSWGuiControl::GetIsSelectable call
// (vtable lookup at 0x4189d0) to also include editbox / listbox / etc.
static bool IsChainNavigable(void* control) {
    if (!control) return false;
    if (CallDowncast(control, kVtableAsButton)        != nullptr) return true;
    if (CallDowncast(control, kVtableAsButtonToggle)  != nullptr) return true;
    if (IsSlider(control))                                        return true;
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
        if (ExtractAnnounceableText(child, text, sizeof(text), panel)) {
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
        if (!ExtractAnnounceableText(c, text, sizeof(text), panel)) continue;
        if (strncmp(text, "Schliess", 8) == 0 ||
            strncmp(text, "Close",    5) == 0 ||
            strncmp(text, "OK",       2) == 0) {
            return c;
        }
    }
    return nullptr;
}

// Find the closest navigable empty-text neighbour of `focused` in
// `panel.controls`, on the visual left or right at the same y-row. Used to
// dispatch Left/Right arrow presses to cycle-button flanker arrows: the
// Difficulty cycle (and similar) renders as `[◀] Normal [▶]` — three plain
// CSWGuiButtons, where the middle one carries the value text and the flanks
// carry an image overlay only. We want Left/Right on the value-display button
// to fire the corresponding flanker so the engine cycles the value.
//
// Heuristic:
//   - Same-row: |cy_neighbour - cy_focused| <= 5 px (allows for off-by-one
//     baseline alignment; KOTOR cycle layouts visually match much tighter).
//   - Empty-text: ExtractAnnounceableText returns nullptr. Real labels and
//     toggles are excluded so the "neighbour" is unambiguously a flanker.
//   - Closest by signed dx: smaller |dx| wins among candidates strictly to
//     the right (toRight=true) or left (toRight=false) of the focused control.
//
// Returns nullptr if no flanker found — caller falls back to "consume the
// keypress with no action" so we don't trigger surprising native behaviour.
static void* FindAdjacentArrow(void* panel, void* focused, bool toRight) {
    if (!panel || !focused) return nullptr;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;

    int focusCx, focusCy;
    if (!GetControlCenter(focused, focusCx, focusCy)) return nullptr;

    void* best = nullptr;
    int bestDx = 0x7fffffff;

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == focused) continue;
        if (!IsChainNavigable(c)) continue;

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;
        if (cy - focusCy > 5 || focusCy - cy > 5) continue;

        int dx = toRight ? (cx - focusCx) : (focusCx - cx);
        if (dx <= 0) continue;

        // Only consider empty-text neighbours — real labels / toggles are not
        // cycle flankers.
        char tmp[64];
        if (ExtractAnnounceableText(c, tmp, sizeof(tmp), panel)) continue;

        if (dx < bestDx) {
            bestDx = dx;
            best   = c;
        }
    }
    return best;
}

// Find the closest label-like sibling of `control` on the panel. Two
// candidate positions:
//
//   1. Same y-row, to the visual LEFT (horizontal layouts, e.g. labelled
//      buttons on a config row — `[Schliess]   Standard`).
//   2. Directly ABOVE the control (vertical layouts, e.g. KOTOR's Sound
//      panel where each slider has a label rendered on the line above it
//      rather than to its left).
//
// Picks the candidate with the smallest scoring distance: Manhattan
// distance for "above" candidates so a label slightly off-axis but close
// in y wins over a same-row label that's far to the left. Strict
// thresholds keep us from matching the panel title (which is far above
// any individual widget) or unrelated controls in adjacent rows.
//
// Returns the source tag ("siblinglabel") and writes the label's text
// into outBuf, or nullptr if no suitable label was found. Pure read;
// doesn't recurse through ExtractAnnounceableText so it's safe to call
// from inside extraction code.
static const char* FindSiblingLabel(void* panel, void* control,
                                    char* outBuf, size_t bufSize) {
    if (!panel || !control || bufSize < 2) return nullptr;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;

    int targetCx, targetCy;
    if (!GetControlCenter(control, targetCx, targetCy)) return nullptr;

    void* best = nullptr;
    int bestScore = 0x7fffffff;

    // Tolerances. Same-row dy<=5; vertical offset (above OR below) up to
    // 50 px (typical KOTOR row height is ~30-55 px) with dx tolerance 80
    // px (label can be slightly offset from the widget center, e.g.
    // left-aligned label vs centered slider). Search expanded vs the
    // original same-row-left + above-only set so InGameMenu-style
    // captioned icons (label below the button) also match.
    constexpr int kSameRowDyTol  = 5;
    constexpr int kVertDyMax     = 50;
    constexpr int kVertDxMax     = 80;

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == control) continue;

        if (CallDowncast(c, kVtableAsLabel) == nullptr &&
            CallDowncast(c, kVtableAsLabelHilight) == nullptr) {
            continue;
        }

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;

        int dy    = cy - targetCy;          // negative = label is above
        int adx   = cx - targetCx;          // negative = label is to the left
        int absDx = adx < 0 ? -adx : adx;
        int absDy = dy  < 0 ? -dy  : dy;

        int score = 0x7fffffff;
        // Same row — favour LEFT siblings (existing behaviour for slider
        // labels) but also accept right-of-target as a fallback so we
        // don't reject all icon-with-trailing-caption layouts.
        if (absDy <= kSameRowDyTol) {
            // Left has score = absDx (lower is better);
            // right gets a small penalty so left wins on ties.
            score = (cx < targetCx) ? absDx : (absDx + 8);
        }
        // Vertically displaced (above OR below) with similar x. Below-target
        // captioned-icon layouts (label below the icon button) need the
        // dy>0 case too. Manhattan distance scoring keeps the closest
        // candidate winning when multiple labels could pair.
        else if (absDy <= kVertDyMax && absDx <= kVertDxMax) {
            score = absDx + absDy;
        }
        else {
            continue;
        }

        if (score < bestScore) {
            bestScore = score;
            best      = c;
        }
    }

    if (!best) return nullptr;
    if (ExtractTextOrStrRefIndirect(best,
                                    kLabelTextOffset,
                                    kLabelStrRefOffset,
                                    kLabelTextObjectOffset,
                                    outBuf, bufSize)) {
        return "siblinglabel";
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

    // Squash cycle-arrow flankers from the chain. An empty-text navigable
    // entry that shares a y-row with a text-bearing entry is a cycle arrow
    // (left/right of a value-display button: `[◀] Normal [▶]`). The user
    // reaches them via Left/Right cycle dispatch on the value-display
    // entry — having them in the chain just produces "control N"
    // placeholders Up/Down would land on. Lone empty-text entries are
    // kept (we can't say what they are, but the user might want to reach
    // them for Enter activation). Same-row threshold matches
    // FindAdjacentArrow's tolerance.
    {
        int writeIdx = 0;
        for (int i = 0; i < g_chainCount; ++i) {
            char tmp[64];
            bool hasText = ExtractAnnounceableText(g_chain[i].control,
                                                   tmp, sizeof(tmp),
                                                   panel) != nullptr;
            if (hasText) {
                g_chain[writeIdx++] = g_chain[i];
                continue;
            }
            bool sameRowWithText = false;
            for (int j = 0; j < g_chainCount; ++j) {
                if (j == i) continue;
                int dy = g_chain[j].cy - g_chain[i].cy;
                if (dy < 0) dy = -dy;
                if (dy > 5) continue;
                char tmp2[64];
                if (ExtractAnnounceableText(g_chain[j].control,
                                            tmp2, sizeof(tmp2),
                                            panel) != nullptr) {
                    sameRowWithText = true;
                    break;
                }
            }
            if (!sameRowWithText) {
                g_chain[writeIdx++] = g_chain[i];
            }
        }
        g_chainCount = writeIdx;
    }

    // Cycle category capture lives in OnSetActiveControl's panel-walk path,
    // not here — RebindChain runs only on the first arrow press in a panel
    // (typically several seconds after panel open), and by then the engine
    // has already replaced cycle buttons' .gui-default category text with
    // the persisted value (e.g. "Difficulty" -> "Normal"). Capture has to
    // happen at panel-walk time to catch the .gui state before the
    // .ini-driven update.

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
                                                  text, sizeof(text),
                                                  panel);
        // Read CSWGuiControl.is_active (+0x4c) and bit_flags (+0x44) per
        // SARIF struct layout. Hypothesis for chargen wizard buttons that
        // silently no-op on Enter (Talente/Name/Spielen): step-gated, with
        // is_active==0 until prereqs met. We already know `is_active` is
        // what blocks vtable[15] FireActivate on tabs (set only by
        // HandleLMouseDown's CaptureMouse path) — same flag, different
        // gate. If Name shows is_active=0 alongside Fähigkeiten=1, we
        // have our answer.
        unsigned int isActive =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x4c);
        unsigned int bitFlags =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x44);
        acclog::Write("Chain   [%d] %p (%d,%d) %s text=\"%s\" is_active=%u bit_flags=0x%x",
                      i, g_chain[i].control, g_chain[i].cx, g_chain[i].cy,
                      src ? src : "?", src ? text : "", isActive, bitFlags);
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
        // Pass `parent` so the perkind fallback resolves correctly when
        // walking InGameMenu's children — the icon labels/buttons have empty
        // CExoString/strref/text_object/gui_string and only resolve via the
        // panel-keyed perkind table.
        const char* source = ExtractAnnounceableText(child, text, sizeof(text),
                                                     parent);
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
    //
    // Also captures cycle-button category text. Cycle widgets (Difficulty
    // etc.) carry their localized category in their CExoString at panel
    // construction time (e.g. "Schwierigkeitsgrad" / "Difficulty"); the
    // engine replaces it with the persisted value (e.g. "Normal") shortly
    // after, and our FireActivate calls overwrite it again on each cycle.
    // SetActiveControl's first fire on a new panel happens before any of
    // those updates, so this is the earliest reachable capture point.
    static void* s_lastPanel = nullptr;
    if (panel && panel != s_lastPanel) {
        s_lastPanel = panel;
        // Dump manager-level panels + modal_stack alongside the per-panel
        // walk. Lets us correlate "the engine just walked panel X" with
        // "panels[] and modal_stack[] currently look like this", which is
        // what we need to validate GetForegroundPanel against the actual
        // visible foreground (especially in flows like character creation
        // where multiple panels are walked in the same frame).
        LogManagerStack(*reinterpret_cast<void**>(kAddrGuiManagerPtr),
                        "panel-walk");
        // Identify the panel via the in-game registry. First-sight log
        // happens inside IdentifyPanel. The kind here is purely diagnostic
        // — actual per-kind handling lives in MonitorPanelContents on each
        // OnUpdate tick.
        PanelKind kind = IdentifyPanel(panel);
        acclog::Write("Panel walk panel=%p kind=%s",
                      panel, PanelKindName(kind));
        WalkChildren("Panel", panel, kPanelControlsOffset);

        g_cycleCategoryCount = 0;
        auto* plist = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (plist && plist->data && plist->size > 0) {
            int pn = plist->size > 256 ? 256 : plist->size;
            for (int i = 0;
                 i < pn && g_cycleCategoryCount < kMaxCycleCategoryEntries;
                 ++i) {
                void* c = plist->data[i];
                if (!c) continue;
                if (CallDowncast(c, kVtableAsButton) == nullptr) continue;
                if (IsToggle(c)) continue;

                void* leftN  = FindAdjacentArrow(panel, c, /*toRight=*/false);
                void* rightN = FindAdjacentArrow(panel, c, /*toRight=*/true);
                if (!leftN && !rightN) continue;

                char text[128];
                bool gotText = false;
                uint32_t strref = ReadU32(c, kButtonStrRefOffset);
                if (LookupTlk(strref, text, sizeof(text))) {
                    gotText = true;
                } else if (ReadCExoString(c, kButtonTextOffset,
                                          text, sizeof(text))) {
                    gotText = true;
                }
                if (gotText) {
                    g_cycleCategories[g_cycleCategoryCount].control = c;
                    strncpy_s(g_cycleCategories[g_cycleCategoryCount].category,
                              text, _TRUNCATE);
                    ++g_cycleCategoryCount;
                    acclog::Write("Cycle category captured: control=%p text=\"%s\" strref=%u",
                                  c, text, strref);
                }
            }
        }
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
    const char* source = ExtractAnnounceableText(newControl, text, sizeof(text),
                                                 panel);

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
        // Suppress placeholder for single-row listboxes (description blobs
        // adjacent to a chain panel — the engine fires SetActiveControl on
        // them as the user navigates the chain, alternating between
        // src=label with text and src=none when the description is
        // momentarily empty). The user isn't navigating these listboxes;
        // "row 0" repeated 5+ times per chain step is just noise.
        //
        // Real multi-row listboxes (save-game list, chargen pickers) keep
        // the fallback so an extraction failure on one row still announces.
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : 0;
        if (ctrlsSize > 1) {
            char placeholder[64];
            snprintf(placeholder, sizeof(placeholder), "row %d", id);
            tolk::Speak(placeholder, /*interrupt=*/false);
        }
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

    // Resolve the foreground panel via the manager's modal_stack / panels[].
    // g_currentPanel tracks "last panel that received SetActiveControl" — fine
    // for per-instance state (sibling-label lookup, cycle-category capture)
    // but UNRELIABLE for routing, because flows that pre-instantiate multiple
    // panels in one frame (character creation: modal + 2 wizards) leave
    // g_currentPanel pointing at the last-walked panel, which is NOT the
    // visible foreground. Verified from patch-20260502-164320.log: in that
    // flow modal_stack.size goes 0→4 with the user-visible Standardcharakter
    // modal correctly at modal[top], while g_currentPanel had latched onto
    // a backgrounded wizard. See ManagerStack diagnostic and report.
    //
    // Fallback to g_currentPanel only when the manager pointer or the
    // foreground resolves to null (early-init frames before any panel
    // exists, or screens we don't yet understand).
    void* activePanel = nullptr;
    {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        void* fg = GetForegroundPanel(mgr);
        activePanel = fg ? fg : g_currentPanel;
        // First-fire-per-pair divergence log: when fg != g_currentPanel we
        // want to see it in the log, but only once per (fg, g_currentPanel)
        // tuple to avoid spamming during steady-state (every keypress in a
        // multi-panel flow would otherwise emit a line).
        if (fg && fg != g_currentPanel) {
            static void* s_lastFg = nullptr;
            static void* s_lastCp = nullptr;
            if (fg != s_lastFg || g_currentPanel != s_lastCp) {
                acclog::Write("Routing: fg=%p current=%p (using fg)",
                              fg, g_currentPanel);
                s_lastFg = fg;
                s_lastCp = g_currentPanel;
            }
        }

        // Drill override: when the user has Entered into a sub-screen, retarget
        // the chain from the strip (kept in fg by SendPanelToBack) to the
        // sub-screen panel. Only fires when fg actually IS the strip — leaves
        // tutorial modals and Options sub-tabs (which become fg in their own
        // right) routing through fg directly.
        if (g_drilledIntoSubScreen) {
            if (IdentifyPanel(activePanel) == PanelKind::InGameMenu) {
                void* sub = FindActiveSubScreenPanel();
                if (sub) {
                    activePanel = sub;
                } else {
                    g_drilledIntoSubScreen = false;
                    acclog::Write("Drill: sub-screen gone from panels[]; "
                                  "returning to strip");
                }
            }
        }
    }

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
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
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
                          activePanel, g_chainIndex, e.control, cursorY);
            consumed = true;
        } else {
            g_pendingActivate       = true;
            g_pendingActivateTarget = e.control;

            // Arm the drill flag when Enter activates an icon on the InGameMenu
            // strip. The engine's activation path (FireActivate → button
            // onClick → SwitchToSWInGameGui) pushes the sub-screen to back; on
            // the next arrow press, the drill router will retarget the chain
            // to it. Set EAGERLY: if the engine no-ops the activation (e.g.
            // re-press of the same icon, GUI_id unchanged), the override
            // self-clears via FindActiveSubScreenPanel-returns-null on the
            // next route.
            if (IdentifyPanel(g_chainPanel) == PanelKind::InGameMenu) {
                g_drilledIntoSubScreen = true;
                acclog::Write("Drill: armed (Enter on InGameMenu icon target=%p)",
                              e.control);
            }

            acclog::Write("Enter activate panel=%p index=%d target=%p",
                          activePanel, g_chainIndex, e.control);
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
        activePanel != nullptr)
    {
        if (activePanel != g_chainPanel) {
            RebindChain(activePanel);
        }
        if (g_chainCount == 0) {
            // Foreground panel has no navigable controls. Log so we can see
            // which panels are routing-only (e.g. the recurring 074FE618
            // overlay and the dialog routing target 0FDEE418 observed in
            // the in-game session) and decide whether to add a fallback
            // strategy (walk down the modal stack to the next chain-eligible
            // panel, or surface the panel's content via the title/listbox
            // path). For now: log only, leave the input unconsumed so the
            // engine sees it.
            PanelKind emptyKind = IdentifyPanel(activePanel);
            acclog::Write("Chain empty: panel=%p kind=%s has no navigable "
                          "controls; input not consumed",
                          activePanel, PanelKindName(emptyKind));

            // Walk the panel ONCE so we can see what's actually in it.
            // OnSetActiveControl's panel-walk gate (s_lastPanel) doesn't
            // fire on these panels because the engine never sets focus on
            // them. Without a walk we never learn their structure — log-only
            // diagnostics give us nothing actionable.
            static void* s_walkedEmptyPanels[16];
            static int   s_walkedEmptyCount = 0;
            bool walked = false;
            for (int i = 0; i < s_walkedEmptyCount; ++i) {
                if (s_walkedEmptyPanels[i] == activePanel) { walked = true; break; }
            }
            if (!walked && s_walkedEmptyCount < 16) {
                s_walkedEmptyPanels[s_walkedEmptyCount++] = activePanel;
                acclog::Write("EmptyChainPanel walk panel=%p kind=%s",
                              activePanel, PanelKindName(emptyKind));
                WalkChildren("EmptyChainPanel", activePanel,
                             kPanelControlsOffset);
            }
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

    // Left/Right dispatch. Two cases:
    //
    //   1. Focused control is a slider — queue a slider HandleInputEvent
    //      with logical inc/dec code (500 / 501). Engine's slider runs the
    //      full pipeline: SetCurValue + bounds clamp + gui_object callback
    //      (audio volume change for Music/Voice/SFX/Movie) + PlayGuiSound.
    //      Letting the keypress pass through to the engine doesn't work
    //      because panel.activeControl isn't set to the slider (chain
    //      navigation only updates mouseOverControl); the engine's natural
    //      dispatch would route Left/Right to whichever previous control was
    //      activeControl, not the slider the user navigated to.
    //
    //   2. Focused control is anything else — find an empty-text navigable
    //      neighbour at the same y-row in panel.controls and fire-activate
    //      it (cycle-arrow flanker). Engine rewrites the value-display
    //      button's CExoString in place. Per-frame monitor catches both
    //      cases on the next tick and re-announces.
    //
    // Both cases consume the keypress so we don't surface unspecified
    // native behaviour from Left/Right on widgets where it has no
    // user-meaningful effect.
    if (param_2 != 0 &&
        (param_1 == kInputNavLeft || param_1 == kInputNavRight) &&
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
        g_chainCount > 0 &&
        g_chainIndex >= 0 &&
        g_chainIndex < g_chainCount)
    {
        void* focused = g_chain[g_chainIndex].control;
        bool toRight = (param_1 == kInputNavRight);

        if (IsSlider(focused)) {
            if (g_pendingClick || g_pendingActivate || g_pendingCursorMove ||
                g_pendingSliderInput) {
                acclog::Write("Slider %s: op already pending; ignoring",
                              toRight ? "right" : "left");
            } else {
                g_pendingSliderInput  = true;
                g_pendingSliderTarget = focused;
                g_pendingSliderCode   = toRight ? 500 : 501;
                acclog::Write("Slider %s panel=%p focus=%p code=%d",
                              toRight ? "right" : "left",
                              activePanel, focused, g_pendingSliderCode);
            }
        } else {
            void* neighbor = FindAdjacentArrow(activePanel, focused, toRight);
            if (neighbor) {
                if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
                    acclog::Write("Cycle %s: op already pending; ignoring",
                                  toRight ? "right" : "left");
                } else {
                    g_pendingActivate       = true;
                    g_pendingActivateTarget = neighbor;
                    acclog::Write("Cycle %s panel=%p focus=%p neighbor=%p",
                                  toRight ? "right" : "left",
                                  activePanel, focused, neighbor);
                }
            } else {
                acclog::Write("Cycle %s: no adjacent arrow for focus=%p",
                              toRight ? "right" : "left", focused);
            }
        }
        consumed = true;
    }

    // Esc in drill mode: return chain to the strip without closing the
    // sub-screen. User flow:
    //   1. On strip, Enter on Inventory → engine pushes InGameInventory,
    //      strip stays fg, drill arms, chain retargets to inventory listbox.
    //   2. User navigates inventory items.
    //   3. Esc → drill clears, next arrow press rebinds chain to strip.
    //   4. From strip, Right-arrow + Enter to switch to a different sub-screen.
    //
    // We deliberately don't fire the sub-screen's exit_button here — leaving
    // the sub-screen alive in panels[] means re-pressing Enter on the same
    // icon is a cheap re-drill (no tutorial replay, no engine-side teardown).
    // Closing the screen is what the sub-screen's own exit_button is for; the
    // user can navigate to it explicitly while drilled.
    //
    // Routes BEFORE the tabbed-panel Esc handler below: drilled mode is the
    // outer state, sub-tab close is the inner. If both could match (drilled
    // into Options with a sub-tab open), close the sub-tab first via the
    // existing handler — drill stays armed because activePanel is still
    // a sub-tab modal at that point, not the strip.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        g_drilledIntoSubScreen &&
        activePanel != nullptr &&
        IdentifyPanel(activePanel) != PanelKind::InGameMenu)
    {
        // Only fire when activePanel is the sub-screen itself, not a sub-tab
        // or modal sitting on top of it. Tabbed-Esc handler below will close
        // sub-tabs first; once activePanel resolves back to the sub-screen,
        // the next Esc lands here and clears the drill.
        PanelKind apk = IdentifyPanel(activePanel);
        if (FindInGameSubScreenSpec(apk)) {
            g_drilledIntoSubScreen = false;
            acclog::Write("Drill: Esc -> back to strip (sub-screen panel=%p "
                          "kind=%s left in panels[])",
                          activePanel, PanelKindName(apk));
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
    // Gated on activePanel != g_tabbedPanel: only fires inside a sub-dialog
    // of a tabbed parent. On the parent Options panel itself, Esc passes
    // through to the engine (which opens the "Möchtest du wirklich aufhören?"
    // quit confirmation — desired existing behavior).
    //
    // We use activePanel (resolved from the manager's modal_stack/panels[]
    // at the top of this function) rather than g_currentPanel. The latter is
    // set by SetActiveControl and never cleared on panel pop, so once a
    // sub-dialog closes, g_currentPanel keeps pointing at the dead panel
    // until a new one takes focus — and Esc would keep firing FireActivate
    // against the popped panel. activePanel always reflects the current
    // foreground per the manager.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        activePanel != nullptr &&
        g_tabbedPanel != nullptr &&
        activePanel != g_tabbedPanel)
    {
        if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
            acclog::Write("Esc: op already pending; ignoring");
            consumed = true;
        } else {
            void* closeBtn = FindCloseButton(activePanel);
            if (closeBtn) {
                g_pendingActivate       = true;
                g_pendingActivateTarget = closeBtn;
                acclog::Write("Esc close panel=%p kind=%s Schliess=%p",
                              activePanel, PanelKindName(IdentifyPanel(activePanel)),
                              closeBtn);
                consumed = true;
            } else {
                acclog::Write("Esc on sub-dialog panel=%p kind=%s but no "
                              "close button found; passing through",
                              activePanel, PanelKindName(IdentifyPanel(activePanel)));
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

// Per-frame focus state monitor. Re-extracts the focused chain entry's
// announceable text and re-announces if it has changed since the last
// snapshot. Generic mechanism for state-change announcements — toggle
// on/off, cycle button value, slider position, and any future widget whose
// state shows up through ExtractAnnounceableText all flow through here, no
// per-widget code needed.
//
// On focus moving to a different control we only update the snapshot. The
// initial announcement is handled by the chain step path (OnHandleInputEvent)
// or OnSetActiveControl; this monitor's job is strictly "same control, text
// changed since last tick". That's precisely the "state mutated under our
// focus" case — Enter on a toggle flips +0x1c8 bit 0 synchronously inside
// FireActivate, the engine's slider HandleInputEvent rewrites cur_value
// synchronously when Left/Right reach the slider, and a cycle activation
// rewrites the value-display button's CExoString in place. All three
// produce a different ExtractAnnounceableText output on the very next tick.
//
// Empty-text controls (cycle arrows, controls we don't yet know how to
// extract) bypass the snapshot entirely so we don't accidentally announce
// transient placeholders. The chain step path already announced "control N"
// for them when focus arrived.
static void MonitorFocusedControl() {
    if (g_chainCount <= 0 ||
        g_chainIndex < 0 ||
        g_chainIndex >= g_chainCount) {
        return;
    }
    if (g_chainPanel != g_currentPanel) {
        // Chain stale (panel transition mid-flight); skip until rebind.
        return;
    }
    void* focused = g_chain[g_chainIndex].control;
    if (!focused) return;

    char text[256];
    const char* source = ExtractAnnounceableText(focused, text, sizeof(text),
                                                 g_chainPanel);
    if (!source) return;

    if (focused == g_focusMonitorControl) {
        if (strncmp(g_focusMonitorText, text,
                    sizeof(g_focusMonitorText)) != 0) {
            tolk::Speak(text, /*interrupt=*/false);
            strncpy_s(g_focusMonitorText, text, _TRUNCATE);
            acclog::Write("Monitor: focused=%p text changed -> \"%s\"",
                          focused, text);
        }
    } else {
        g_focusMonitorControl = focused;
        strncpy_s(g_focusMonitorText, text, _TRUNCATE);
    }
}

// =============================================================================
// Per-panel content-change monitor.
//
// MonitorFocusedControl above watches the focused chain entry's text for
// state-mutation announcements (toggle flip, cycle value, slider position).
// That's the right thing for INTERACTIVE focus targets, but blind to two
// classes of events:
//
//   1. Panels that have no focused control (newControl=NULL throughout).
//      The tutorial popup is the canonical case — panel 12B04010 has a
//      label child carrying the hint text but no focusable child; the
//      engine never fires SetActiveControl with a non-null target. The
//      label text is also late-bound: the panel appears with " " in the
//      label, then the engine writes the actual hint string seconds later.
//      Our pointer-keyed gates in OnSetActiveControl (s_lastPanel,
//      g_lastTitledPanel) only fire once per panel address, so we miss
//      the late text binding entirely.
//
//   2. Always-on panels at the bottom of panels[] (the HUD, persistent
//      overlays). These never receive SetActiveControl, so they're never
//      walked, never titled, never monitored.
//
// MonitorPanelContents fills both gaps generically: every OnUpdate tick,
// walk the manager's panels[], identify each by IdentifyPanel, and for
// the ones flagged as content-monitored compute a fingerprint of their
// label-bearing children (concatenation of every label and listbox text).
// Diff against last snapshot, announce changes.
//
// Per-kind whitelist (IsContentMonitored) keeps the cost down — we don't
// fingerprint every panel every frame, only the ones whose content actually
// changes meaningfully (tutorials, dialogue text, transition text, modal
// messages). MainInterface (HUD vitals, queue, combat-mode) deserves a
// dedicated polling layer with named-offset reads instead of full-panel
// fingerprinting; deferred to a follow-up.
// =============================================================================

struct ContentSnapshot {
    void* panel;
    char  text[512];
};
constexpr int kMaxContentSnapshots = 8;
static ContentSnapshot g_contentSnapshots[kMaxContentSnapshots];
static int g_contentSnapshotCount = 0;

static bool IsContentMonitored(PanelKind k) {
    switch (k) {
    case PanelKind::TutorialBox:
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
    case PanelKind::BarkBubble:
    case PanelKind::MessageBox:
    case PanelKind::AreaTransition:
    // In-game sub-screens reached via the icon strip. The icon strip
    // (CSWGuiInGameMenu) stays foreground after activation, so the sub-screen
    // never becomes the chain target — without content monitoring the user
    // hears the strip but nothing about what's INSIDE the screen they just
    // opened. Buttons are filtered by BuildContentFingerprint so the strip's
    // own buttons don't pollute the fingerprint.
    case PanelKind::InGameInventory:
    case PanelKind::InGameMap:
    case PanelKind::InGameJournal:
    case PanelKind::InGameCharacter:
    case PanelKind::InGameAbilities:
    case PanelKind::InGameMessages:
    case PanelKind::InGameEquip:
        return true;
    default:
        return false;
    }
}

// Localized name of an in-game sub-screen, indexed by PanelKind. Reuses
// the same dialog.tlk strrefs as the perkind icon-label table in
// ExtractAnnounceableText (verified by parsing the user's dialog.tlk —
// memory/reference_dialog_tlk_menu_strrefs.md). Returns spec on hit, nullptr
// if the kind isn't a tracked sub-screen.
struct InGameSubScreenSpec {
    PanelKind   kind;
    uint32_t    strref;     // 0xFFFFFFFF = no strref, use literal
    const char* literal;
};
static const InGameSubScreenSpec k_inGameSubScreens[] = {
    { PanelKind::InGameEquip,     0xFFFFFFFFu, "Ausr\xfcstung" },
    { PanelKind::InGameInventory, 48220u,      "Inventar" },
    { PanelKind::InGameCharacter, 48225u,      "Charakterblatt" },
    { PanelKind::InGameMap,       48221u,      "Karte" },
    { PanelKind::InGameAbilities, 48224u,      "F\xe4higkeiten" },
    { PanelKind::InGameJournal,   48218u,      "Auftr\xe4ge" },
    { PanelKind::InGameOptions,   48222u,      "Optionen" },
    { PanelKind::InGameMessages,  48223u,      "Nachrichten" },
};
static const InGameSubScreenSpec* FindInGameSubScreenSpec(PanelKind k) {
    for (const auto& s : k_inGameSubScreens) {
        if (s.kind == k) return &s;
    }
    return nullptr;
}

// Walk the manager's panels[] for any in-game sub-screen panel
// (CSWGuiInGameInventory / Map / Journal / …). Used by the drill router to
// retarget the chain when g_drilledIntoSubScreen is set and the strip is fg.
//
// Returns the lowest-index match. CSWGuiManager::SendPanelToBack inserts at
// front of panels[], so the most recently opened sub-screen typically lives
// at index 0 — which is also what the user expects to navigate. Multiple
// sub-screens shouldn't normally coexist (SwitchToSWInGameGui pops the
// previous one before adding the new one), but if it ever happens we pick
// the first match deterministically.
static void* FindActiveSubScreenPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (FindInGameSubScreenSpec(k)) return p;
    }
    return nullptr;
}

// Tracks which sub-screen panel pointers are currently in the manager's
// panels[]. Panels added since last tick → speak the screen's localized
// name once. Removed → drop from the tracked set so a re-open re-announces.
// Panels[] is small (≤16 in our cap) and turnover is human-paced, so a flat
// array is fine.
static void* g_visibleSubScreens[16];
static int   g_visibleSubScreenCount = 0;

static bool IsSubScreenTracked(void* p) {
    for (int i = 0; i < g_visibleSubScreenCount; ++i) {
        if (g_visibleSubScreens[i] == p) return true;
    }
    return false;
}

// Walk current panels[], speak on additions of any tracked sub-screen kind,
// drop removals from the tracked set. Called from MonitorPanelContents per
// tick, before the per-panel content fingerprint pass — the kind name lands
// first, then the fingerprint diff fills in the actual labels/items.
static void AnnounceNewSubScreens(void** panels, int count) {
    void* nowVisible[16];
    int   nowCount = 0;
    for (int i = 0; i < count && nowCount < 16; ++i) {
        void* p = panels[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        const InGameSubScreenSpec* spec = FindInGameSubScreenSpec(k);
        if (!spec) continue;
        nowVisible[nowCount++] = p;
        if (IsSubScreenTracked(p)) continue;
        // First sight in panels[] for this address+kind — speak the screen's
        // localized name. The user already heard the icon's name on focus
        // before activating; this is the "you are now in this screen"
        // confirmation.
        char text[128];
        bool spoke = false;
        if (spec->strref != 0xFFFFFFFFu &&
            LookupTlk(spec->strref, text, sizeof(text))) {
            acclog::Write("SubScreen open: panel=%p kind=%s strref=%u text=\"%s\"",
                          p, PanelKindName(k), spec->strref, text);
            tolk::Speak(text, /*interrupt=*/false);
            spoke = true;
        }
        if (!spoke) {
            acclog::Write("SubScreen open: panel=%p kind=%s text=\"%s\" (literal)",
                          p, PanelKindName(k), spec->literal);
            tolk::Speak(spec->literal, /*interrupt=*/false);
        }
    }
    memcpy(g_visibleSubScreens, nowVisible, sizeof(nowVisible));
    g_visibleSubScreenCount = nowCount;
}

// Build a fingerprint of the panel's label/listbox content. Buttons are
// skipped because their text mutates on hover (rendered border changes
// would create false-positive content changes). Whitespace-only fields
// are skipped (engine uses " " as a "not-yet-bound" placeholder).
//
// Output is the concatenation of contents separated by ' | ', truncated
// at outSize.
static void BuildContentFingerprint(void* panel, char* out, size_t outSize) {
    if (outSize == 0) return;
    out[0] = '\0';
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 32 ? 32 : list->size;
    size_t off = 0;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        // Skip buttons — hover state mutates their border-rendered text.
        if (CallDowncast(c, kVtableAsButton) != nullptr) continue;
        if (CallDowncast(c, kVtableAsButtonToggle) != nullptr) continue;

        char text[256];
        const char* src = ExtractAnnounceableText(c, text, sizeof(text), panel);
        if (!src) continue;
        size_t tlen = strnlen(text, sizeof(text));
        if (tlen == 0) continue;

        // Skip whitespace-only.
        bool allWs = true;
        for (size_t k = 0; k < tlen; ++k) {
            char ch = text[k];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                allWs = false; break;
            }
        }
        if (allWs) continue;

        size_t needed = (off > 0 ? 3 : 0) + tlen;
        if (off + needed + 1 >= outSize) break;
        if (off > 0) {
            out[off++] = ' ';
            out[off++] = '|';
            out[off++] = ' ';
        }
        memcpy(out + off, text, tlen);
        off += tlen;
        out[off] = '\0';
    }
}

// Get-or-create the snapshot slot for a panel. FIFO-evicts when full.
static char* GetContentSnapshot(void* panel) {
    for (int i = 0; i < g_contentSnapshotCount; ++i) {
        if (g_contentSnapshots[i].panel == panel) {
            return g_contentSnapshots[i].text;
        }
    }
    if (g_contentSnapshotCount >= kMaxContentSnapshots) {
        memmove(g_contentSnapshots, g_contentSnapshots + 1,
                sizeof(g_contentSnapshots[0]) * (kMaxContentSnapshots - 1));
        g_contentSnapshotCount = kMaxContentSnapshots - 1;
    }
    int idx = g_contentSnapshotCount++;
    g_contentSnapshots[idx].panel = panel;
    g_contentSnapshots[idx].text[0] = '\0';
    return g_contentSnapshots[idx].text;
}

// Per-tick content scan. Walks the manager's panels[] (top to bottom),
// finds any panel of an interesting kind, snapshots its content
// fingerprint, announces diffs. Persistent panels with stable content
// (dialog-letterbox borders, etc.) settle to a fingerprint that matches
// the snapshot and stay quiet; only changes speak.
static void MonitorPanelContents() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return;
    if (panelCount > 16) panelCount = 16;

    // Speak on first sight of an in-game sub-screen (Inventory, Map, …).
    // Runs before the content-fingerprint loop so the kind name lands
    // before the per-panel label dump for the same panel.
    AnnounceNewSubScreens(panelData, panelCount);

    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (!IsContentMonitored(k)) continue;

        char fingerprint[512];
        BuildContentFingerprint(p, fingerprint, sizeof(fingerprint));

        char* last = GetContentSnapshot(p);
        if (strncmp(last, fingerprint, sizeof(g_contentSnapshots[0].text)) == 0) {
            continue;  // unchanged
        }

        if (fingerprint[0] != '\0') {
            acclog::Write("ContentChange: panel=%p kind=%s",
                          p, PanelKindName(k));
            acclog::Write("ContentChange:   prev=\"%.300s\"", last);
            acclog::Write("ContentChange:   curr=\"%.300s\"", fingerprint);
            tolk::Speak(fingerprint, /*interrupt=*/false);
        } else {
            acclog::Write("ContentChange: panel=%p kind=%s fingerprint cleared "
                          "(prev=\"%.100s\")", p, PanelKindName(k), last);
        }
        strncpy_s(last, sizeof(g_contentSnapshots[0].text),
                  fingerprint, _TRUNCATE);
    }
}

// =============================================================================
// Dialog-reply selection monitor.
//
// During an in-game conversation, the foreground panel is a CSWGuiDialog
// subclass (CSWGuiDialogCinematic, DialogComputer, etc.) whose child[1] is
// a CSWGuiListBox holding the player's reply choices. The engine's per-row
// arrow-key navigation mutates listbox.selection_index in place WITHOUT
// firing either CSWGuiPanel::SetActiveControl or CSWGuiListBox::
// SetActiveControl — so without a poll we never hear which reply is
// currently highlighted. The user sees the visual highlight move but we
// stay silent.
//
// MonitorDialogReplies snapshots selection_index per-listbox, announces
// the row's extracted text on change. State resets when we leave a dialog
// (so re-entering a new one announces from the new initial state). The
// content monitor (Layer 3) still handles the one-shot announcement of the
// full reply list when it first appears; this monitor is purely for
// per-row navigation announcements.
// =============================================================================

struct DialogReplyState {
    void* listBox;
    short lastSelection;
};
static DialogReplyState g_dialogReplyState = { nullptr, -1 };

static bool IsDialogPanelKind(PanelKind k) {
    switch (k) {
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
        return true;
    default:
        return false;
    }
}

// Find the first CSWGuiListBox child in a panel's controls. Returns
// nullptr if none. CSWGuiDialog::replies_listbox is at child[1] in
// observed panels (preceded by the message_label at child[0]); first-
// match on IsListBox is robust enough for the dialog case.
static void* FindListBoxChild(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 32 ? 32 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (c && IsListBox(c)) return c;
    }
    return nullptr;
}

static void MonitorDialogReplies() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;

    // Scan ALL panels in the manager's panels[] for a dialog-kind panel.
    // Was previously gating on fg, which fails because during arrow-key
    // navigation in a dialog the foreground panel switches to a separate
    // auxiliary panel (Unknown kind) — the actual dialog-cinematic panel
    // stays in panels[] but isn't fg, so the old fg-only check rejected
    // the dialog and reset the monitor state on every keystroke. Verified
    // in patch-20260502-192712.log: the same listbox 0FE2D434 stayed
    // allocated through all 8 reply turns; selection_index successfully
    // changed (initialSel went from -1 → 1 → 0 across turns) but every
    // change was missed because the monitor reset between them.
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* dialogPanel = nullptr;
    PanelKind dialogKind = PanelKind::Unknown;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            PanelKind pk = IdentifyPanel(p);
            if (IsDialogPanelKind(pk)) {
                dialogPanel = p;
                dialogKind  = pk;
                break;
            }
        }
    }

    if (!dialogPanel) {
        if (g_dialogReplyState.listBox) {
            acclog::Write("Dialog reply monitor disarmed: no dialog panel in stack");
            g_dialogReplyState.listBox = nullptr;
            g_dialogReplyState.lastSelection = -1;
        }
        return;
    }

    void* lb = FindListBoxChild(dialogPanel);
    if (!lb) return;
    PanelKind k = dialogKind;
    void* fg = dialogPanel;  // for log-line compatibility below

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    // First sight of this listbox: snapshot only (don't announce — the
    // content monitor already spoke the full reply list when the dialog
    // entered the reply state).
    if (g_dialogReplyState.listBox != lb) {
        g_dialogReplyState.listBox = lb;
        g_dialogReplyState.lastSelection = selIdx;
        acclog::Write("Dialog reply monitor armed: panel=%p kind=%s listbox=%p "
                      "initialSel=%d", fg, PanelKindName(k), lb, selIdx);
        return;
    }

    if (selIdx == g_dialogReplyState.lastSelection) return;
    short prev = g_dialogReplyState.lastSelection;
    g_dialogReplyState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Dialog reply selection cleared: listbox=%p prev=%d",
                      lb, prev);
        return;
    }

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Dialog reply selection out of range: listbox=%p sel=%d "
                      "size=%d", lb, selIdx,
                      (lbList ? lbList->size : -1));
        return;
    }

    void* row = lbList->data[selIdx];
    if (!row) return;

    char text[256];
    const char* src = ExtractAnnounceableText(row, text, sizeof(text));
    if (src) {
        acclog::Write("Dialog reply selected: panel=%p kind=%s listbox=%p "
                      "sel=%d (was %d) src=%s text=\"%s\"",
                      fg, PanelKindName(k), lb, selIdx, prev, src, text);
        tolk::Speak(text, /*interrupt=*/false);
    } else {
        char vtbl[160];
        DumpControlVtable(row, vtbl, sizeof(vtbl));
        acclog::Write("Dialog reply selected (src=none): panel=%p listbox=%p "
                      "sel=%d row=%p %s", fg, lb, selIdx, row, vtbl);
    }
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
    MonitorFocusedControl();
    MonitorPanelContents();
    MonitorDialogReplies();
    if (!g_pendingCursorMove && !g_pendingClick && !g_pendingActivate &&
        !g_pendingSliderInput) return;

    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) {
        acclog::Write("Update: pending op but GuiManager singleton is NULL");
        g_pendingCursorMove     = false;
        g_pendingClick          = false;
        g_pendingActivate       = false;
        g_pendingSliderInput    = false;
        g_pendingTarget         = nullptr;
        g_pendingActivateTarget = nullptr;
        g_pendingSliderTarget   = nullptr;
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
    //
    // Post-activation re-announce is handled generically by
    // MonitorFocusedControl on the next tick: toggles flip +0x1c8 bit 0
    // synchronously inside FireActivate, cycles rewrite the value-display
    // button's CExoString in place, sliders mutate cur_value at +0x74. All
    // three produce a different ExtractAnnounceableText on next entry, and
    // the monitor speaks the diff.
    if (g_pendingActivate) {
        g_pendingActivate = false;
        void* tgt = g_pendingActivateTarget;
        g_pendingActivateTarget = nullptr;
        acclog::Write("Update: FireActivate target=%p", tgt);
        FireActivate(tgt);
    }

    // Slider value adjustment via vtable[15].HandleInputEvent(500/501, 1).
    // The slider's HandleInputEvent runs SetCurValue (clamped to
    // [0, max_value]) and the gui_object callback that propagates to the
    // audio system, then plays the click feedback sound. Per-frame focus
    // monitor catches the cur_value change at +0x74 on the next tick.
    if (g_pendingSliderInput) {
        g_pendingSliderInput = false;
        void* tgt  = g_pendingSliderTarget;
        int   code = g_pendingSliderCode;
        g_pendingSliderTarget = nullptr;
        g_pendingSliderCode   = 0;
        if (tgt) {
            void** vtable = *reinterpret_cast<void***>(tgt);
            if (vtable) {
                auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
                    vtable[kVtableHandleInputEvent]);
                if (fn) {
                    acclog::Write("Update: slider HandleInputEvent target=%p code=%d",
                                  tgt, code);
                    fn(tgt, code, 1);
                }
            }
        }
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
