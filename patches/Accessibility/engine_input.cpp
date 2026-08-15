#include "engine_input.h"

#include <windows.h>
#include <intrin.h>   // _ReturnAddress — names the failing mouse-init step
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "engine_game.h"
#include "engine_offsets_select.h"  // acc::off — the CExoRawInputInternal
                                    // device fields moved by 4 bytes in K2
#include "engine_rebase.h"
#include "pad_input.h"   // CodeHint — the KOTOR 2 pad shares this numbering
#include "prism.h"       // SpeakUrgent — the keyboard-lost warning
#include "strings.h"

namespace acc::engine {

namespace {

// Engine global holding the CExoInput facade pointer (the same symbol
// HideLoadScreen reads before calling SetActive). Verified in Lane's DB:
// SYMBOL ExoInput @0x007a39e4.
//
// KOTOR 2 @0x00a1b48c, resolved 2026-08-13 (it sat on TodoGlobal until then,
// so BOTH constants here were 0 on KOTOR 2 and every reacquire/release faulted
// into the SEH handler below — 25 "exception during SetActive" lines in one
// short session, i.e. the focus-theft fix did not exist there). Two
// independent witnesses agree:
//   * the constructor at 0x0072f760 writes the CExoInput RTTI vtable
//     (0x0099cab8) into the new object, and 0x00409c7e stores that object to
//     0x00a1b48c;
//   * the engine's global-pointer cluster keeps KOTOR 1's relative layout
//     (ExoResMan/GuiMan/Aurora/AppMan/VM/Tlk/Rules deltas all match), and
//     KOTOR 1's ExoInput is likewise the dword below ExoResMan.
const uintptr_t kAddrExoInputGlobal =
    acc::addr::PickGlobal(0x007a39e4, 0x00a1b48c);

// CExoInput::SetActive(this, int active) @0x005df540. Sets the facade +
// CExoInputInternal active flags and forwards to
// CExoRawInputInternal::SetActive, which Acquire()s the DirectInput
// keyboard/mouse/joystick devices on the 0->1 transition.
//
// KOTOR 2 @0x0072fb20 — the same three-layer chain, decompiled 2026-08-13:
// 0x0072fb20 forwards this->internal to CExoInputInternal::SetActive
// (0x0072df60), which stores the flag and forwards to
// CExoRawInputInternal::SetActive (0x00733090). That last one confirms the
// design this file depends on: it early-outs when the requested state already
// matches, then calls IDirectInputDevice8::Acquire (vtable +0x1c) or
// ::Unacquire (+0x20) on the keyboard, the mouse and every joystick. So the
// 0->1 edge ForceReacquireInput drives is required on KOTOR 2 for the same
// reason it is on KOTOR 1.
const uintptr_t kAddrCExoInputSetActive =
    acc::addr::Pick(0x005df540, 0x0072fb20);

typedef void(__thiscall* PFN_CExoInputSetActive)(void* this_, int active);

// ---------------------------------------------------------------------------
// CExoRawInputInternal::InitializeDirectInputMouse — no-mouse crash guard
// ---------------------------------------------------------------------------
// Vanilla engine bug, present in both games. The function's whole job is
// IDirectInput8::CreateDevice(GUID_SysMouse, &this->mouse_device, NULL), and it
// loads the interface's vtable without ever testing the interface for NULL:
//
//     MOV EDX, [this + direct_input_interface]
//     MOV EDX, [EDX]          <-- AV when the interface is NULL
//     MOV EAX, [EDX + 0xc]    <-- IDirectInput8 vtable slot 3 = CreateDevice
//     CALL EAX
//
// The interface is NULL exactly when the engine has already torn DirectInput
// down. On any device bring-up failure it calls ShutDownDirectInput, which
// nulls the field — and then reports success to its caller anyway. The mouse
// device pointer is left NULL too, so the per-frame mouse update re-enters
// through the lazy "create the device if we haven't got one" path and
// dereferences the NULL interface. Uptime to the crash is however long the
// intro takes to reach the first main-loop frame.
//
// Who hits it: players with no mouse attached at all — which, for a mod whose
// entire premise is that the game is playable without one, is not an edge case.
//
// The guard returns the engine's own failure value when the interface is NULL.
// That is a state both callers already handle: the caller re-tests the device
// pointer and returns 0, and the mouse update compares against 1 and skips the
// frame's mouse handling. Nothing downstream sees a novel state.
//
// Why an inline trampoline rather than a hooks.toml detour: an earlier attempt
// hooked this entry through the standard KPatchManager wrapper and broke the
// engine's input-pipeline bring-up — the menu came up keyboard-dead and users
// had to alt-tab after every launch. The suspected cause is the wrapper's
// PUSHAD/PUSHFD/CALL overhead colliding with DirectInput's foreground-
// cooperative-level handshake. A bare JMP has none of that overhead.
//
// Crash reports: userlogs/swkotor.exe.58504.dmp (KOTOR 1, fixed v0.4.x) and
// userlogs/kennygamecrashes.7z :: swkotor2.exe.348.dmp (KOTOR 2). The KOTOR 2
// half was unguarded from the first K2 build until this change: the installer
// ran only from OnRulesInit, which is a KOTOR-1-only hook (absent from
// kotor2.hooks.toml) and additionally gated on HandlerEnabled().
const uintptr_t kAddrInitMouse =
    acc::addr::Pick(0x005e3fa0, 0x00731070);

// Where the trampoline rejoins the engine: the instruction after the prologue
// bytes we displace, plus (KOTOR 1 only) the interface load the trampoline
// re-executes itself.
const uintptr_t kAddrInitMouseContinue =
    acc::addr::Pick(0x005e3faa, 0x00731076);

// One past the last byte of InitializeDirectInputMouse — the scan bound for
// the teardown block below. KOTOR 1: Ghidra gives the function as
// 0x005e3fa0..0x005e4044 inclusive, with CExoRawInputInternal::GetMouseState
// (the per-frame update that lazily re-invokes the init) starting at
// 0x005e4050. KOTOR 2: the function's shared epilogue is reached by the
// JMP at the end of the CreateDevice-failed branch and lands at 0x007311b2,
// with the next function at 0x007311d0 — 0x007311c0 sits in the padding
// between them.
//
// Only the bound matters, not the exact last instruction: the scan rewrites a
// site only when its rel32 resolves to ShutDownDirectInput, and the nearest
// other caller of that function in either binary is the engine's own public
// shutdown wrapper, which sits BELOW the init's entry (KOTOR 2 0x00731051) and
// so is outside this range in both directions.
const uintptr_t kAddrInitMouseEnd =
    acc::addr::Pick(0x005e4045, 0x007311c0);

// CExoRawInputInternal::ShutDownDirectInput. The engine's real teardown: it
// Unacquires + Releases the keyboard device, then the mouse, then every
// joystick, frees the joystick arrays, Releases the DI8 interface and NULLs
// every one of those fields (KOTOR 1 decompile 2026-08-15; KOTOR 2 same shape,
// disassembled the same day).
const uintptr_t kAddrShutDownDirectInput =
    acc::addr::Pick(0x005e3dc0, 0x00733200);

// CExoRawInputInternal fields. KOTOR 1 values are Lane's struct
// (/KotOR Types/Exo Types/CExoRawInputInternal, size 0x34); KOTOR 2 values are
// from the disassembly of its InitializeDirectInputMouse / SetActive /
// ShutDownDirectInput, which agree with each other — the three device fields
// each sit 4 bytes higher than in KOTOR 1, while `active` does not move.
const size_t kOffRawActive   = acc::off::Same(0x04);
const size_t kOffRawInterface = acc::off::Pick(0x1c, 0x20);
const size_t kOffRawKeyboard  = acc::off::Pick(0x20, 0x24);
const size_t kOffRawMouse     = acc::off::Pick(0x24, 0x28);

// CExoInput::internal, then CExoInputInternal::raw_input — the walk from the
// ExoInput global to the object that owns the DirectInput devices. KOTOR 2's
// raw_input offset is read off CExoInputInternal::SetActive, which tests that
// field for NULL before forwarding.
const size_t kOffInputInternal = acc::off::Same(0x04);
const size_t kOffInternalRaw   = acc::off::Pick(0x140, 0x1a0);

// KOTOR 1's failure exit (POP EDI; XOR EAX,EAX; ...). KOTOR 1 needs it because
// its prologue has already pushed ESI/EDI by the time we test, so the fail path
// has a frame to unwind. KOTOR 2's function takes no stack arguments and ends
// in a bare RET, so its trampoline tests BEFORE building any frame and returns
// inline — there is no K2 address to find here, hence Kotor1Only.
const uintptr_t kAddrInitMouseFailEpilogue =
    acc::addr::Kotor1Only(0x005e401f);

// Entry bytes we displace, verified before we overwrite them. A mismatch means
// the address is wrong for the running build; skipping is the safe answer,
// since writing a JMP into the middle of an unrelated function corrupts state
// long before it faults.
const uint8_t kInitMousePrologueK1[] = {
    0x83, 0xec, 0x14,  // SUB ESP, 0x14
    0x56,              // PUSH ESI
    0x57,              // PUSH EDI
    0x8b, 0xf9,        // MOV EDI, ECX        (this)
};
const uint8_t kInitMousePrologueK2[] = {
    0x55,              // PUSH EBP
    0x8b, 0xec,        // MOV EBP, ESP
    0x83, 0xec, 0x30,  // SUB ESP, 0x30
};

// Set once a mouse device has proven un-creatable this session. A plain byte
// rather than a bool or an atomic because the mouse guard's trampoline tests it
// with a hand-written CMP against its absolute address, and because the only
// writer is the engine thread that owns DirectInput.
//
// Not a function-local static anywhere: the handler below runs under __try, and
// a function-local static's thread-safe-init guard is exactly the kind of
// unwinding MSVC refuses to mix with SEH (C2712).
volatile uint8_t g_mouseUnavailable = 0;

// One-shot log latches, for the same reason.
bool g_teardownLogged = false;

// Return addresses of the teardown call sites we rewrote, in address order,
// so the handler can say WHICH step of the mouse bring-up failed rather than
// just that one did. The order is the order of the steps in the function —
// CreateDevice, SetDataFormat, SetCooperativeLevel, SetProperty, Acquire —
// which the KOTOR 1 decompile spells out and KOTOR 2 recompiles unchanged.
// KOTOR 1's compiler folds the first four failures into one shared exit, so it
// has two sites where KOTOR 2 has five; the names below are indexed per game.
uintptr_t g_teardownSites[8] = {};
int       g_teardownSiteCount = 0;

const char* TeardownSiteName(int index, int total) {
    static const char* const k5[] = {
        "CreateDevice", "SetDataFormat", "SetCooperativeLevel", "SetProperty",
        "Acquire",
    };
    static const char* const k2sites[] = {
        "CreateDevice/SetDataFormat/SetCooperativeLevel/SetProperty (shared "
        "failure exit)",
        "Acquire",
    };
    if (index < 0) return "unknown step";
    if (total == 5 && index < 5) return k5[index];
    if (total == 2 && index < 2) return k2sites[index];
    return "unknown step";
}

// JMP rel32. Twin of save_crash_guard.cpp's WriteRel32Jmp — the other place in
// this patch that hand-writes a trampoline. Kept file-local in both rather than
// promoted to a shared header: two call sites, four lines, and no third caller
// in sight.
void WriteJmpRel32(uint8_t* at, uintptr_t target) {
    at[0] = 0xe9;
    int32_t rel = static_cast<int32_t>(
        target - (reinterpret_cast<uintptr_t>(at) + 5));
    memcpy(at + 1, &rel, 4);
}

}  // namespace

// Names for the InputIndices enum (size 132, definition lifted from Lane's
// Ghidra SARIF: /KotOR Types/Enums/InputIndices). Index = enum value;
// covers MOUSE_*, KEYBOARD_*, JOYSTICK_*. -1 / 0xFFFFFFFF is INPUTDEVICE_NONE.
namespace {
const char* InputIndexNameRaw(int code) {
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
}  // namespace

// Public wrapper. On KOTOR 2 the gamepad reuses this very numbering, so a raw
// name like "KEYBOARD_F12" is actively misleading in a log that is mostly
// about pad events — annotate the codes a pad can produce with what they mean
// on the pad. The keyboard name stays primary and the hint keeps its question
// mark: the numbering genuinely IS shared with real bindablekeys.2da rows, so
// renaming outright would just move the lie.
//
// The small rotating buffer exists because callers pass the result straight
// into a format string; one slot would break a line that names two codes.
const char* InputIndexName(int code) {
    const char* base = InputIndexNameRaw(code);
    const char* hint = acc::pad::CodeHint(code);
    if (!hint) return base;
    static char s_bufs[4][96];
    static int  s_slot = 0;
    s_slot = (s_slot + 1) & 3;
    _snprintf_s(s_bufs[s_slot], sizeof(s_bufs[s_slot]), _TRUNCATE,
                "%s [pad %s?]", base, hint);
    return s_bufs[s_slot];
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

void InstallDirectInputMouseGuard() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    const bool k2 = acc::game::IsKotor2();

    if (!acc::addr::Ok(kAddrInitMouse) ||
        !acc::addr::Ok(kAddrInitMouseContinue) ||
        (!k2 && !acc::addr::Ok(kAddrInitMouseFailEpilogue))) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard: no address for this build (%s); skipped",
            acc::game::BuildName());
        return;
    }

