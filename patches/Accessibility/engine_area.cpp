#include "engine_area.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_player.h"  // GetPlayerArea, kAddrAppManagerPtr
#include "engine_reads.h"   // ExtractTextOrStrRef, ReadCExoString
#include "log.h"            // seam-filter telemetry
#include "strings.h"        // door state suffix lookup (DoorOpen/DoorLocked)

namespace acc::engine {

namespace {

typedef void* (__thiscall* PFN_CSWSAreaGetRoom)(void* this_,
                                                Vector* pos,
                                                int* outRoomIndex);
typedef void* (__thiscall* PFN_GetObjectArray)(void* this_);
typedef bool  (__thiscall* PFN_GetGameObject)(void* this_,
                                              uint32_t id,
                                              void** out);

// Walk *kAddrAppManagerPtr → AppManager + 0x8 → CServerExoApp* →
// GetObjectArray() → CGameObjectArray*. SEH-guarded for the same reason as
// the rest of engine_*: the chain may not be populated during engine
// teardown / very early init.
void* GetServerObjectArray() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffset);
        if (!serverApp) return nullptr;
        auto fn = reinterpret_cast<PFN_GetObjectArray>(
            kAddrCServerExoAppGetObjectArray);
        return fn(serverApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void* GetCurrentArea() {
    return GetPlayerArea();  // already SEH-guarded inside engine_player
}

int GetObjectKind(void* gameObject) {
    if (!gameObject) return -1;
    __try {
        return static_cast<int>(*reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(gameObject) + kObjectKindOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

uint32_t GetObjectHandle(void* gameObject) {
    if (!gameObject) return 0;
    __try {
        // CGameObject.id @+0x4 (per /KotOR Types/Other Classes/CGameObject
        // size 0xc, members vtable@0x0 / id@0x4 / object_type@0x8). Same
        // ulong namespace AreaObjectIterator yields and ResolveServerObjectHandle
        // accepts.
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(gameObject) + 0x4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

namespace {

// Engine sentinels shared by both resolve paths: 0 (uninitialised),
// 0xFFFFFFFF (removed), 0x7F000000 (kInvalidObjectId — the "no object"
// marker the action queue and LastTarget use).
bool IsSentinelHandle(uint32_t handle) {
    return handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u;
}

}  // namespace

void* ResolveServerObjectHandle(uint32_t handle) {
    if (IsSentinelHandle(handle)) return nullptr;

    void* objectArray = GetServerObjectArray();
    if (!objectArray) return nullptr;

    // CGameObjectArray::GetGameObject returns *false on hit, true on miss*
    // — same inverted-bool convention as AreaObjectIterator::Next. See the
    // comment at line ~225 of this file for the decompilation evidence.
    auto resolve = reinterpret_cast<PFN_GetGameObject>(
        kAddrCGameObjectArrayGetGameObject);
    void* out = nullptr;
    bool miss = true;
    __try {
        miss = resolve(objectArray, handle, &out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return (!miss && out) ? out : nullptr;
}

namespace {

// CClientExoApp::GetGameObject — direct one-call resolver. Returns
// CSWCObject* (client side); caller chains through +0xf8 to reach the
// matching server CSWSObject* the rest of engine_area expects.
typedef void* (__thiscall* PFN_CClientGetGameObject)(void* this_,
                                                    uint32_t handle);

void* GetClientExoApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void* ResolveClientObjectHandle(uint32_t handle) {
    if (IsSentinelHandle(handle)) return nullptr;

    void* clientApp = GetClientExoApp();
    if (!clientApp) return nullptr;

    void* clientObject = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_CClientGetGameObject>(
            kAddrCClientExoAppGetGameObject);
        clientObject = fn(clientApp, handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!clientObject) return nullptr;

    // CSWCObject.server_object @+0xf8 → CSWSObject*. Same offset
    // engine_player uses on the player creature's CSWCCreature.
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientObject) +
            kClientObjectServerObjectOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool GetObjectPosition(void* gameObject, Vector& out) {
    if (!gameObject) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(gameObject) +
            kServerObjectPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetRoomAt(void* area, const Vector& pos) {
    if (!area) return nullptr;
    Vector local = pos;  // GetRoom takes Vector* — pass a writable address
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaGetRoom>(kAddrCSWSAreaGetRoom);
        return fn(area, &local, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex) {
    outIndex = -1;
    if (!area) return nullptr;
    Vector local = pos;
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaGetRoom>(kAddrCSWSAreaGetRoom);
        return fn(area, &local, &outIndex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outIndex = -1;
        return nullptr;
    }
}

bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld) {
    if (!area || roomIdx < 0) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        int roomCount = static_cast<int>(*reinterpret_cast<uint32_t*>(
            base + kAreaRoomCountOffset));
        if (roomIdx >= roomCount) return false;

        // kAreaRoomsOffset holds a POINTER to the inline-stride rooms
        // buffer, not the rooms themselves. (Header comment was
        // misleading — see BuildAreaWallCache for the canonical access
        // pattern.)
        void* rooms = *reinterpret_cast<void**>(base + kAreaRoomsOffset);
        if (!rooms) return false;
        auto* roomBase = reinterpret_cast<unsigned char*>(rooms);
        void* room = roomBase +
                     static_cast<size_t>(roomIdx) * kRoomStride;
        void* mesh = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(room) + kRoomSurfaceMeshOffset);
        if (!mesh) return false;

        auto* meshBytes = reinterpret_cast<unsigned char*>(mesh);
        Vector* vertices = *reinterpret_cast<Vector**>(
            meshBytes + kCollisionMeshVerticesOffset);
        uint32_t faceCount = *reinterpret_cast<uint32_t*>(
            meshBytes + kCollisionMeshFaceCountOffset);
        void* faceIndices = *reinterpret_cast<void**>(
            meshBytes + kCollisionMeshFacesOffset);
        if (!vertices || !faceIndices || faceCount == 0) return false;

        // Middle face — less likely to sit on the room boundary than
        // face 0 (which often corresponds to a corner triangle in
        // walkmesh authoring tools).
        uint32_t f = faceCount / 2;
        auto* face = reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(faceIndices) +
            static_cast<size_t>(f) * kWalkmeshFaceStride);
        uint32_t v0 = face[0], v1 = face[1], v2 = face[2];
        Vector a = vertices[v0];
        Vector b = vertices[v1];
        Vector c = vertices[v2];
        Vector localCentroid;
        localCentroid.x = (a.x + b.x + c.x) / 3.0f;
        localCentroid.y = (a.y + b.y + c.y) / 3.0f;
        localCentroid.z = (a.z + b.z + c.z) / 3.0f;

        Vector worldCentroid = localCentroid;
        // The anonymous-namespace PFN_CollisionMeshLocalToWorld lives
        // later in this TU; redeclare locally rather than reorder.
        typedef void (__thiscall* PFN_L2W)(void* this_,
                                           Vector* output,
                                           Vector* localPoint);
        auto fnL2W = reinterpret_cast<PFN_L2W>(
            kAddrCollisionMeshLocalToWorld);
        __try {
            fnL2W(mesh, &worldCentroid, &localCentroid);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Best-effort: fall back to the local copy (correct when
            // world_coords=1, common runtime case for room walkmeshes).
            worldCentroid = localCentroid;
        }
        outWorld = worldCentroid;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize) {
    if (!area || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    // Localized name first: CExoLocString shape matches CExoString — try
    // the inline c_string at +0x150, fall back to TLK strref at +0x154.
    if (ExtractTextOrStrRef(area, kAreaNameLocOffset,
                            kAreaNameLocOffset + 4, outBuf, bufSize) &&
        outBuf[0] != '\0') {
        return true;
    }
    // Modder-assigned tag (CExoString at +0x158) — better "tar_m02ac" than
    // empty silence per feedback_never_silence_fallback_announcement.
    __try {
        return ReadCExoString(area, kAreaTagOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetRoomDisplayName(void* area, int roomIndex,
                        char* outBuf, size_t bufSize) {
    if (!area || !outBuf || bufSize < 2 || roomIndex < 0) return false;
    outBuf[0] = '\0';
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        int roomCount = static_cast<int>(*reinterpret_cast<uint32_t*>(
            base + kAreaRoomCountOffset));
        if (roomIndex >= roomCount) return false;
        void* namesArray = *reinterpret_cast<void**>(
            base + kAreaRoomNamesOffset);
        if (!namesArray) return false;
        size_t entryOffset =
            static_cast<size_t>(roomIndex) * kCExoStringStride;
        return ReadCExoString(namesArray, entryOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

AreaObjectIterator::AreaObjectIterator(void* area)
    : handles_(nullptr), size_(0), index_(0), objectArray_(nullptr) {
    if (!area) return;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        handles_ = *reinterpret_cast<uint32_t**>(base + kAreaGameObjectsOffset);
        size_    = *reinterpret_cast<int*>     (base + kAreaGameObjectCountOffset);
        if (size_ < 0) size_ = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        handles_ = nullptr;
        size_    = 0;
    }
    objectArray_ = GetServerObjectArray();
    if (!objectArray_) {
        // Without the resolver, every Next() would fail anyway — short-circuit.
        size_ = 0;
    }
}

namespace {

// Resolve a CExoLocString by treating its 8 bytes as the same shape as
// CExoString (char* + uint32 length); fall back to TLK strref at +4. Both
// reads are SEH-guarded by the engine_reads helpers.
bool TryReadLocString(void* base, size_t locStringOffset,
                      char* outBuf, size_t bufSize) {
    return ExtractTextOrStrRef(base, locStringOffset,
                               locStringOffset + 4, outBuf, bufSize);
}

// Last-resort: speak the modder-assigned tag instead of an empty name.
// Better "g_dnt_carth" than nothing.
bool TryReadTag(void* obj, char* outBuf, size_t bufSize) {
    __try {
        return ReadCExoString(obj, kObjectTagOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Append ", <text>" to outBuf if there's room. No-op when text is empty
// or outBuf is already at capacity.
void AppendCommaSeparated(char* outBuf, size_t bufSize, const char* text) {
    if (!outBuf || bufSize < 3 || !text || !text[0]) return;
    size_t curLen = 0;
    while (curLen < bufSize && outBuf[curLen]) ++curLen;
    if (curLen >= bufSize - 1) return;
    size_t remaining = bufSize - curLen - 1;
    if (remaining < 3) return;  // no room for ", x" minimum
    outBuf[curLen++] = ',';
    outBuf[curLen++] = ' ';
    remaining -= 2;
    for (size_t i = 0; i < remaining && text[i]; ++i) {
        outBuf[curLen++] = text[i];
    }
    outBuf[curLen] = '\0';
}

// Build the comma-prefixed suffix for a CSWSDoor — state + transition
// destination + description. Empty when the door is in the boring default
// state (closed + unlocked) AND has no transition target / description.
//
// Order is deliberate: state first ("Tür, verriegelt") because that's the
// most actionable bit for the player; destination second ("Tür, offen,
// Brücke") so the user hears it before any long-form description; then
// description last.
void BuildDoorSuffix(void* serverDoor, char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return;
    outBuf[0] = '\0';
    if (!serverDoor) return;

    uint32_t locked    = 0;
    uint8_t  openState = 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(serverDoor);
        locked    = *reinterpret_cast<uint32_t*>(base + kDoorLockedOffset);
        openState = *reinterpret_cast<uint8_t*> (base + kDoorOpenStateOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // leave defaults; suffix will skip the state line
    }

    if (locked != 0) {
        AppendCommaSeparated(outBuf, bufSize,
            acc::strings::Get(acc::strings::Id::DoorLocked));
    } else if (openState != 0) {
        // Treat any non-zero open_state as "open enough to mention".
        // Stock KOTOR uses 0=closed, ≥1=opening/open in our observations
        // (CSWCDoor.state=2 was the post-open value seen in the May 5
        // logs); the server-side byte mirrors that. If the value space
        // turns out wider, the worst case is we read "offen" for an
        // animating door — better signal than silent.
        AppendCommaSeparated(outBuf, bufSize,
            acc::strings::Get(acc::strings::Id::DoorOpen));
    }

    char buf[128];
    if (TryReadLocString(serverDoor, kDoorTransitionDestOffset,
                         buf, sizeof(buf)) && buf[0]) {
        AppendCommaSeparated(outBuf, bufSize, buf);
    }
    if (TryReadLocString(serverDoor, kDoorDescriptionOffset,
                         buf, sizeof(buf)) && buf[0]) {
        AppendCommaSeparated(outBuf, bufSize, buf);
    }
}

}  // namespace

// Inner: one engine call with the exact handle we got. Returns true
// on a non-empty resolved name. Empty result and engine-side faults
// fold into a single false return so the outer can retry.
static bool TryResolveDisplayNameOnce(void* clientApp, uint32_t handle,
                                      char* outBuf, size_t bufSize) {
    typedef int (__thiscall* PFN_GetObjectName)(void* this_, uint32_t handle,
                                                CExoString* outStr);
    CExoString out{nullptr, 0};
    __try {
        auto fn = reinterpret_cast<PFN_GetObjectName>(
            kAddrCClientExoAppGetObjectName);
        fn(clientApp, handle, &out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!out.c_string || out.length == 0 || out.length >= bufSize) {
        return false;
    }
    __try {
        memcpy(outBuf, out.c_string, out.length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
    outBuf[out.length] = '\0';
    return outBuf[0] != '\0';
}

bool GetObjectDisplayNameByHandle(uint32_t handle,
                                  char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        return false;
    }

    void* clientApp = nullptr;
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return false;
        clientApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!clientApp) return false;

    // Try the handle as-is first. Then, if that fails AND the handle
    // looked server-side (high bit clear), retry with the high bit
    // set. CClientExoApp::GetObjectName resolves only the client-side
    // namespace (verified 2026-05-10 in patch-20260510-003647.log:
    // 0x8000002c → "Sith-Soldat" succeeded; 0x0000002c → empty/tag
    // fallback). Server-side IDs with high bit cleared share their
    // low 24 bits with the matching client handle per
    // memory:project_object_handle_namespaces, so OR'ing 0x80000000
    // is the namespace-cross conversion.
    if (TryResolveDisplayNameOnce(clientApp, handle, outBuf, bufSize)) {
        return true;
    }
    if ((handle & 0x80000000u) == 0u) {
        outBuf[0] = '\0';
        if (TryResolveDisplayNameOnce(clientApp,
                                      handle | 0x80000000u,
                                      outBuf, bufSize)) {
            return true;
        }
    }
    outBuf[0] = '\0';
    return false;
}

bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize) {
    if (!gameObject || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    int kind = GetObjectKind(gameObject);
    if (kind < 0) return false;

    using K = GameObjectKind;
    bool got = false;
    switch (K(kind)) {
        case K::Door:
            got = TryReadLocString(gameObject, kDoorLocNameOffset,
                                   outBuf, bufSize);
            // Enrich with state ("verriegelt"/"offen") + transition
            // destination + description. All three are silent on the
            // common case (closed unlocked in-area door) so cycle
            // narration stays terse; locked/open doors and module
            // transitions get a meaningful suffix so the user can tell
            // them apart without inspecting tags.
            if (got && outBuf[0] != '\0') {
                BuildDoorSuffix(gameObject, outBuf + std::strlen(outBuf),
                                bufSize - std::strlen(outBuf));
            }
            break;
        case K::Creature: {
            __try {
                void* stats = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(gameObject) +
                    kCreatureStatsPtrOffset);
                if (stats) {
                    got = TryReadLocString(
                        stats, kCreatureStatsFirstNameOffset,
                        outBuf, bufSize);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                got = false;
            }
            break;
        }
        case K::Placeable:
            got = TryReadLocString(gameObject, kPlaceableLocNameOffset,
                                   outBuf, bufSize);
            break;
        case K::Item:
            got = TryReadLocString(gameObject, kItemLocNameOffset,
                                   outBuf, bufSize);
            break;
        case K::Waypoint:
            got = TryReadLocString(gameObject, kWaypointLocNameOffset,
                                   outBuf, bufSize);
            break;
        case K::Trigger:
            got = TryReadLocString(gameObject, kTriggerLocNameOffset,
                                   outBuf, bufSize);
            break;
        default:
            return false;
    }

    if (got && outBuf[0] != '\0') return true;
    return TryReadTag(gameObject, outBuf, bufSize);
}

bool IsUsablePlaceable(void* placeable) {
    if (!placeable) return false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(placeable);
        uint8_t usable       = *(base + kPlaceableUsableOffset);
        uint8_t hasInventory = *(base + kPlaceableHasInventoryOffset);
        return usable != 0 || hasInventory != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsLandmarkWaypoint(void* waypoint) {
    if (!waypoint) return false;
    __try {
        return *(reinterpret_cast<unsigned char*>(waypoint) +
                 kWaypointHasMapNoteOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsTransitionTrigger(void* trigger) {
    if (!trigger) return false;
    __try {
        Vector dest = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(trigger) +
            kTriggerTransitionDestOffset);
        return dest.x != 0.0f || dest.y != 0.0f || dest.z != 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize) {
    if (!waypoint || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    bool ok = ExtractTextOrStrRef(waypoint,
                                  kWaypointMapNoteLocOffset,
                                  kWaypointMapNoteLocOffset + 4,
                                  outBuf, bufSize);
    return ok && outBuf[0] != '\0';
}

namespace {

typedef void (__thiscall* PFN_CollisionMeshLocalToWorld)(void* this_,
                                                         Vector* output,
                                                         Vector* localPoint);

// Read a CSWSRoom's surface_mesh pointer. Returns nullptr if the room slot
// itself is null/garbage or surface_mesh hasn't been populated. SEH-guarded.
void* GetRoomSurfaceMesh(void* room) {
    if (!room) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(room) + kRoomSurfaceMeshOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Walk every triangle of one room's surface mesh, emit a WallEdge for each
// adjacency==-1 side. Returns the number of edges this room contributed (to
// the running total — written to outBuf only while there's space).
int ScanRoomWallEdges(void* surfaceMesh, int roomId,
                      WallEdge* outBuf, int maxEdges, int alreadyWritten) {
    if (!surfaceMesh) return 0;
    auto* mesh = reinterpret_cast<unsigned char*>(surfaceMesh);

    Vector*   vertices     = nullptr;
    uint32_t  faceCount    = 0;
    void*     faceIndices  = nullptr;
    uint32_t* materials    = nullptr;
    int*      adjacencies  = nullptr;   // flat int[faceCount*3]

    __try {
        vertices = *reinterpret_cast<Vector**>(
            mesh + kCollisionMeshVerticesOffset);
        faceCount = *reinterpret_cast<uint32_t*>(
            mesh + kCollisionMeshFaceCountOffset);
        faceIndices = *reinterpret_cast<void**>(
            mesh + kCollisionMeshFacesOffset);
        materials = *reinterpret_cast<uint32_t**>(
            mesh + kCollisionMeshMaterialsOffset);
        // SurfaceMeshAdjacency lives on the wrapping CSWRoomSurfaceMesh,
        // not on the embedded CSWCollisionMesh — offset is +0x88 from the
        // surface_mesh base (which IS the wrapper). Each entry is
        // `int indices[3]` (12 bytes); we treat the whole thing as a flat
        // int array indexed via f*3 + e to avoid a local struct typedef.
        adjacencies = *reinterpret_cast<int**>(
            mesh + kSurfaceMeshAdjacenciesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    if (!vertices || !faceIndices || !adjacencies || faceCount == 0) {
        return 0;
    }

    auto fnLocalToWorld = reinterpret_cast<PFN_CollisionMeshLocalToWorld>(
        kAddrCollisionMeshLocalToWorld);

    int emitted = 0;
    auto* faces = reinterpret_cast<unsigned char*>(faceIndices);

    for (uint32_t f = 0; f < faceCount; ++f) {
        // Per-face vertex indices (3 × ulong).
        uint32_t v[3] = {0, 0, 0};
        int      adj[3] = {0, 0, 0};
        bool     readOk = true;
        __try {
            auto* face = reinterpret_cast<uint32_t*>(
                faces + static_cast<size_t>(f) * kWalkmeshFaceStride);
            v[0] = face[0]; v[1] = face[1]; v[2] = face[2];
            adj[0] = adjacencies[f * 3 + 0];
            adj[1] = adjacencies[f * 3 + 1];
            adj[2] = adjacencies[f * 3 + 2];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            readOk = false;
        }
        if (!readOk) continue;

        // surfacemat.2da row for this face — captured once per face (all
        // three potential edges share the same material).
        int materialId = -1;
        __try {
            materialId = static_cast<int>(materials[f]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            materialId = -1;
        }

        for (int e = 0; e < 3; ++e) {
            if (adj[e] != -1) continue;  // interior edge — has a neighbour
            // Edge endpoints — face-vertex order, wrap on the third side.
            uint32_t va = v[e];
            uint32_t vb = v[(e + 1) % 3];

            Vector localA = {0, 0, 0}, localB = {0, 0, 0};
            __try {
                localA = vertices[va];
                localB = vertices[vb];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            Vector worldA = localA, worldB = localB;
            __try {
                fnLocalToWorld(surfaceMesh, &worldA, &localA);
                fnLocalToWorld(surfaceMesh, &worldB, &localB);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Best-effort: fall back to the local copies (correct only
                // when world_coords=1, which is the common runtime case for
                // room walkmeshes anyway).
                worldA = localA;
                worldB = localB;
            }

            int slot = alreadyWritten + emitted;
            if (outBuf && slot < maxEdges) {
                outBuf[slot].a           = worldA;
                outBuf[slot].b           = worldB;
                outBuf[slot].room_id     = roomId;
                outBuf[slot].material_id = materialId;
            }
            ++emitted;
        }
    }
    return emitted;
}

}  // namespace

int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges) {
    if (!area) return 0;

    void*    rooms     = nullptr;
    uint32_t roomCount = 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        rooms     = *reinterpret_cast<void**>(base + kAreaRoomsOffset);
        roomCount = *reinterpret_cast<uint32_t*>(base + kAreaRoomCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!rooms || roomCount == 0) return 0;

    auto* roomBase = reinterpret_cast<unsigned char*>(rooms);
    int total = 0;
    for (uint32_t i = 0; i < roomCount; ++i) {
        void* room = roomBase + static_cast<size_t>(i) * kRoomStride;
        void* sm   = GetRoomSurfaceMesh(room);
        if (!sm) continue;
        int contributed = ScanRoomWallEdges(sm, static_cast<int>(i),
                                            outBuf, maxEdges, total);
        total += contributed;
    }

    // Count-only probe — no buffer to filter. Return pre-filter total so
    // callers can detect "would have overflowed the buffer" telemetry.
    if (!outBuf || maxEdges <= 0) return total;

    // Seam filtering. KOTOR's walkmesh joins rooms via portals / AABB
    // structures, NOT via per-triangle adjacency. When room A and room B
    // share a walkable boundary, both rooms' meshes mark their side of
    // the shared edge with adjacency=-1, so the per-room scan above
    // emits the same world edge twice — once from each room — and both
    // copies look like perimeter walls. The engine treats those edges
    // as walkable through the portal mechanism; we have to too, or
    // Pillar 1 narrates phantom walls, Pillar 2 collision blocks the
    // virtual cursor on them, and Pillar 3 path-smoothing refuses to
    // skip nav nodes through them (the case that motivated this fix —
    // confirmed via Pathfind smoothing log 2026-05-12, Endar Spire
    // edge #342 emitted from room 9 blocking diagonals that the engine
    // itself routes nav-graph connections through).
    //
    // Detection: pair every emitted edge with every other one in a
    // different room. If endpoints match within a small epsilon (either
    // direction), mark both as seams. Then compact the buffer in-place,
    // dropping seam-marked entries.
    //
    // Cost: O(N²) for N ≤ maxEdges. Observed N ≈ 500 → 125k cheap
    // comparisons; runs once per area-load. Negligible.
    int written = (total < maxEdges) ? total : maxEdges;

    // Endpoint match tolerance. LocalToWorld involves matrix math, so a
    // pair of "identical" edges from two different rooms may not be
    // bit-equal — allow ~1cm of slack per coordinate. Squared so we can
    // compare against squared distance and avoid a sqrt.
    constexpr float kSeamEpsSq = 1e-4f;  // ~1cm² in world units
    auto coincident = [&](const Vector& p, const Vector& q) {
        float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        return (dx * dx + dy * dy + dz * dz) < kSeamEpsSq;
    };

    // Seam-flag scratch. Sized for the largest plausible per-area edge
    // count we've observed (K1 areas top out around 500). Static so we
    // don't burden the stack at the BuildAreaWallCache scope; single-
    // threaded patch ⇒ no reentrancy concern.
    constexpr int kMaxSeamFlags = 8192;
    static bool s_isSeam[kMaxSeamFlags];
    int flagN = (written < kMaxSeamFlags) ? written : kMaxSeamFlags;
    for (int i = 0; i < flagN; ++i) s_isSeam[i] = false;

    int seamPairs = 0;
    for (int i = 0; i < flagN; ++i) {
        for (int j = i + 1; j < flagN; ++j) {
            if (outBuf[i].room_id == outBuf[j].room_id) continue;
            bool match =
                (coincident(outBuf[i].a, outBuf[j].a) &&
                 coincident(outBuf[i].b, outBuf[j].b)) ||
                (coincident(outBuf[i].a, outBuf[j].b) &&
                 coincident(outBuf[i].b, outBuf[j].a));
            if (match) {
                s_isSeam[i] = true;
                s_isSeam[j] = true;
                ++seamPairs;
                // Continue scanning — at 3+ room corners (rare but
                // possible), one edge may match multiple counterparts;
                // we want every copy of the shared boundary marked.
            }
        }
    }

    int kept = 0;
    for (int i = 0; i < flagN; ++i) {
        if (s_isSeam[i]) continue;
        if (kept != i) outBuf[kept] = outBuf[i];
        ++kept;
    }
    // Tail beyond kMaxSeamFlags (only possible if maxEdges > kMaxSeamFlags
    // AND the area has that many edges) — unfilterable, append unchanged.
    // Pathological; we log if it ever happens.
    if (written > flagN) {
        int tail = written - flagN;
        for (int i = 0; i < tail; ++i) {
            outBuf[kept + i] = outBuf[flagN + i];
        }
        kept += tail;
        acclog::Write("AreaWalls",
            "seam filter exceeded flag buffer (written=%d flagN=%d) — "
            "%d trailing edges unfiltered",
            written, flagN, tail);
    }

    acclog::Write("AreaWalls",
        "seam filter: emitted=%d -> kept=%d (dropped %d via %d seam pairs)",
        written, kept, written - kept, seamPairs);

    return kept;
}

bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint) {
    if (!walls || wallCount <= 0) return false;

    // Movement direction in 2D. abx/aby form the player segment; if both
    // are ~0 the cursor isn't actually moving and there's nothing to
    // test (avoids the divide-by-zero in the parametric formula below).
    float abx = b.x - a.x;
    float aby = b.y - a.y;
    if (abx * abx + aby * aby < 1e-10f) return false;

    bool   anyHit       = false;
    float  bestT        = 1e30f;
    Vector bestHit      = a;

    for (int i = 0; i < wallCount; ++i) {
        const WallEdge& w = walls[i];
        float cdx = w.b.x - w.a.x;
        float cdy = w.b.y - w.a.y;

        // Standard 2D segment-segment intersection in the XY plane.
        // Solve for parametric t (along a→b) and u (along w.a→w.b):
        //   a + t * (b - a) == w.a + u * (w.b - w.a)
        // → denom = abx*cdy - aby*cdx (≈ 0 means the segments are
        //   parallel; treat as no hit — sliding-along is fine for our
        //   cursor scale).
        float denom = abx * cdy - aby * cdx;
        if (denom > -1e-8f && denom < 1e-8f) continue;

        float dx = w.a.x - a.x;
        float dy = w.a.y - a.y;
        float t  = (dx * cdy - dy * cdx) / denom;
        float u  = (dx * aby - dy * abx) / denom;

        if (t < 0.0f || t > 1.0f) continue;
        if (u < 0.0f || u > 1.0f) continue;

        if (t < bestT) {
            bestT      = t;
            bestHit.x  = a.x + t * abx;
            bestHit.y  = a.y + t * aby;
            bestHit.z  = a.z + t * (b.z - a.z);
            anyHit     = true;
        }
    }

    if (anyHit) outHitPoint = bestHit;
    return anyHit;
}

void* AreaObjectIterator::Next() {
    if (!handles_ || !objectArray_) return nullptr;
    auto resolve = reinterpret_cast<PFN_GetGameObject>(
        kAddrCGameObjectArrayGetGameObject);
    while (index_ < size_) {
        uint32_t id = 0;
        __try {
            id = handles_[index_++];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            size_ = 0;
            return nullptr;
        }
        if (id == 0 || id == 0xFFFFFFFFu) continue;  // sentinels

        // CGameObjectArray::GetGameObject returns *false on hit, true on
        // miss* — the function-internal "no match" branch falls through to
        // `return true` after writing NULL to the out-param. Flag interpreted
        // as the canonical "did the lookup fail" rather than "did it
        // succeed". Decompiled @0x004d8230, verified 2026-05-04.
        void* out = nullptr;
        bool miss = true;
        __try {
            miss = resolve(objectArray_, id, &out);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            miss = true;
        }
        if (!miss && out) return out;
    }
    return nullptr;
}

}  // namespace acc::engine
