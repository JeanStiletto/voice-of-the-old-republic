// Keyboard transition-stream re-synchronisation — see the header for the
// contract and gui-and-input-internals.md ("keyboard_transition_stream") for
// the engine model. Decompiled 2026-09-14 on both games.
//
// The engine side, in one paragraph. CExoInputInternal::GetEvents runs once
// per frame from ProcessInput. It calls CExoRawInputInternal::
// GetKeyboardBuffer, which allocates a 256-entry batch and fills it with
// IDirectInputDevice8::GetDeviceData (DIDEVICEOBJECTDATA, 0x14 bytes each:
// dwOfs = DIK scancode, dwData = 0x80 for down / 0 for up, dwTimeStamp,
// dwSequence, uAppData). GetEvents then walks the batch in sequence order and,
// for every transition that matches a binding in the active input class,
// assigns the transition's level to that binding's flag. PollInput reads the
// flag; that is the turn axis, the walk axis, everything. Transitions that
// match no binding are skipped. Nothing re-reads the device.
//
// Where releases go missing (both games, same code shape):
//   * CExoRawInputInternal::SetActive 0->1 drains the DirectInput buffer
//     (ClearKeyboardBuffer) without touching the flags. The engine drives that
//     edge on window activate/deactivate, fullscreen toggle, area transitions,
//     load screens, movies, dialogue start and end, the pause popup and the
//     quit popup. KOTOR 2 logs prove it: the pause key's own release is lost
//     on essentially every press (Diag.ClientHIE 218 down, never up).
//   * GetKeyboardBuffer's own error path: DIERR_INPUTLOST / DIERR_NOTACQUIRED
//     make it re-Acquire and return an EMPTY batch; whatever queued in the gap
//     is gone. Foreground cooperative level, so any foreground blip does it.
//   * Input-class switches also drain the buffer, but through ClearEvents,
//     which zeroes every flag first — those release keys rather than strand
//     them, and are not our concern.
//
// Why a wrapper on the fetch, and not a hook on each discard site: the discard
// sites are many, one of them (the DirectInput-level loss) is invisible from
// outside, and the fix wanted is the same for all of them — make the engine's
// view of the keyboard converge on the device's. Doing it where the engine
// itself ingests transitions means the correction travels the engine's own
// path (sequence ordering, class matching, event emission) and no other code
// changes. KOTOR 2's joystick reader already works this way: it derives its
// transitions by diffing the device state each frame.
//
// Safety rules, in priority order:
//   1. Never synthesise a press. A missed press is the safe failure (the key
//      does nothing until pressed again); a synthesised one could fire an
//      edge-triggered command twice if the real transition follows.
//   2. Synthesise a release only after two consecutive frames of disagreement
//      with no transition for that key in this frame or the previous one. A
//      real release that lands between the engine's buffer read and our state
//      read shows up in the next batch and cancels the synthesis, so a
//      duplicate is impossible; only a transition DirectInput itself never
//      delivered survives the wait.
//   3. Gate on the reader's own gates (active, keyboard mode, device present).
//      While the device is unacquired the true state is unknown; nothing is
//      appended, and the next acquired frame catches up.
//   4. Any fault disables the resync for the session; the wrapper keeps
//      forwarding to the engine's reader regardless.
//
// C2712: everything under __try is POD and file-scope; no objects, no
// function-local statics.

#include "engine_keyboard_resync.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_game.h"
#include "engine_input.h"     // raw_input:: field layout
#include "engine_rebase.h"
#include "log.h"