    auto* entry = reinterpret_cast<uint8_t*>(kAddrInitMouse);
    const uint8_t* expect = k2 ? kInitMousePrologueK2 : kInitMousePrologueK1;
    const size_t expectLen =
        k2 ? sizeof(kInitMousePrologueK2) : sizeof(kInitMousePrologueK1);
    if (memcmp(entry, expect, expectLen) != 0) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard: entry bytes at %p are not the expected "
            "prologue (got %02x %02x %02x %02x %02x); skipped — the address is "
            "wrong for this build and patching it would corrupt .text",
            entry, entry[0], entry[1], entry[2], entry[3], entry[4]);
        return;
    }

    constexpr size_t kTrampolineSize = 64;
    auto* tramp = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!tramp) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard install failed: VirtualAlloc returned NULL");
        return;
    }

    uint8_t* p = tramp;

    // Mouse-absent latch, ahead of everything else.
    //
    // The engine's per-frame mouse update re-enters this function on EVERY
    // frame for as long as the mouse device pointer is NULL, so on a machine
    // where the device cannot be created the engine would otherwise run a
    // doomed CreateDevice (a COM call into DirectInput, device enumeration and
    // all) sixty times a second for the whole session. Once
    // InstallMouseTeardownBlock's handler has seen a failure there is nothing
    // left to try, so answer the engine's own failure value immediately.
    //
    // Placed before the interface test because it is the cheaper answer to the
    // same question and because it is valid in both games' register state: at
    // the entry nothing has been pushed, so XOR EAX,EAX / RET returns 0 with
    // the stack untouched (neither game's InitializeDirectInputMouse takes a
    // stack argument). The JNE's displacement is back-patched once the failure
    // stub's position is known.
    *p++ = 0x80; *p++ = 0x3d;                   // CMP byte ptr [g_mouseUnavailable], 0
    const uintptr_t flagAddr =
        reinterpret_cast<uintptr_t>(&g_mouseUnavailable);
    memcpy(p, &flagAddr, 4); p += 4;
    *p++ = 0x00;
    *p++ = 0x75;                                // JNE -> latch stub
    uint8_t* latchRel8 = p++;                   // back-patched below

    if (k2) {
        // Test first, before any frame exists: at the entry ECX is still `this`
        // and nothing has been pushed, so the failure path is a bare
        // XOR EAX,EAX / RET. The function takes no stack arguments (its sole
        // caller pushes nothing and it ends in a plain RET), so that returns 0
        // to the caller with the stack exactly as it found it.
        *p++ = 0x8b; *p++ = 0x41; *p++ = 0x20;  // MOV EAX, [ECX+0x20]
        *p++ = 0x85; *p++ = 0xc0;               // TEST EAX, EAX
        *p++ = 0x74; *p++ = 0x0b;               // JZ +11 -> the XOR below
        memcpy(p, kInitMousePrologueK2, sizeof(kInitMousePrologueK2));
        p += sizeof(kInitMousePrologueK2);
        WriteJmpRel32(p, kAddrInitMouseContinue); p += 5;
        // Failure stub. Shared with the latch above: both arrive with the
        // entry's register state intact, so both want this exact answer.
        *latchRel8 = static_cast<uint8_t>(p - (latchRel8 + 1));
        *p++ = 0x33; *p++ = 0xc0;               // XOR EAX, EAX
        *p++ = 0xc3;                            // RET
    } else {
        // KOTOR 1 keeps the layout it has shipped with since v0.4.x, byte for
        // byte: run the displaced prologue (which is what puts `this` in EDI),
        // re-execute the interface load, then branch to the engine's own
        // continue point or its failure epilogue.
        memcpy(p, kInitMousePrologueK1, sizeof(kInitMousePrologueK1));
        p += sizeof(kInitMousePrologueK1);
        *p++ = 0x8b; *p++ = 0x47; *p++ = 0x1c;  // MOV EAX, [EDI+0x1c]
        *p++ = 0x85; *p++ = 0xc0;               // TEST EAX, EAX
        *p++ = 0x74; *p++ = 0x05;               // JZ +5 -> the fail JMP
        WriteJmpRel32(p, kAddrInitMouseContinue); p += 5;
        WriteJmpRel32(p, kAddrInitMouseFailEpilogue); p += 5;
        // The latch cannot share KOTOR 1's failure exit: that epilogue pops the
        // ESI/EDI the displaced prologue pushed, and the latch answers before
        // the prologue has run. Its own stub returns from the entry state.
        *latchRel8 = static_cast<uint8_t>(p - (latchRel8 + 1));
        *p++ = 0x33; *p++ = 0xc0;               // XOR EAX, EAX
        *p++ = 0xc3;                            // RET
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard install failed: VirtualProtect entry");
        return;
    }
    WriteJmpRel32(entry, reinterpret_cast<uintptr_t>(tramp));
    DWORD ignored = 0;
    VirtualProtect(entry, 5, oldProtect, &ignored);

    FlushInstructionCache(GetCurrentProcess(), entry, 5);
    FlushInstructionCache(GetCurrentProcess(), tramp, kTrampolineSize);

    acclog::Write("EngineInput",
        "DirectInput mouse guard installed (%s, InitializeDirectInputMouse "
        "@%p, trampoline @%p)", acc::game::TitleName(), entry, tramp);
}

