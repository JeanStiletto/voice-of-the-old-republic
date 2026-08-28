// Controller device census — see pad_diag.h for why this exists at all.

#include "pad_diag.h"

#include <cstdio>
#include <cstring>

#include "log.h"

namespace acc::pad::diag {

namespace {

// ---------------------------------------------------------------------------
// XInput
// ---------------------------------------------------------------------------
// Local mirrors, for the same reason pad_input keeps its own: the patch must
// build against any SDK arrangement and must never acquire a static import on
// a DLL that is allowed to be absent.
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
struct XiVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
};
struct XiCaps {
    BYTE        Type;
    BYTE        SubType;
    WORD        Flags;
    XiGamepad   Gamepad;
    XiVibration Vibration;
};
// XInputGetCapabilitiesEx — xinput1_4 only, and exported by ORDINAL 108 with
// no name and no header. It is the only way to get from an XInput slot to the
// USB ids behind it, which is what ties a slot number to a named device in the
// DirectInput half of the census below. Undocumented, so every call goes
// through TryCapsEx's SEH guard and a failure is simply less detail.
struct XiCapsEx {
    XiCaps Caps;
    WORD   VendorId;
    WORD   ProductId;
    WORD   ProductVersion;
    WORD   reserved0;
    DWORD  reserved1;
    DWORD  slack[8];   // room for anything a future runtime writes past the end
};

typedef DWORD(WINAPI* PFN_XiGetState)(DWORD, XiState*);
typedef DWORD(WINAPI* PFN_XiGetCaps)(DWORD, DWORD, XiCaps*);
typedef DWORD(WINAPI* PFN_XiGetCapsEx)(DWORD, DWORD, DWORD, XiCapsEx*);

struct XiProcs {
    PFN_XiGetState  get;
    PFN_XiGetCaps   caps;
    PFN_XiGetCapsEx capsEx;
};

XiProcs Resolve(HMODULE xinput) {
    XiProcs p = {};
    if (!xinput) return p;
    p.get = reinterpret_cast<PFN_XiGetState>(
        GetProcAddress(xinput, "XInputGetState"));
    p.caps = reinterpret_cast<PFN_XiGetCaps>(
        GetProcAddress(xinput, "XInputGetCapabilities"));
    p.capsEx = reinterpret_cast<PFN_XiGetCapsEx>(
        GetProcAddress(xinput, MAKEINTRESOURCEA(108)));
    return p;
}

// Its own function because of the SEH: an ordinal export with no documented
// signature is exactly the call that must not be able to take the game down,
// and MSVC forbids __try in a function holding anything that needs unwinding.
bool TryCapsEx(PFN_XiGetCapsEx fn, unsigned slot, XiCapsEx* out) {
    __try {
        return fn(1, slot, 0, out) == ERROR_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* SubTypeName(BYTE sub) {
    switch (sub) {
        case 0:  return "unknown";
        case 1:  return "gamepad";
        case 2:  return "wheel";
        case 3:  return "arcade stick";
        case 4:  return "flight stick";
        case 5:  return "dance pad";
        case 6:  return "guitar";
        case 7:  return "guitar alternate";
        case 8:  return "drum kit";
        case 11: return "guitar bass";
        case 19: return "arcade pad";
        default: return "other";
    }
}

// ---------------------------------------------------------------------------
// DirectInput
// ---------------------------------------------------------------------------
// Only the two vtable slots this needs are typed; the others are placeholders
// so the indices stay right. Same reasoning as the XInput mirrors — no SDK
// dependency, no static import.
// The WIDE variant deliberately. Device names are the whole payload of this
// half of the census, they come from the driver in Unicode, and DirectInput's
// ANSI variant reaches us only after converting them through the system code
// page — which both mangles anything outside it and lands raw high bytes in a
// log that is otherwise UTF-8. The machines this diagnostic exists for are
// disproportionately non-English, so the name has to survive intact: taken wide
// here and converted once, to UTF-8, at the point of logging.
struct DiDeviceInstanceW {
    DWORD dwSize;
    GUID  guidInstance;
    GUID  guidProduct;
    DWORD dwDevType;
    WCHAR tszInstanceName[MAX_PATH];
    WCHAR tszProductName[MAX_PATH];
    GUID  guidFFDriver;
    WORD  wUsagePage;
    WORD  wUsage;
};

typedef BOOL(CALLBACK* PFN_DiEnumCb)(const DiDeviceInstanceW*, void*);

struct DiObj;
struct DiVtbl {
    void* QueryInterface;
    void* AddRef;
    ULONG(STDMETHODCALLTYPE* Release)(DiObj*);
    void* CreateDevice;
    HRESULT(STDMETHODCALLTYPE* EnumDevices)(DiObj*, DWORD, PFN_DiEnumCb, void*,
                                            DWORD);
};
struct DiObj {
    DiVtbl* vtbl;
};

typedef HRESULT(WINAPI* PFN_DirectInput8Create)(HINSTANCE, DWORD, const GUID*,
                                                void**, void*);

// IID_IDirectInput8W. Its ANSI twin is the same GUID ending 0x30; the trailing
// 0x31 is the only thing that selects the wide interface, and getting it wrong
// hands back an object whose device-name fields are half the width we read.
const GUID kIidDirectInput8W = {
    0xBF798031, 0x483A, 0x4DA2,
    {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

constexpr DWORD kDirectInputVersion  = 0x0800;
constexpr DWORD kDi8DevClassGameCtrl = 4;
constexpr DWORD kDiEdflAttachedOnly  = 1;
constexpr DWORD kDiDevTypeHid        = 0x00010000;

const char* DevTypeName(DWORD devType) {
    switch (devType & 0xFF) {
        case 0x11: return "device";
        case 0x12: return "mouse";
        case 0x13: return "keyboard";
        case 0x14: return "joystick";
        case 0x15: return "gamepad";
        case 0x16: return "driving";
        case 0x17: return "flight";
        case 0x18: return "first-person";
        case 0x19: return "device control";
        case 0x1a: return "screen pointer";
        case 0x1b: return "remote";
        case 0x1c: return "supplemental";
        default:   return "unrecognised";
    }
}

// HID devices carry their USB ids in guidProduct: the low half of Data1 is the
// vendor, the high half the product, and the tail is the fixed "PIDVID" GUID
// that marks the encoding. A device whose tail says otherwise (a legacy
// gameport stick, a virtual device that made its own GUID up) reports no ids
// rather than a pair of misread numbers.
bool HidIdsFromProductGuid(const GUID& g, unsigned* vid, unsigned* pid) {
    static const BYTE kPidVid[6] = {'P', 'I', 'D', 'V', 'I', 'D'};
    if (g.Data2 != 0 || g.Data3 != 0) return false;
    if (g.Data4[0] != 0 || g.Data4[1] != 0) return false;
    if (memcmp(&g.Data4[2], kPidVid, sizeof(kPidVid)) != 0) return false;
    *vid = static_cast<unsigned>(g.Data1 & 0xFFFF);
    *pid = static_cast<unsigned>((g.Data1 >> 16) & 0xFFFF);
    return true;
}

struct EnumCtx {
    int count;
};

// The log is UTF-8 — every other line in it is — so a device name has to be
// converted, not poured in as whatever bytes the platform hands over. A name
// that will not convert is reported as a placeholder rather than dropped: which
// devices exist is the finding, and it survives even when what they are called
// does not.
void WideToUtf8(const wchar_t* in, char* out, int outCap) {
    out[0] = '\0';
    if (!in || !*in) return;
    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, out, outCap, nullptr,
                            nullptr) <= 0) {
        _snprintf_s(out, outCap, _TRUNCATE, "(name not convertible)");
    }
}

BOOL CALLBACK OnDiDevice(const DiDeviceInstanceW* di, void* ref) {
    auto* ctx = static_cast<EnumCtx*>(ref);
    char ids[64];
    ids[0] = '\0';
    unsigned vid = 0;
    unsigned pid = 0;
    if (HidIdsFromProductGuid(di->guidProduct, &vid, &pid)) {
        _snprintf_s(ids, sizeof(ids), _TRUNCATE, " vid=0x%04X pid=0x%04X", vid,
                    pid);
    }
    // Worst-case UTF-8 expansion of a MAX_PATH name, so no name can ever be
    // truncated mid-sequence and leave a half-encoded character in the log.
    char product[MAX_PATH * 4];
    char instance[MAX_PATH * 4];
    WideToUtf8(di->tszProductName, product, sizeof(product));
    WideToUtf8(di->tszInstanceName, instance, sizeof(instance));

    acclog::Write("Pad.Devices",
                  "  DirectInput [%d]: product=\"%s\" instance=\"%s\" type=%s%s%s",
                  ctx->count, product, instance, DevTypeName(di->dwDevType),
                  (di->dwDevType & kDiDevTypeHid) ? " (HID)" : "", ids);
    ++ctx->count;
    return TRUE;   // DIENUM_CONTINUE
}

// Returns the controller count, or -1 when the enumeration could not be run at
// all — which is NOT the same answer as zero and must never be read as one.
int LogDirectInputControllers() {
    // Whatever dinput8 the process already has, which on both games is the
    // mod's own loader proxy forwarding to the system one. Deliberate: the
    // point of this half of the census is to see what KOTOR 2's engine saw when
    // it ran the identical enumeration at input init, so it has to go through
    // the identical module.
    HMODULE di = GetModuleHandleW(L"dinput8.dll");
    if (!di) di = LoadLibraryW(L"dinput8.dll");
    if (!di) {
        acclog::Write("Pad.Devices", "  DirectInput: dinput8.dll not present — "
                                     "game controllers not enumerable");
        return -1;
    }
    auto create = reinterpret_cast<PFN_DirectInput8Create>(
        GetProcAddress(di, "DirectInput8Create"));
    if (!create) {
        acclog::Write("Pad.Devices", "  DirectInput: dinput8.dll exports no "
                                     "DirectInput8Create");
        return -1;
    }

    DiObj* obj = nullptr;
    HRESULT hr = create(GetModuleHandleW(nullptr), kDirectInputVersion,
                        &kIidDirectInput8W, reinterpret_cast<void**>(&obj),
                        nullptr);
    if (FAILED(hr) || !obj) {
        acclog::Write("Pad.Devices",
                      "  DirectInput: DirectInput8Create failed (hr=0x%08lX)",
                      static_cast<unsigned long>(hr));
        return -1;
    }

    EnumCtx ctx = {0};
    hr = obj->vtbl->EnumDevices(obj, kDi8DevClassGameCtrl, &OnDiDevice, &ctx,
                                kDiEdflAttachedOnly);
    const int count = ctx.count;
    obj->vtbl->Release(obj);
    if (FAILED(hr)) {
        acclog::Write("Pad.Devices",
                      "  DirectInput: EnumDevices failed (hr=0x%08lX) after %d "
                      "device(s)",
                      static_cast<unsigned long>(hr), count);
        return -1;
    }
    if (count == 0) {
        acclog::Write("Pad.Devices",
                      "  DirectInput: no game controllers attached");
    }
    return count;
}

// ---------------------------------------------------------------------------
// The census itself
// ---------------------------------------------------------------------------
// `valveSlots` counts the occupied slots whose vendor id is Valve's, which
// LogReading needs and nothing else here does. Out-param rather than a second
// pass: the ids are already in hand at the only point they can be read.
int LogXInputSlots(const XiProcs& p, int* valveSlots) {
    int found = 0;
    *valveSlots = 0;
    for (unsigned i = 0; i < 4; ++i) {
        XiState st = {};
        if (!p.get || p.get(i, &st) != ERROR_SUCCESS) {
            acclog::Write("Pad.Devices", "  XInput slot %u: empty", i);
            continue;
        }
        ++found;

        char capsText[128];
        capsText[0] = '\0';
        XiCaps c = {};
        if (p.caps && p.caps(i, 0, &c) == ERROR_SUCCESS) {
            _snprintf_s(capsText, sizeof(capsText), _TRUNCATE,
                        " type=%u subtype=%u (%s) capflags=0x%04X",
                        static_cast<unsigned>(c.Type),
                        static_cast<unsigned>(c.SubType), SubTypeName(c.SubType),
                        static_cast<unsigned>(c.Flags));
        }

        char idText[64];
        idText[0] = '\0';
        XiCapsEx cx = {};
        if (p.capsEx && TryCapsEx(p.capsEx, i, &cx)) {
            if (cx.VendorId == kVendorValve) ++*valveSlots;
            _snprintf_s(idText, sizeof(idText), _TRUNCATE,
                        " vid=0x%04X pid=0x%04X rev=0x%04X",
                        static_cast<unsigned>(cx.VendorId),
                        static_cast<unsigned>(cx.ProductId),
                        static_cast<unsigned>(cx.ProductVersion));
        }

        // The live sample goes in the line too. A device that is present but
        // reports a permanently centred, all-zero state on every census is the
        // fingerprint of a virtual pad, and a device holding a button or a
        // pulled trigger at session start explains a mod binding that seems to
        // fire on its own.
        acclog::Write("Pad.Devices",
                      "  XInput slot %u: present%s%s packet=%lu buttons=0x%04X "
                      "LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d",
                      i, capsText, idText,
                      static_cast<unsigned long>(st.dwPacketNumber),
                      static_cast<unsigned>(st.Gamepad.wButtons),
                      static_cast<unsigned>(st.Gamepad.bLeftTrigger),
                      static_cast<unsigned>(st.Gamepad.bRightTrigger),
                      static_cast<int>(st.Gamepad.sThumbLX),
                      static_cast<int>(st.Gamepad.sThumbLY),
                      static_cast<int>(st.Gamepad.sThumbRX),
                      static_cast<int>(st.Gamepad.sThumbRY));
    }
    return found;
}

// What the two counts together MEAN, in the words the reader of a tester's log
// needs. Kept here rather than in a doc because the log is what gets sent to
// us, and a count nobody can interpret is not a diagnosis.
void LogReading(int xiCount, int diCount, int valveSlots) {
    if (xiCount == 0 && diCount == 0) {
        acclog::Write("Pad.Devices",
                      "READING: no controller is visible to EITHER API. On a "
                      "handheld (ROG Ally and friends) that is the signature of "
                      "the built-in controller sitting in desktop / "
                      "keyboard-and-mouse emulation mode: its sticks and buttons "
                      "arrive as keystrokes and mouse motion, so there is no "
                      "gamepad for anything to bind to. Switching the device to "
                      "its gamepad (XInput) mode is the fix — no amount of mod "
                      "code can substitute for it.");
        return;
    }
    if (xiCount == 0 && diCount > 0) {
        acclog::Write("Pad.Devices",
                      "READING: DirectInput sees %d controller(s) but XInput "
                      "sees none — this pad speaks DirectInput only. KOTOR 1 "
                      "cannot use it at all, because the mod is the pad driver "
                      "there and drives it through XInput. On KOTOR 2 the "
                      "engine's own bindings will work and the mod's trigger "
                      "layer will not.",
                      diCount);
        return;
    }
    if (xiCount == 0) {
        acclog::Write("Pad.Devices",
                      "READING: XInput sees no controller, and DirectInput could "
                      "not be enumerated to say whether one exists at all.");
        return;
    }
    // Ahead of the multi-device reading and without returning: both can be
    // true at once, and when they are, the Steam layer is the cause and the
    // device count is the symptom.
    if (valveSlots > 0) {
        acclog::Write("Pad.Devices",
                      "READING: %d XInput slot(s) report vendor 0x%04X, Valve — "
                      "these are STEAM INPUT's emulated pads, not the "
                      "controller in the player's hands. Steam re-presents the "
                      "physical pad through a layout, so what reaches the game "
                      "is whatever that layout emits: a stick bound to buttons "
                      "or to the D-pad arrives with no analog axes at all, and "
                      "the pad can be enumerated twice, once per virtual "
                      "device, which delivers every press twice and leaves the "
                      "mod's slot choice a coin flip. Turning Steam Input off "
                      "for this title (Steam, game Properties, Controller, "
                      "\"Disable Steam Input\") hands the game the real pad and "
                      "is the fix — the mod cannot see past the layer.",
                      valveSlots, kVendorValve);
    }
    if (xiCount > 1) {
        acclog::Write("Pad.Devices",
                      "READING: %d XInput devices are present. The mod reads ONE "
                      "slot at a time, so if the slot it chose is a virtual or "
                      "vendor-layer controller rather than the pad in the "
                      "player's hands, every binding looks dead while the log "
                      "looks healthy. A later \"switching to slot\" line is the "
                      "idle-slot guard correcting that choice; its absence "
                      "alongside silence from the pad is itself the finding.",
                      xiCount);
    }
}

unsigned long long Fingerprint(const XiProcs& p) {
    // 0 is reserved for "no census has run yet", so the walk starts at 1 and a
    // machine with no controller at all still fingerprints to something.
    unsigned long long fp = 1;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned long long slot = 0;
        XiState st = {};
        if (p.get && p.get(i, &st) == ERROR_SUCCESS) {
            slot = 1;
            XiCaps c = {};
            if (p.caps && p.caps(i, 0, &c) == ERROR_SUCCESS) {
                slot |= (static_cast<unsigned long long>(c.SubType) + 1) << 1;
            }
            XiCapsEx cx = {};
            if (p.capsEx && TryCapsEx(p.capsEx, i, &cx)) {
                slot ^= static_cast<unsigned long long>(cx.VendorId) << 16;
                slot ^= static_cast<unsigned long long>(cx.ProductId) << 32;
            }
        }
        fp = fp * 1000003ull + slot;
    }
    return fp;
}

unsigned long long g_lastFingerprint = 0;

}  // namespace

int SlotSubType(HMODULE xinput, unsigned slot) {
    if (!xinput || slot > 3) return -1;
    auto caps = reinterpret_cast<PFN_XiGetCaps>(
        GetProcAddress(xinput, "XInputGetCapabilities"));
    if (!caps) return -1;
    XiCaps c = {};
    if (caps(slot, 0, &c) != ERROR_SUCCESS) return -1;
    return static_cast<int>(c.SubType);
}

bool SlotVendorIds(HMODULE xinput, unsigned slot, unsigned* vid,
                   unsigned* pid) {
    if (!xinput || slot > 3 || !vid || !pid) return false;
    auto capsEx = reinterpret_cast<PFN_XiGetCapsEx>(
        GetProcAddress(xinput, MAKEINTRESOURCEA(108)));
    if (!capsEx) return false;
    XiCapsEx cx = {};
    if (!TryCapsEx(capsEx, slot, &cx)) return false;
    *vid = static_cast<unsigned>(cx.VendorId);
    *pid = static_cast<unsigned>(cx.ProductId);
    return true;
}

void LogDeviceCensus(HMODULE xinput, const wchar_t* xinputName,
                     const char* why) {
    const XiProcs p = Resolve(xinput);
    g_lastFingerprint = Fingerprint(p);

    acclog::Write("Pad.Devices", "controller census — %s", why ? why : "");
    if (!xinput) {
        acclog::Write("Pad.Devices",
                      "  XInput: no runtime resolved — the mod cannot read a "
                      "controller through XInput at all");
    } else {
        acclog::Write("Pad.Devices",
                      "  XInput runtime: %ls (capabilities:%s, vendor ids:%s)",
                      xinputName ? xinputName : L"(unnamed)",
                      p.caps ? " yes" : " no", p.capsEx ? " yes" : " no");
    }

    int valveSlots    = 0;
    const int xiCount = xinput ? LogXInputSlots(p, &valveSlots) : 0;
    const int diCount = LogDirectInputControllers();

    if (diCount >= 0) {
        acclog::Write("Pad.Devices",
                      "census: %d XInput device(s), %d DirectInput controller(s)",
                      xiCount, diCount);
    } else {
        acclog::Write("Pad.Devices",
                      "census: %d XInput device(s), DirectInput not enumerable",
                      xiCount);
    }
    LogReading(xiCount, diCount, valveSlots);
}

void LogDeviceCensusIfChanged(HMODULE xinput, const wchar_t* xinputName,
                              const char* why) {
    const XiProcs p = Resolve(xinput);
    if (Fingerprint(p) == g_lastFingerprint) return;
    LogDeviceCensus(xinput, xinputName, why);
}

}  // namespace acc::pad::diag