namespace acc::engine {

namespace {

// CExoRawInputInternal::GetKeyboardBuffer. KOTOR 1 is Lane's name; KOTOR 2
// located 2026-09-14 by its 0x1400 batch allocation (FindScalar) and
// decompiled: same gates, same 256-entry GetDeviceData, same error handling,
// keyboard device at +0x24.
const uintptr_t kAddrGetKeyboardBuffer =
    acc::addr::Pick(0x005e3ae0, 0x00732c40);

// The CALL to it inside CExoInputInternal::GetEvents — the ONLY per-frame
// consumer (GetLastCapturedKeyboardKey reads the same buffer, but only inside
// the key-mapping options screen, where GetEvents skips the keyboard). Both
// sites are direct CALL rel32 (KOTOR 2 bytes e8 b5 62 00 00, verified from
// the Ghidra project); the installer re-verifies before rewriting.
const uintptr_t kAddrGetEventsKeyboardCall =
    acc::addr::Pick(0x005e25fe, 0x0072c986);

// CExoDeviceBuffer (Lane's struct, size 0xc; GetEvents strides it by 0xc on
// both games): the batch pointer, the entry count, and a field the reader
// never sets.
struct DeviceBuffer {
    uint32_t (*entries)[5];
    int       size;
    int       reserved;
};

typedef int (__thiscall* PFN_GetKeyboardBuffer)(void* rawInput,
                                                DeviceBuffer* buf);

// IDirectInputDevice8::GetDeviceState — vtable slot 9 (+0x24), between the
// Acquire (+0x1c) / Unacquire (+0x20) / GetDeviceData (+0x28) slots the
// engine's reader uses, so the same vtable the engine trusts.
constexpr size_t kVtGetDeviceState = 0x24;
typedef HRESULT (__stdcall* PFN_GetDeviceState)(void* device, DWORD cb,
                                                void* data);

constexpr int kKeys          = 256;   // c_dfDIKeyboard: one byte per DIK
constexpr int kBatchCapacity = 256;   // 0x1400 / 0x14
constexpr int kConfirmFrames = 2;     // rule 2 above

// The engine's view, rebuilt from every transition that passed through.
uint8_t  g_shadowDown[kKeys];
// Frame of the last transition seen for each key — the "settling" test.
uint32_t g_lastTransitionFrame[kKeys];
// Consecutive frames the two views disagreed (capped).
uint8_t  g_disagreeFrames[kKeys];
// One line per lost-press episode.
uint8_t  g_pressLostLogged[kKeys];
uint32_t g_frame        = 0;
void*    g_lastDevice   = nullptr;
uint32_t g_releasesSynthesised = 0;
volatile int g_disabled = 0;
int      g_installed    = 0;

void ResetShadow() {
    memset(g_shadowDown,          0, sizeof(g_shadowDown));
    memset(g_lastTransitionFrame, 0, sizeof(g_lastTransitionFrame));
    memset(g_disagreeFrames,      0, sizeof(g_disagreeFrames));
    memset(g_pressLostLogged,     0, sizeof(g_pressLostLogged));
}

// Everything after the engine's reader returned. SEH-guarded; POD only.
void Resync(void* rawInput, DeviceBuffer* buf, int readerRc) {
    __try {
        ++g_frame;

        auto* base = static_cast<uint8_t*>(rawInput);
        const unsigned active =
            *reinterpret_cast<unsigned*>(base + raw_input::kOffActive);
        const int keyboardMode =
            *reinterpret_cast<int*>(base + raw_input::kOffKeyboardMode);
        void* device =
            *reinterpret_cast<void**>(base + raw_input::kOffKeyboard);

        if (device != g_lastDevice) {
            // A recreated device starts a new stream; a stale shadow would
            // synthesise releases for keys the new stream never reported.
            ResetShadow();
            g_lastDevice = device;
            acclog::Write("KeyboardResync",
                "keyboard device is now %p — shadow reset", device);
        }
        // The reader's own gates. Keyboard mode (editbox / console) means it
        // did not read DirectInput at all; the transitions queue up and arrive
        // when the mode ends, so the shadow must not move either.
        if (!active || keyboardMode != 0 || !device) return;

        // Feed the batch into the shadow. readerRc == 0 means no batch (alloc
        // failure or a gate we already tested); entries may be NULL then.
        uint32_t maxSeq = 0;
        const bool haveBatch =
            readerRc != 0 && buf && buf->entries && buf->size > 0;
        if (haveBatch) {
            const int n = buf->size < kBatchCapacity ? buf->size
                                                     : kBatchCapacity;
            for (int i = 0; i < n; ++i) {
                const uint32_t ofs  = buf->entries[i][0];
                const uint32_t data = buf->entries[i][1];
                const uint32_t seq  = buf->entries[i][3];
                if (seq > maxSeq) maxSeq = seq;
                if (ofs >= static_cast<uint32_t>(kKeys)) continue;
                g_shadowDown[ofs]          = (data & 0x80) ? 1 : 0;
                g_lastTransitionFrame[ofs] = g_frame;
                g_disagreeFrames[ofs]      = 0;
                g_pressLostLogged[ofs]     = 0;
            }
        }

        // The truth. Not acquired here is exactly the reader's silent
        // empty-batch path, so the edge is worth a line: it dates the gaps
        // the rest of the log cannot see.
        uint8_t state[kKeys];
        void** vtable = *reinterpret_cast<void***>(device);
        auto getState = reinterpret_cast<PFN_GetDeviceState>(
            vtable[kVtGetDeviceState / sizeof(void*)]);
        const HRESULT hr = getState(device, kKeys, state);
        acclog::Edge("KeyboardResync.Device", hr == 0 ? 1 : 0,
            hr == 0 ? "keyboard device acquired (GetDeviceState ok)"
                    : "keyboard device NOT acquired (GetDeviceState hr=0x%08x) "
                      "— the engine's reader answers empty batches here and "
                      "every transition in the gap is lost",
            static_cast<unsigned>(hr));
        if (hr != 0) return;

        for (int k = 0; k < kKeys; ++k) {
            const bool trueDown   = (state[k] & 0x80) != 0;
            const bool shadowDown = g_shadowDown[k] != 0;
            if (trueDown == shadowDown) {
                g_disagreeFrames[k] = 0;
                continue;
            }
            // A transition for this key in this frame or the previous one:
            // the stream is still delivering, let it settle (rule 2).
            if (g_lastTransitionFrame[k] + 1 >= g_frame) {
                g_disagreeFrames[k] = 0;
                continue;
            }
            if (g_disagreeFrames[k] < 250) ++g_disagreeFrames[k];
            if (g_disagreeFrames[k] < kConfirmFrames) continue;

            if (!shadowDown) {
                // Lost press (rule 1: left alone). Logged once per episode —
                // the next transition for the key clears the latch.
                if (!g_pressLostLogged[k]) {
                    g_pressLostLogged[k] = 1;
                    acclog::Write("KeyboardResync",
                        "press of DIK 0x%02x reached DirectInput but never "
                        "the engine (down for %u frames, no transition) — "
                        "left alone; a missed press is the safe failure",
                        k, static_cast<unsigned>(g_disagreeFrames[k]));
                }
                continue;
            }

            // Stranded release. Append it so the engine's own consumer
            // processes it this frame.
            if (readerRc == 0 || !buf || !buf->entries) {
                acclog::Once("KeyboardResync",
                    "DIK 0x%02x is stranded but this frame has no batch to "
                    "append to (rc=%d) — retrying next frame", k, readerRc);
                continue;
            }
            if (buf->size >= kBatchCapacity) {
                acclog::Once("KeyboardResync",
                    "DIK 0x%02x is stranded but the batch is full (%d) — "
                    "retrying next frame", k, buf->size);
                continue;
            }
            uint32_t* e = buf->entries[buf->size];
            e[0] = static_cast<uint32_t>(k);   // dwOfs: the scancode
            e[1] = 0;                          // dwData: released
            e[2] = GetTickCount();             // dwTimeStamp (DI uses ms ticks)
            e[3] = maxSeq + 1;                 // dwSequence: after the real ones
            e[4] = 0;                          // uAppData
            ++maxSeq;
            buf->size += 1;
            g_shadowDown[k]          = 0;
            g_disagreeFrames[k]      = 0;
            g_lastTransitionFrame[k] = g_frame;
            ++g_releasesSynthesised;
            acclog::Write("KeyboardResync",
                "synthesised release for DIK 0x%02x — the engine held it "
                "down with no transition for %d frames while DirectInput "
                "reports it up (batch now %d entries; %u synthesised this "
                "session)",
                k, kConfirmFrames, buf->size, g_releasesSynthesised);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = 1;
        acclog::Write("KeyboardResync",
            "fault while re-synchronising — disabled for the session; the "
            "engine's reader is still called unchanged");
    }
}

// Stand-in for the CALL inside GetEvents. __fastcall with an unused EDX is the
// stack contract of the __thiscall(CExoDeviceBuffer*) it replaces: `this` in
// ECX, one stack argument, callee cleans it. Same trick as
// MouseInitFailedInsteadOfTeardown in engine_input.cpp.
int __fastcall GetKeyboardBufferResynced(void* rawInput, void* /*edx*/,
                                         DeviceBuffer* buf) {
    auto original =
        reinterpret_cast<PFN_GetKeyboardBuffer>(kAddrGetKeyboardBuffer);
    const int rc = original(rawInput, buf);
    if (!g_disabled) Resync(rawInput, buf, rc);
    return rc;
}

}  // namespace

void InstallKeyboardStreamResync() {
    if (g_installed) return;
    g_installed = 1;

    if (!acc::addr::Ok(kAddrGetKeyboardBuffer) ||
        !acc::addr::Ok(kAddrGetEventsKeyboardCall)) {
        acclog::Write("KeyboardResync",
            "no address for this build (%s); not installed — stranded keys "
            "stay stranded until pressed again", acc::addr::ActiveBuildName());
        return;
    }

    auto* site = reinterpret_cast<uint8_t*>(kAddrGetEventsKeyboardCall);
    if (site[0] != 0xe8) {
        acclog::Write("KeyboardResync",
            "call site %p is not a CALL rel32 (byte %02x); not installed — "
            "the address is wrong for this build", site, site[0]);
        return;
    }
    int32_t rel = 0;
    memcpy(&rel, site + 1, 4);
    const uintptr_t after = reinterpret_cast<uintptr_t>(site) + 5;
    if (after + static_cast<uintptr_t>(static_cast<intptr_t>(rel)) !=
        kAddrGetKeyboardBuffer) {
        acclog::Write("KeyboardResync",
            "call site %p does not call GetKeyboardBuffer (rel32 resolves to "
            "%p, expected %p); not installed",
            site,
            reinterpret_cast<void*>(
                after + static_cast<uintptr_t>(static_cast<intptr_t>(rel))),
            reinterpret_cast<void*>(kAddrGetKeyboardBuffer));
        return;
    }

    const uintptr_t replacement =
        reinterpret_cast<uintptr_t>(&GetKeyboardBufferResynced);
    const intptr_t delta =
        static_cast<intptr_t>(replacement) - static_cast<intptr_t>(after);
    if (delta > 0x7ffffff0 || delta < -0x7ffffff0) {
        acclog::Write("KeyboardResync",
            "wrapper is out of rel32 range of %p; not installed", site);
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        acclog::Write("KeyboardResync",
            "VirtualProtect over the call site failed; not installed");
        return;
    }
    const int32_t newRel = static_cast<int32_t>(delta);
    memcpy(site + 1, &newRel, 4);
    DWORD ignored = 0;
    VirtualProtect(site, 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), site, 5);

    ResetShadow();
    acclog::Write("KeyboardResync",
        "installed (%s) — GetEvents' keyboard fetch at %p now returns through "
        "the re-sync wrapper; a release the engine loses is re-delivered "
        "after %d frames",
        acc::game::TitleName(), site, kConfirmFrames);
}

}  // namespace acc::engine