namespace {

// Stand-in for CExoRawInputInternal::ShutDownDirectInput, reached only from the
// failure branches of InitializeDirectInputMouse (the engine's own shutdown
// path still calls the real thing). __fastcall with an unused second parameter
// is the exact stack contract of the __thiscall(void) it replaces: `this` in
// ECX, nothing pushed, nothing to clean.
//
// Doing nothing IS the fix. The branch that called us goes on to return the
// same value it always returned, and its caller — the per-frame mouse update —
// already treats that as "no mouse this frame". What no longer happens is the
// collateral: the keyboard device stays created and acquired.
void __fastcall MouseInitFailedInsteadOfTeardown(void* rawInput,
                                                 void* /*edx*/) {
    g_mouseUnavailable = 1;
    if (g_teardownLogged) return;
    g_teardownLogged = true;

    // Which failure branch called us. _ReturnAddress is the instruction after
    // the CALL we rewrote, which is exactly what the install loop recorded.
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    int site = -1;
    for (int i = 0; i < g_teardownSiteCount; ++i) {
        if (g_teardownSites[i] == ret) { site = i; break; }
    }

    void* iface = nullptr;
    void* keyboard = nullptr;
    void* mouse = nullptr;
    __try {
        auto* base = static_cast<uint8_t*>(rawInput);
        iface    = *reinterpret_cast<void**>(base + kOffRawInterface);
        keyboard = *reinterpret_cast<void**>(base + kOffRawKeyboard);
        mouse    = *reinterpret_cast<void**>(base + kOffRawMouse);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    acclog::Write("EngineInput",
        "MouseTeardownBlock: mouse bring-up failed at %s (site %d/%d, "
        "return=0x%08x); blocked the engine's ShutDownDirectInput "
        "(raw=%p interface=%p keyboard=%p mouse=%p). Vanilla would have "
        "released the KEYBOARD here and left it unrecoverable for the "
        "session; the mouse stays unavailable and the per-frame retry is now "
        "latched off",
        TeardownSiteName(site, g_teardownSiteCount), site + 1,
        g_teardownSiteCount, static_cast<unsigned>(ret),
        rawInput, iface, keyboard, mouse);
}

}  // namespace

void InstallMouseTeardownBlock() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    if (!acc::addr::Ok(kAddrInitMouse) ||
        !acc::addr::Ok(kAddrInitMouseEnd) ||
        !acc::addr::Ok(kAddrShutDownDirectInput)) {
        acclog::Write("EngineInput",
            "MouseTeardownBlock: no address for this build (%s); skipped",
            acc::game::BuildName());
        return;
    }

