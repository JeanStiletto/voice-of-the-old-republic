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

// GetAsyncKeyState lives in user32.lib, which the upstream create-patch.bat
// doesn't link by default (it links kernel32 + sqlite3 only). Pragma the
// dependency in here rather than touching the vendored build script.
#pragma comment(lib, "user32.lib")

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

// Chain state. g_currentPanel is updated in OnSetActiveControl. g_chainPanel
// is rebound lazily per arrow press if the focused panel has changed since the
// last navigation. A null current panel disables chain nav (fall through to
// the engine for unsupported screens — e.g. the title-screen K/L cycle still
// works, per Decision 7).
static void* g_currentPanel = nullptr;
static void* g_chainPanel   = nullptr;
static int   g_chainIndex   = 0;
static int   g_chainSize    = 0;

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

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// Tabbed-panel navigation state (Options-menu style: a CSWGuiListBox at
// controls[0] holds the current tab's content, button cluster after [0] = tabs).
//
// The two orders that matter for tabs:
//   1. panel.controls index order — how the engine STORES the buttons
//      (tabsStart..tabsStart+tabsCount-1).
//   2. Visual screen order — top-to-bottom by y-center.
// In the Options panel these orders DIFFER (Feedback is panel.controls[5] but
// visually sits between Gameplay and Auto-Pause). The engine's tab-cycling
// behavior is driven by visual order, so we sort by y at arming time and
// drive the cursor warp from g_visualTabs[].
//
// Empirical engine behavior, derived from patch-20260501-152849 log:
// when we MoveMouseToPosition to visual tab P's center, the engine activates
// visual tab P-1 (the tab visually above P). No wrap at 0 — warping to the
// topmost tab (visual 0) yields no activation. Consequence: to activate
// visual idx N, warp the cursor to visual idx N+1 mod count.
//
// Known limitation: the bottom tab (visual idx count-1) is unreachable
// because warping past it lands on visual 0, which doesn't activate anything.
// In the Options panel that means Sound stays unreachable via Tab. Reaching
// it needs a different mechanism (number-key shortcut, or a safe direct
// activation path) — separate task.
//
// Detection deliberately keys off "controls[0] is a non-empty CSWGuiListBox"
// rather than "panel has buttons" — the main menu also has buttons but no
// active listbox, and arrow-keys must continue to drive chain navigation
// there. kVtableListBox is the observed Steam-1.0.3 vtable for CSWGuiListBox;
// future builds will need updating if Lane's SARIF reports a different value.
//
// Tab arrives as logical code 0xCE (206), confirmed via patch-20260430-202843
// log: HandleInputEvent fired with raw param_1 = 206 on every Tab press, never
// translated to an InputIndices value (the engine's translator passes 0xCE
// through unchanged).
constexpr int       kInputTab       = 0xCE;
constexpr uintptr_t kVtableListBox  = 0x0073E840;

constexpr int kMaxVisualTabs = 16;
struct VisualTab {
    void* control;
    int   xCenter;
    int   yCenter;
};

static void*      g_tabbedPanel       = nullptr;  // panel currently in tabbed mode
static int        g_tabsStart         = -1;       // first tab-button index in panel.controls
static int        g_tabsCount         = 0;        // number of contiguous tab buttons
static VisualTab  g_visualTabs[kMaxVisualTabs];   // sorted by yCenter (top→bottom)
static int        g_visualTabCount    = 0;
// GuiManager pointer captured when Tab mode was armed. The engine swaps
// App→gui_manager between manager-shaped objects across state transitions
// (verified: after our setActive on a tab, an internal event fires through
// HandleInputEvent with a different `this` pointer than the one driving
// real user input). When OnHandleInputEvent sees a Tab event whose `thisPtr`
// doesn't match this captured value, the event is part of the engine's
// post-activation cascade — not a user press — and we must NOT re-handle it.
static void*      g_armedManager      = nullptr;

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

// Linear scan a panel's controls list for `needle`. Returns the index, or
// -1 if not found / list empty / corrupt.
static int FindControlIndex(void* panel, void* needle) {
    if (!panel || !needle) return -1;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return -1;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        if (list->data[i] == needle) return i;
    }
    return -1;
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

// Step the chain index forward (or backward) by stride, skipping NULL slots
// and non-navigable controls. Clamps at the ends of panel.controls. Returns
// the new index. The stride lets the caller walk through panels with mixed
// content without forcing the user to stride-press through labels.
static int AdvanceChainIndex(void* panel, int from, int delta) {
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list || !list->data || list->size <= 0) return from;
    int max = (list->size > 256 ? 256 : list->size) - 1;

    int candidate = from + delta;
    while (candidate >= 0 && candidate <= max) {
        void* c = list->data[candidate];
        if (IsChainNavigable(c)) return candidate;
        candidate += delta;
    }
    // No navigable control in the requested direction — clamp to last
    // navigable seen, or stay put if there isn't one.
    int probe = from;
    while (probe >= 0 && probe <= max) {
        if (IsChainNavigable(list->data[probe])) return probe;
        probe += (delta > 0) ? -1 : +1;
    }
    return from;
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

