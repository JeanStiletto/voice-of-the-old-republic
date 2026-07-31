// area map surface: fog-of-war, map notes and map pins.
//
// Split out of engine_area.cpp by the Phase-1 structure pass (refactoring
// candidate 3). engine_area.cpp kept the core object model (iteration,
// handle resolution, naming/tagging, door/waypoint/trigger state); this
// file owns everything that talks to CSWSAreaMap / CSWCArea's map-pin
// list, plus the two module-scope readers (ReadGlobalNumber,
// IsLoadingSaveGame) that only the map paths use.
//
// Declarations stay in engine_area.h - splitting that header is a separate,
// deferred item, so every existing includer is unaffected.

#include "engine_area.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine_app.h"     // GetServerApp
#include "engine_player.h"
#include "engine_reads.h"
#include "log.h"
#include "map_note_renames.h"
#include "engine_rebase.h"

namespace acc::engine {

bool IsMapNoteEnabled(void* waypoint) {
    if (!waypoint) return false;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(waypoint) +
            kWaypointMapNoteEnabledOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool EnableMapNote(void* waypoint) {
    if (!waypoint) return false;
    __try {
        unsigned char* base = reinterpret_cast<unsigned char*>(waypoint);
        // Same guard the engine action applies before its write: only
        // waypoints authored with a map note carry the enabled field.
        if (*reinterpret_cast<int*>(base + kWaypointHasMapNoteOffset) == 0) {
            return false;
        }
        *reinterpret_cast<int*>(base + kWaypointMapNoteEnabledOffset) = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize) {
    if (!waypoint || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    // Mirrors ExtractTextOrStrRef's order with one insertion: embedded
    // note text (mods author it directly) always wins; strref-resolved
    // notes first consult the curated rename table — vanilla's cryptic
    // corridor labels ("S\xFCdlicher Pfad") speak their transition
    // destination instead (see map_note_renames) — then fall back to the
    // vanilla tlk text.
    //
    // The two raw reads need their own SEH frame. ReadCExoString / ReadU32
    // deliberately carry none — callers supply it — and this caller did not.
    // On KOTOR 2 kWaypointMapNoteLocOffset is still Todo, so it resolves to
    // acc::off::kUnportedOffset and both reads land far outside the mapping:
    // an unrecoverable fault rather than the graceful miss the rest of the
    // engine-reading code degrades to. Found by tools/re-scripts/
    // handler_chain_audit.py while clearing the KOTOR 2 handler gates.
    uint32_t strref = 0;
    __try {
        if (ReadCExoString(waypoint, kWaypointMapNoteLocOffset,
                           outBuf, bufSize)) {
            return outBuf[0] != '\0';
        }
        strref = ReadU32(waypoint, kWaypointMapNoteLocOffset + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    Vector notePos;
    if (GetObjectPosition(waypoint, notePos) &&
        acc::map_note_renames::Override(strref, notePos, outBuf, bufSize)) {
        return true;
    }
    return LookupTlk(strref, outBuf, bufSize) && outBuf[0] != '\0';
}

namespace {

typedef void* (__thiscall* PFN_CServerExoApp_GetModule)(void* /*serverApp*/);
typedef bool  (__thiscall* PFN_CSWSAreaMap_IsWorldPointExplored)(
    void* /*areaMap*/, Vector /*pos*/);
// MSVC compiles `long double` returns through ST(0) (same register the
// engine pushes its float10 result into), then converts on the receiving
// side. Treating the return as `double` is equivalent at the binary
// level — the FPU push lands in ST(0); the compiler emits an fstp to
// the receiving stack slot. The 80→64-bit precision loss is well below
// the ~1-degree announcement granularity.
typedef double (__thiscall* PFN_CSWSAreaMap_GetMapRotateCCW)(
    void* /*areaMap*/, Vector /*orientation*/);

}  // namespace

void* GetAreaMap() {
    void* serverApp = GetServerApp();
    if (!serverApp) return nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_CServerExoApp_GetModule>(
            kAddrCServerExoAppGetModule);
        void* module = fn(serverApp);
        if (!module) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(module) + kModuleAreaMapOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetCurrentAreaResName(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    void* serverApp = GetServerApp();
    if (!serverApp) return false;
    __try {
        auto getMod = reinterpret_cast<PFN_CServerExoApp_GetModule>(
            kAddrCServerExoAppGetModule);
        void* module = getMod(serverApp);
        if (!module) return false;
        // CSWSModule::GetModuleResourceName @0x004c4b80 returns a CExoString by
        // VALUE: hidden out-buffer is the first stack arg, this=module in ECX
        // (verified: it copy-constructs into the out buffer, so a zero-init
        // {c_string,len} is safe). We deliberately leak the engine-allocated
        // c_string — this runs once per area change (matches the engine_reads
        // leak convention; freeing across the DLL/CRT boundary is riskier).
        const uintptr_t kAddrGetModuleResourceName = acc::addr::R(0x004c4b80);
        struct { char* p; int len; } out = {nullptr, 0};
        using PFN = void* (__thiscall*)(void*, void*);
        reinterpret_cast<PFN>(kAddrGetModuleResourceName)(module, &out);
        return ReadCExoString(&out, 0, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

int ReadGlobalNumber(const char* name) {
    if (!name || !name[0]) return -1;
    void* serverApp = GetServerApp();
    if (!serverApp) return -1;
    __try {
        // table = CServerExoApp::GetGlobalVariableTable(server)
        const uintptr_t kAddrGetGlobalVarTable = acc::addr::R(0x004aee60);
        const uintptr_t kAddrGlobalVarGetNumber = acc::addr::R(0x00529240);
        using PFN_GetTable = void* (__thiscall*)(void*);
        void* table = reinterpret_cast<PFN_GetTable>(kAddrGetGlobalVarTable)(serverApp);
        if (!table) return -1;
        // GetValueNumber(table, &CExoString{name,len}, &out) — writes low byte.
        struct { const char* p; uint32_t len; } nameStr{
            name, static_cast<uint32_t>(std::strlen(name)) };
        int value = 0;
        using PFN_GetNum = void (__thiscall*)(void*, void*, int*);
        reinterpret_cast<PFN_GetNum>(kAddrGlobalVarGetNumber)(table, &nameStr, &value);
        return value & 0xff;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool IsLoadingSaveGame() {
    void* serverApp = GetServerApp();  // CServerExoApp facade (AppManager+0x8)
    if (!serverApp) return false;
    __try {
        // CServerExoApp::GetLoadFromSaveGame(this) — returns
        // this->internal->load_from_savegame (decompile-verified). The getter
        // does the facade→internal deref itself, so we pass the facade.
        //
        // KOTOR 2: 0x0051CDE0, found by aligning the two games' server-facade
        // getter-thunk clusters — both read the SAME internal field +0x100C4,
        // and the alignment is anchored by the already-verified
        // GetObjectArray pair (K1 0x004AED70 / K2 0x0051C080, both +0x1005C).
        const uintptr_t kAddrGetLoadFromSaveGame =
            acc::addr::Pick(0x004af050, 0x0051CDE0);
        if (!acc::addr::Ok(kAddrGetLoadFromSaveGame)) return false;
        using PFN = int(__thiscall*)(void*);
        return reinterpret_cast<PFN>(kAddrGetLoadFromSaveGame)(serverApp) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsWorldPointExplored(void* areaMap, const Vector& pos) {
    if (!areaMap) return false;
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaMap_IsWorldPointExplored>(
            kAddrCSWSAreaMapIsWorldPointExplored);
        return fn(areaMap, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetFogCellSizeM(void* areaMap, float& outCellXM, float& outCellYM) {
    if (!areaMap) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(areaMap);
        int resX = *reinterpret_cast<int*>(base + kAreaMapResXOffset);
        int resY = *reinterpret_cast<int*>(base + kAreaMapResYOffset);
        float wppx =
            *reinterpret_cast<float*>(base + kAreaMapWorldPerPxXOffset);
        float wppy =
            *reinterpret_cast<float*>(base + kAreaMapWorldPerPxYOffset);
        if (resX <= 0 || resY <= 0) return false;
        // Calibration axes can run negative (map north vs world axes);
        // cell extent is the magnitude.
        float cellX = std::fabs(wppx) * (440.0f / static_cast<float>(resX));
        float cellY = std::fabs(wppy) * (256.0f / static_cast<float>(resY));
        if (cellX <= 0.0f || cellY <= 0.0f) return false;
        outCellXM = cellX;
        outCellYM = cellY;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetMapRotateCCWFromWorldOrientation(void* areaMap,
                                         const Vector& orientation,
                                         float& outDegCCW) {
    if (!areaMap) return false;
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaMap_GetMapRotateCCW>(
            kAddrCSWSAreaMapGetMapRotateCCW);
        double deg = fn(areaMap, orientation);
        outDegCCW = static_cast<float>(deg);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetClientArea(void* serverArea) {
    if (!serverArea) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(serverArea) +
            kAreaClientAreaBackOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int GetMapPinCount(void* clientArea) {
    if (!clientArea) return 0;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(clientArea) +
            kClientAreaMapPinsCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* GetMapPinAt(void* clientArea, int i) {
    if (!clientArea || i < 0) return nullptr;
    __try {
        void** pinArray = *reinterpret_cast<void***>(
            reinterpret_cast<unsigned char*>(clientArea) +
            kClientAreaMapPinsOffset);
        if (!pinArray) return nullptr;
        return pinArray[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetMapPinPosition(void* mapPin, Vector& out) {
    if (!mapPin) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(mapPin) + kMapPinPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


bool IsMapPinEnabled(void* mapPin) {
    if (!mapPin) return false;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(mapPin) + kMapPinEnabledOffset)
            != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetMapPinNoteText(void* mapPin, char* outBuf, size_t bufSize) {
    if (!mapPin || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    // CExoString at +0x100; inline c_string first, falling back to the
    // strref at the same CExoString's +0x04 slot via TLK lookup. Map pins
    // served over the wire from the server carry inline text; quest-
    // script-created pins may carry strref-only. Strref-resolved pins get
    // the same curated rename pass as GetWaypointMapNote so both readers
    // of one note always speak one label.
    //
    // SEH for the same reason as GetWaypointMapNote above: these two offsets
    // are still Todo for KOTOR 2, and the read helpers carry no guard of their
    // own.
    uint32_t strref = 0;
    __try {
        if (ReadCExoString(mapPin, kMapPinNoteTextOffset, outBuf, bufSize)) {
            return outBuf[0] != '\0';
        }
        strref = ReadU32(mapPin, kMapPinNoteStrrefOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    Vector notePos;
    if (GetMapPinPosition(mapPin, notePos) &&
        acc::map_note_renames::Override(strref, notePos, outBuf, bufSize)) {
        return true;
    }
    return LookupTlk(strref, outBuf, bufSize) && outBuf[0] != '\0';
}

namespace {

typedef void* (__cdecl*    PFN_OperatorNew)(size_t /*size*/);
typedef void* (__thiscall* PFN_CSWCMapPinCtor)(void* /*this*/);
typedef void* (__thiscall* PFN_CExoStringAssignFromCString)(
    void* /*this CExoString*/, const char* /*rhs*/);
typedef void  (__thiscall* PFN_CSWCAreaAddMapPin)(void* /*this CSWCArea*/,
                                                  void* /*pin*/);

}  // namespace

bool CreateMapPin(void* clientArea, const Vector& pos, const char* name,
                  uint32_t referenceNumber, void** outPin) {
    if (outPin) *outPin = nullptr;
    if (!clientArea) return false;

    void* pin = nullptr;
    __try {
        // 1) operator_new(0x110)
        auto opNew = reinterpret_cast<PFN_OperatorNew>(kAddrOperatorNew);
        pin = opNew(0x110);
        if (!pin) return false;

        // 2) CSWCMapPin::CSWCMapPin() — initialises vtable + zero-fields.
        auto ctor = reinterpret_cast<PFN_CSWCMapPinCtor>(kAddrCSWCMapPinCtor);
        ctor(pin);

        // 3) Direct field writes — match the engine's own pattern in
        //    HandleServerToPlayerMapPinReferenceNumber. Position via
        //    direct write (the engine uses a vtable[35] setter that
        //    ultimately writes +0x24; bypassing it is safe because the
        //    pin isn't yet attached to the area and no other thread
        //    races us).
        auto* p = reinterpret_cast<unsigned char*>(pin);
        *reinterpret_cast<Vector*>(p + kMapPinPositionOffset) = pos;
        *reinterpret_cast<int*>  (p + kMapPinEnabledOffset)   = 1;
        *reinterpret_cast<uint32_t*>(p + kMapPinFlagsOffset)  = referenceNumber;
        *reinterpret_cast<int*>  (p + kMapPinSubtypeOffset)   = 1;

        // note_text CExoString — operator=(char*) handles the heap
        // allocation + strcpy. Pass our string in; the engine owns the
        // copy from this point on.
        if (name && name[0] != '\0') {
            auto exoAssign =
                reinterpret_cast<PFN_CExoStringAssignFromCString>(
                    kAddrCExoStringAssignFromCString);
            exoAssign(p + kMapPinNoteTextOffset, name);
        }

        // 4) Append to clientArea->map_pins[].
        auto add = reinterpret_cast<PFN_CSWCAreaAddMapPin>(
            kAddrCSWCAreaAddMapPin);
        add(clientArea, pin);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Pin was alloc'd but engine path faulted before AddMapPin —
        // leaking 0x110 bytes is preferable to mismatched delete.
        if (outPin) *outPin = nullptr;
        return false;
    }
    if (outPin) *outPin = pin;
    return true;
}

}  // namespace acc::engine