    auto* lo = reinterpret_cast<uint8_t*>(kAddrInitMouse);
    auto* hi = reinterpret_cast<uint8_t*>(kAddrInitMouseEnd);
    if (hi <= lo) return;
    const size_t span = static_cast<size_t>(hi - lo);

    DWORD oldProtect = 0;
    if (!VirtualProtect(lo, span, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        acclog::Write("EngineInput",
            "MouseTeardownBlock: VirtualProtect over "
            "InitializeDirectInputMouse failed; skipped");
        return;
    }

    const uintptr_t replacement =
        reinterpret_cast<uintptr_t>(&MouseInitFailedInsteadOfTeardown);
    int patched = 0;
    for (size_t i = 0; i + 5 <= span; ++i) {
        if (lo[i] != 0xe8) continue;  // CALL rel32
        int32_t rel = 0;
        memcpy(&rel, lo + i + 1, 4);
        const uintptr_t after = reinterpret_cast<uintptr_t>(lo + i + 5);
        if (after + static_cast<uintptr_t>(static_cast<intptr_t>(rel)) !=
            kAddrShutDownDirectInput) {
            continue;
        }
        // Range check for form's sake: a rel32 spans +-2 GB, the image sits at
        // 0x00400000 and no user-mode module base reaches 0x80000000, so the
        // displacement from engine code to our DLL cannot overflow. Checked
        // anyway, because the failure mode if it ever did would be a call into
        // nowhere on the one path nobody exercises.
        const intptr_t delta =
            static_cast<intptr_t>(replacement) - static_cast<intptr_t>(after);
        if (delta > 0x7ffffff0 || delta < -0x7ffffff0) {
            acclog::Write("EngineInput",
                "MouseTeardownBlock: handler is out of rel32 range of %p; "
                "site left alone", lo + i);
            continue;
        }
        const int32_t newRel = static_cast<int32_t>(delta);
        memcpy(lo + i + 1, &newRel, 4);
        if (patched < static_cast<int>(sizeof(g_teardownSites) /
                                       sizeof(g_teardownSites[0]))) {
            g_teardownSites[patched] = after;  // the handler's return address
        }
        ++patched;
        i += 4;  // skip the displacement we just wrote
    }

    DWORD ignored = 0;
    VirtualProtect(lo, span, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), lo, span);