// Build g_visualTabs[] sorted top-to-bottom by y-center. Called once at
// arming time. Buttons with degenerate extents (no center) are skipped.
static void BuildVisualTabOrder(void* panel, int tabsStart, int tabsCount) {
    g_visualTabCount = 0;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int end = tabsStart + tabsCount;
    int cap = list->size > 256 ? 256 : list->size;
    if (end > cap) end = cap;
    for (int i = tabsStart; i < end && g_visualTabCount < kMaxVisualTabs; ++i) {
        void* btn = list->data[i];
        if (!btn) continue;
        int cx, cy;
        if (!GetControlCenter(btn, cx, cy)) continue;
        g_visualTabs[g_visualTabCount].control = btn;
        g_visualTabs[g_visualTabCount].xCenter = cx;
        g_visualTabs[g_visualTabCount].yCenter = cy;
        ++g_visualTabCount;
    }
    // Insertion sort by yCenter (small N, simple is fine).
    for (int i = 1; i < g_visualTabCount; ++i) {
        VisualTab v = g_visualTabs[i];
        int j = i;
        while (j > 0 && g_visualTabs[j - 1].yCenter > v.yCenter) {
            g_visualTabs[j] = g_visualTabs[j - 1];
            --j;
        }
        g_visualTabs[j] = v;
    }
}

// Linear search visual order for `control`. Returns -1 if not a tab.
static int FindVisualTabIdx(void* control) {
    if (!control) return -1;
    for (int i = 0; i < g_visualTabCount; ++i) {
        if (g_visualTabs[i].control == control) return i;
    }
    return -1;
}


