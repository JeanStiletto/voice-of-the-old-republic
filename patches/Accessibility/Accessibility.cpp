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
    return "?";
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

// Resolve a strref via the engine's TLK lookup. Currently DISABLED — the
// previous test session showed that after entering Options the patch went
// silent (no further hook log entries), and TLK lookup is the most likely
// culprit. Reverting to a safe baseline that just records the strref so we
// can verify the field is populated, then we'll re-enable the engine call
// once we understand the failure mode (likely needs a try/except guard or
// strref bounds-check).
//
// When re-enabling, the working pattern is:
//   void* tlk = *reinterpret_cast<void**>(kAddrTlkTablePtr);   // *0x007a3a08
//   CExoString tmp = {nullptr, 0};
//   auto fn = reinterpret_cast<PFN_GetSimpleString>(kAddrGetSimpleString);
//   fn(tlk, &tmp, strref);  // engine fills tmp.c_string + tmp.length
// The c_string allocation is engine-CRT — leak it (focus events are
// low-frequency; bounded growth across a session).
static bool g_logStrRef = true;  // diagnostic: log strref values when set
static bool LookupTlk(uint32_t strref, char* /*outBuf*/, size_t /*bufSize*/) {
    if (strref == 0 || strref == 0xFFFFFFFF) return false;
    if (g_logStrRef) {
        acclog::Write("strref candidate: %u (TLK lookup disabled)", strref);
    }
    return false;
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

    if (!newControl) {
        if (n <= 100 || n % 50 == 0) {
            acclog::Write("SetActiveControl #%d panel=%p newControl=NULL", n, panel);
        }
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newControl) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newControl, text, sizeof(text));

    if (source) {
        if (n <= 100 || n % 50 == 0) {
            acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d src=%s text=\"%s\"",
                          n, panel, newControl, id, source, text);
        }
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

    if (!newRow) {
        if (n <= 100 || n % 50 == 0) {
            acclog::Write("ListBox::SetActiveControl #%d list=%p newRow=NULL p2=%d",
                          n, listBox, param2);
        }
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newRow) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newRow, text, sizeof(text));

    if (source) {
        if (n <= 100 || n % 50 == 0) {
            acclog::Write("ListBox::SetActiveControl #%d list=%p row=%p id=%d "
                          "p2=%d src=%s text=\"%s\"",
                          n, listBox, newRow, id, param2, source, text);
        }
        SpeakIfChanged(/*channel=*/1, text);
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
    if (n <= 50 || n % 100 == 0) {
        const char* tip; uint32_t tipLen; int id;
        ReadControlNameFields(thisPtr, tip, tipLen, id);
        acclog::Write("HandleFocusChange #%d this=%p p1=%d id=%d tip[%u]=\"%s\"",
                      n, thisPtr, param_1, id, tipLen,
                      (tip && tipLen > 0) ? tip : "");
    }
}

// CSWGuiMainMenu::HandleInputEvent — hooked mid-function at 0x67b395.
// At hook entry: ESI = this, EBX = param_1 (InputIndices key/button code),
// EDI = param_2 (state, always non-zero here — we land after the
// "param_2 == 0" early-out).
extern "C" void __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    if (n <= 1000 || n % 100 == 0) {
        acclog::Write("HandleInputEvent #%d this=%p key=%s(%d) val=%d",
                      n, thisPtr, InputIndexName(param_1), param_1, param_2);
    }
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