    g_teardownSiteCount = patched;

    if (patched == 0) {
        acclog::Write("EngineInput",
            "MouseTeardownBlock: found no ShutDownDirectInput call inside "
            "InitializeDirectInputMouse (%p..%p) — nothing patched. Either the "
            "addresses are wrong for this build or the engine changed shape; "
            "a mouse failure can still cost this session its keyboard",
            lo, hi);
        return;
    }
    acclog::Write("EngineInput",
        "MouseTeardownBlock: installed (%s) — %d teardown call site(s) inside "
        "InitializeDirectInputMouse now answer us instead of releasing the "
        "keyboard",
        acc::game::TitleName(), patched);
}

bool MouseDeviceUnavailable() {
    return g_mouseUnavailable != 0;
}

namespace {

// Everything the probe reports, in one POD so the SEH reader can fill it and
// the (SEH-free) caller can act on it. Split for C2712: the caller speaks and
// formats, both of which want objects.
struct DiSnapshot {
    void*        raw;
    unsigned int active;
    void*        iface;
    void*        keyboard;
    void*        mouse;
};

bool ReadDiSnapshot(DiSnapshot* out) {
    __try {
        if (!acc::addr::Ok(kAddrExoInputGlobal)) return false;
        void* exoInput = *reinterpret_cast<void**>(kAddrExoInputGlobal);
        if (!exoInput) return false;
        void* internal = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(exoInput) + kOffInputInternal);
        if (!internal) return false;
        void* raw = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(internal) + kOffInternalRaw);
        if (!raw) return false;