// Reset all tabbed-mode state. Called when the focused panel changes to a
// different one — the new panel may or may not be tabbed; OnListBoxSetActive-
// Control re-runs detection lazily on its first listbox event.
static void ResetTabbedState() {
    g_tabbedPanel      = nullptr;
    g_tabsStart        = -1;
    g_tabsCount        = 0;
    g_visualTabCount   = 0;
    g_virtualLineCount = 0;
    g_virtualLineIdx   = -1;
    g_armedManager     = nullptr;
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

// (Re)bind the chain to the currently focused panel. Anchors g_chainIndex on
// the engine's current activeControl when present, so the first arrow press
// moves one step from where the user was, not from index 0.
static void RebindChain(void* panel) {
    g_chainPanel = panel;
    g_chainIndex = 0;
    g_chainSize  = 0;
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    g_chainSize = list->size > 256 ? 256 : list->size;

    void* active = ReadPanelActiveControl(panel);
    int idx = FindControlIndex(panel, active);
    g_chainIndex = (idx >= 0) ? idx : 0;
    acclog::Write("Chain rebind panel=%p size=%d index=%d active=%p",
                  panel, g_chainSize, g_chainIndex, active);
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

    // Tabbed-mode state is per-panel. When the focused panel changes, drop any
    // tabbed state from the previous panel; OnListBoxSetActiveControl re-detects
    // on its next firing (which is what populates tabbed mode in the first place).
    if (panel != prevPanel && panel != g_tabbedPanel) {
        ResetTabbedState();
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
                g_tabbedPanel  = g_currentPanel;
                g_tabsStart    = tabsStart;
                g_tabsCount    = tabsCount;
                g_armedManager = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
                BuildVisualTabOrder(g_currentPanel, tabsStart, tabsCount);
                acclog::Write("Tab mode armed: panel=%p mgr=%p tabsStart=%d tabsCount=%d "
                              "visualTabs=%d",
                              g_currentPanel, g_armedManager, tabsStart, tabsCount,
                              g_visualTabCount);
                for (int i = 0; i < g_visualTabCount; ++i) {
                    char btext[128];
                    const char* src = ExtractAnnounceableText(
                        g_visualTabs[i].control, btext, sizeof(btext));
                    acclog::Write("  visual[%d] %p y=%d text=\"%s\"",
                                  i, g_visualTabs[i].control,
                                  g_visualTabs[i].yCenter,
                                  src ? btext : "(none)");
                }
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
    // Enter-press commit: force panel.activeControl to match the chain
    // target so the engine's normal activation pipeline fires onClick on
    // the right button. Single-shot (only on press, not release) and only
    // when chain state is current — avoids the focus oscillation that
    // crashed when an explicit setActive ran on every arrow press.
    if (param_2 != 0 &&
        (param_1 == kInputEnter1 || param_1 == kInputEnter2) &&
        g_currentPanel != nullptr &&
        g_chainPanel == g_currentPanel &&
        g_chainSize > 0)
    {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(g_chainPanel) + kPanelControlsOffset);
        void* target = (list && list->data && g_chainIndex < list->size)
                       ? list->data[g_chainIndex] : nullptr;
        if (target && IsChainNavigable(target)) {
            void* current = ReadPanelActiveControl(g_currentPanel);
            if (current != target) {
                acclog::Write("Enter commit: panel=%p target=%p (was %p)",
                              g_currentPanel, target, current);
                auto setActive = reinterpret_cast<PFN_PanelSetActiveControl>(
                    kAddrPanelSetActiveControl);
                setActive(g_currentPanel, target);
            }
        }
    }

    bool consumed = false;

    // Tab key in a tabbed panel.
    //
    // Plan M: explicitly commit the tab change via CSWGuiPanel::
    // SetActiveControl, the same pattern Enter uses for chain nav.
    // No cursor warp — that approach kept getting reverted by the
    // engine's release-edge handler (which apparently treats hover-
    // induced focus changes as transient and undoes them on key up).
    //
    // We cycle in VISUAL order, top-to-bottom on screen. Each press
    // moves Δ=1 in visual idx. Read panel.activeControl each press to
    // anchor the cycle in reality so we don't drift if the engine
    // does anything unexpected.
    //
    // Sound (the bottom-most visual tab) becomes reachable because we
    // call SetActiveControl directly with the tab pointer — there's no
    // cursor-position hit-test to defeat us.
    //
    // Shift+Tab is detected via GetAsyncKeyState(VK_SHIFT) — KOTOR's
    // manager-level HandleInputEvent doesn't pass modifier state with
    // the key code.
    //
    // We don't announce in the handler. The setActive call fires our
    // OnSetActiveControl hook synchronously, which speaks the engine's
    // resulting activation via SpeakIfChanged. By construction that
    // text matches the new listbox content.
    //
    // Manager-pointer guard (`thisPtr == g_armedManager`): the engine
    // swaps App→gui_manager between manager-shaped objects across state
    // transitions. After our setActive on a tab, the engine fires a
    // post-activation cascade event back through HandleInputEvent with a
    // DIFFERENT `thisPtr` (e.g. armed mgr 06CEEF00 vs cascade mgr
    // 06B93F48 — see patch-20260502-035745 log lines 26 vs 92, plus
    // CExoInputInternal::BufferEvent diagnosis confirming this event is
    // not a real Win32 message). That cascade is the engine's normal
    // post-activation work; if we re-enter our Tab logic on it (or even
    // just consume it), we corrupt the cascade and crash. Letting it
    // pass through unchanged is the right behavior.
    if (param_1 == kInputTab &&
        g_tabbedPanel != nullptr && g_tabbedPanel == g_currentPanel &&
        g_visualTabCount > 0 &&
        thisPtr == g_armedManager)
    {
        // Only cycle on val=128 (standard press edge). Releases (val=0)
        // are consumed without action. Re-entry guard kept as cheap
        // insurance against any reentry path the manager-pointer guard
        // doesn't catch.
        static bool s_inTabHandler = false;
        if (param_2 == 128 && !s_inTabHandler) {
            s_inTabHandler = true;

            bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            int n = g_visualTabCount;

            void* active = ReadPanelActiveControl(g_currentPanel);
            int currentVisual = FindVisualTabIdx(active);
            if (currentVisual < 0) currentVisual = 0;

            int targetVisual = isShift
                ? (currentVisual + n - 1) % n
                : (currentVisual + 1) % n;

            void* targetCtl = g_visualTabs[targetVisual].control;
            if (targetCtl && targetCtl != active) {
                auto setActive = reinterpret_cast<PFN_PanelSetActiveControl>(
                    kAddrPanelSetActiveControl);
                acclog::Write("Tab cycle %s: current=%d target=%d ctl=%p "
                              "(was %p)",
                              isShift ? "BACK" : "FWD",
                              currentVisual, targetVisual, targetCtl, active);
                setActive(g_currentPanel, targetCtl);
            } else {
                acclog::Write("Tab cycle %s: current=%d target=%d ctl=%p "
                              "(no change — already active)",
                              isShift ? "BACK" : "FWD",
                              currentVisual, targetVisual, targetCtl);
            }

            s_inTabHandler = false;
        }
        consumed = true;
    }
    // Arrow keys in a tabbed panel: walk the parsed virtual lines. No engine
    // activation — the multi-line listbox blob has no per-line click target
    // (controls.size == 1), so this is a read-only enumeration for now.
    // Future: synthesize per-line activation when we figure out the engine
    // surface for clicking individual settings.
    else if (param_2 != 0 &&
             (param_1 == kInputNavUp || param_1 == kInputNavDown) &&
             g_tabbedPanel != nullptr && g_tabbedPanel == g_currentPanel &&
             g_virtualLineCount > 0)
    {
        int delta = (param_1 == kInputNavDown) ? +1 : -1;
        if (g_virtualLineIdx < 0) {
            g_virtualLineIdx = (delta > 0) ? 0 : g_virtualLineCount - 1;
        } else {
            g_virtualLineIdx += delta;
            if (g_virtualLineIdx < 0) g_virtualLineIdx = 0;
            if (g_virtualLineIdx >= g_virtualLineCount) g_virtualLineIdx = g_virtualLineCount - 1;
        }
        const char* line = g_virtualLines[g_virtualLineIdx];
        tolk::Speak(line, /*interrupt=*/false);
        acclog::Write("VLine: panel=%p line=%d/%d %s text=\"%s\"",
                      g_currentPanel, g_virtualLineIdx, g_virtualLineCount,
                      param_1 == kInputNavDown ? "DOWN" : "UP", line);
        consumed = true;
    }
    // Arrow keys in a non-tabbed panel: existing chain navigation.
    else if (param_2 != 0 &&
        (param_1 == kInputNavUp || param_1 == kInputNavDown) &&
        g_currentPanel != nullptr)
    {
        if (g_currentPanel != g_chainPanel) {
            RebindChain(g_currentPanel);
        }
        if (g_chainSize > 0) {
            int delta = (param_1 == kInputNavDown) ? +1 : -1;
            int newIndex = AdvanceChainIndex(g_chainPanel, g_chainIndex, delta);
            g_chainIndex = newIndex;

            auto* list = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(g_chainPanel) + kPanelControlsOffset);
            void* target = (list && list->data) ? list->data[g_chainIndex] : nullptr;

            if (target) {
                AnnounceControl(target);
                int cx, cy;
                if (GetControlCenter(target, cx, cy)) {
                    g_pendingX = cx;
                    g_pendingY = cy;
                    g_pendingTarget = target;
                    g_pendingCursorMove = true;
                    acclog::Write("Chain step panel=%p index=%d target=%p center=(%d,%d) %s",
                                  g_chainPanel, g_chainIndex, target, cx, cy,
                                  param_1 == kInputNavDown ? "DOWN" : "UP");
                } else {
                    acclog::Write("Chain step panel=%p index=%d target=%p extent=degenerate",
                                  g_chainPanel, g_chainIndex, target);
                }
            } else {
                acclog::Write("Chain step panel=%p index=%d target=NULL %s",
                              g_chainPanel, g_chainIndex,
                              param_1 == kInputNavDown ? "DOWN" : "UP");
            }
            // Always consume nav-up/nav-down on a panel with a non-empty chain.
            // AdvanceChainIndex skips NULL slots and non-navigable controls,
            // but if the panel has zero navigables we still consume rather
            // than letting the key fall through to the engine's broken cycle.
            consumed = true;
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
    if (!g_pendingCursorMove) return;
    g_pendingCursorMove = false;

    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) {
        acclog::Write("Update: pending move but GuiManager singleton is NULL");
        g_pendingTarget = nullptr;
        return;
    }

    // Move the cursor. The engine's MoveMouseToPosition internally drives
    // HitCheckMouse → UpdateMouseOverControl → CSWGuiPanel::SetActiveControl,
    // so the cursor move alone updates panel.activeControl to whatever the
    // hit-test resolves at the new coords. Verified by Phase-2 testing:
    // pressing Enter after a chain step activated the focused control
    // (e.g. Options button → Options menu) without any explicit setActive
    // call. An earlier prototype that *also* called SetActiveControl(target)
    // here caused crashes on panels where multiple controls share hit-test
    // space (Options menu tabs at overlapping screen positions): the engine
    // would activate the hit-test winner, our explicit setActive would
    // override it back to the chain target, the listbox would refresh
    // twice, and rapid state oscillation destabilized the engine.
    auto move = reinterpret_cast<PFN_MoveMouseToPosition>(kAddrMoveMouseToPosition);
    move(gm, g_pendingX, g_pendingY);

    acclog::Write("Update: MoveMouseToPosition(%d, %d) target=%p",
                  g_pendingX, g_pendingY, g_pendingTarget);
    // g_pendingTarget cleared by OnSetActiveControl's self-dedup path when
    // the engine's hover→active flow lands on the same control we asked
    // for; otherwise it stays set and the next chain step overwrites it.
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