        auto* base = static_cast<uint8_t*>(raw);
        out->raw      = raw;
        out->active   = *reinterpret_cast<unsigned int*>(base + kOffRawActive);
        out->iface    = *reinterpret_cast<void**>(base + kOffRawInterface);
        out->keyboard = *reinterpret_cast<void**>(base + kOffRawKeyboard);
        out->mouse    = *reinterpret_cast<void**>(base + kOffRawMouse);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DiSnapshot g_lastDi{};
bool       g_haveDi = false;
bool       g_keyboardLostWarned = false;

}  // namespace

void LogDirectInputStateIfChanged() {
    DiSnapshot now{};
    if (!ReadDiSnapshot(&now)) return;

    if (g_haveDi && now.raw == g_lastDi.raw && now.active == g_lastDi.active &&
        now.iface == g_lastDi.iface && now.keyboard == g_lastDi.keyboard &&
        now.mouse == g_lastDi.mouse) {
        return;
    }

    // A keyboard device that goes from present to NULL is the failure this
    // whole file is about. NULL-from-the-start is a different (and normal)
    // state — the engine has simply not built its devices yet — so it is
    // logged but never spoken.
    const bool keyboardLost =
        g_haveDi && g_lastDi.keyboard != nullptr && now.keyboard == nullptr;

    g_lastDi = now;
    g_haveDi = true;

    acclog::Write("EngineInput",
        "DirectInput state: raw=%p active=%u interface=%p keyboard=%p "
        "mouse=%p%s",
        now.raw, now.active, now.iface, now.keyboard, now.mouse,
        keyboardLost ? " [KEYBOARD DEVICE RELEASED]" : "");

    if (keyboardLost && !g_keyboardLostWarned) {
        g_keyboardLostWarned = true;
        acclog::Write("EngineInput",
            "DirectInput: the engine released its keyboard device — no key "
            "press can reach the game from here, and SetActive cannot "
            "re-acquire a device that no longer exists; warning the player");
        prism::SpeakUrgent(
            acc::strings::Get(acc::strings::Id::InputKeyboardLost));
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

// Last engine-delivered key PRESS, GetTickCount64 (0 = none yet). Written from
// the input hooks (engine thread), read by the tick — atomic so the read can
// never tear across the 64-bit value.
std::atomic<unsigned long long> g_lastEngineKeyMs{0};
}  // namespace

void NoteEngineKeyEvent() {
    g_lastEngineKeyMs.store(GetTickCount64(), std::memory_order_relaxed);
}

unsigned long long LastEngineKeyEventMs() {
    return g_lastEngineKeyMs.load(std::memory_order_relaxed);
}

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
