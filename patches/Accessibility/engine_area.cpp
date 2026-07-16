#include "engine_area.h"

#include <windows.h>
#include <cmath>
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
        return static_cast<int>(*reinterpret_cast<uint8_t*>(
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

void* ResolveClientObject(uint32_t handle) {
    if (IsSentinelHandle(handle)) return nullptr;

    void* clientApp = GetClientExoApp();
    if (!clientApp) return nullptr;

    __try {
        auto fn = reinterpret_cast<PFN_CClientGetGameObject>(
            kAddrCClientExoAppGetGameObject);
        return fn(clientApp, handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* ResolveClientObjectHandle(uint32_t handle) {
    void* clientObject = ResolveClientObject(handle);
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

bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld,
                                int* outFailReason) {
    if (outFailReason) *outFailReason = 0;
    auto fail = [&](int code) -> bool {
        if (outFailReason) *outFailReason = code;
        return false;
    };
    if (!area || roomIdx < 0) return fail(1);
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        int roomCount = static_cast<int>(*reinterpret_cast<uint32_t*>(
            base + kAreaRoomCountOffset));
        if (roomIdx >= roomCount) return fail(2);

        // kAreaRoomsOffset holds a POINTER to the inline-stride rooms
        // buffer, not the rooms themselves. (Header comment was
        // misleading — see BuildAreaWallCache for the canonical access
        // pattern.)
        void* rooms = *reinterpret_cast<void**>(base + kAreaRoomsOffset);
        if (!rooms) return fail(3);
        auto* roomBase = reinterpret_cast<unsigned char*>(rooms);
        void* room = roomBase +
                     static_cast<size_t>(roomIdx) * kRoomStride;
        void* mesh = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(room) + kRoomSurfaceMeshOffset);
        if (!mesh) return fail(4);

        auto* meshBytes = reinterpret_cast<unsigned char*>(mesh);
        Vector* vertices = *reinterpret_cast<Vector**>(
            meshBytes + kCollisionMeshVerticesOffset);
        uint32_t faceCount = *reinterpret_cast<uint32_t*>(
            meshBytes + kCollisionMeshFaceCountOffset);
        void* faceIndices = *reinterpret_cast<void**>(
            meshBytes + kCollisionMeshFacesOffset);
        if (!vertices || !faceIndices || faceCount == 0) return fail(5);

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
        return fail(6);
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

bool GetObjectTag(void* gameObject, char* outBuf, size_t bufSize) {
    if (!gameObject || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
    __try {
        return ReadCExoString(gameObject, kObjectTagOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetAreaTag(void* area, char* outBuf, size_t bufSize) {
    if (!area || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';
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
    uint32_t isStatic  = 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(serverDoor);
        locked    = *reinterpret_cast<uint32_t*>(base + kDoorLockedOffset);
        openState = *reinterpret_cast<uint8_t*> (base + kDoorOpenStateOffset);
        isStatic  = *reinterpret_cast<uint32_t*>(base + kDoorStaticOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // leave defaults; suffix will skip the state line
    }

    // Static doors are non-interactive set dressing — the engine never lets
    // anyone open them (it offers no actions at all). They're frequently also
    // flagged locked in the blueprint, but "verriegelt" misleads the player
    // into hunting for a key/slice that doesn't exist. Label them "kosmetisch"
    // instead and skip the rest of the suffix (state/transition/description are
    // all meaningless on a door that can't be used).
    if (isStatic != 0) {
        AppendCommaSeparated(outBuf, bufSize,
            acc::strings::Get(acc::strings::Id::DoorCosmetic));
        return;
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
    if (IsSentinelHandle(handle)) return false;

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
            // Prefer the engine's universal display-name accessor (same
            // string a sighted user sees in tooltip / target reticle —
            // localized via dialog.tlk). Generic spawns like
            // tar02_woman02 / tar02_maintdrd have empty first_name
            // strrefs, so the per-stats path below would otherwise fall
            // through to the raw tag. Verified in combat.cpp /
            // combat_queue.cpp where the same accessor returns
            // "Sith-Soldat" etc.
            uint32_t handle = GetObjectHandle(gameObject);
            if (handle != 0u &&
                GetObjectDisplayNameByHandle(handle, outBuf, bufSize) &&
                outBuf[0] != '\0') {
                got = true;
                break;
            }
            outBuf[0] = '\0';
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
            // For a landmark waypoint the curated map-note label (+0x230)
            // IS the canonical name — the exact string sighted players
            // read off the area map, and what the map-context cycle
            // already prioritises. Prefer it outright: stock K1 leaves the
            // waypoint LocName (+0x238) empty but sets a resref-style Tag
            // ("k35_map_dreshdae"), so a LocName-first order would let that
            // machine name win in world context. Non-landmark waypoints
            // have no map note; GetWaypointMapNote returns false and we
            // fall through to LocName → tag.
            if (GetWaypointMapNote(gameObject, outBuf, bufSize)) {
                got = true;
                break;
            }
            got = TryReadLocString(gameObject, kWaypointLocNameOffset,
                                   outBuf, bufSize);
            break;
        case K::Trigger:
            got = TryReadLocString(gameObject, kTriggerLocNameOffset,
                                   outBuf, bufSize);
            // Area-transition triggers carry their human-readable "to X"
            // label in the transition_destination LocString (CSWSTrigger
            // +0x30c), exactly like doors (BuildDoorSuffix reads the door
            // equivalent). The trigger LocName above is near-always empty
            // in stock K1, so without this the Transition cycle category
            // falls through to the raw tag. Non-transition triggers (trap
            // / encounter) have an empty transition_destination and still
            // fall to the tag below.
            if (!got || outBuf[0] == '\0') {
                got = TryReadLocString(gameObject, kTriggerTransitionDestOffset,
                                       outBuf, bufSize);
            }
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

bool IsEmptyContainer(void* gameObject) {
    if (!gameObject) return false;
    // Gate on kind FIRST: has_inventory / item_repository live at
    // placeable offsets that map to unrelated fields on other object
    // kinds, so reading them on a creature/door could fake an empty
    // container.
    int kind = GetObjectKind(gameObject);
    if (kind != static_cast<int>(GameObjectKind::Placeable)) {
        return false;
    }
    __try {
        auto* base = reinterpret_cast<unsigned char*>(gameObject);
        // Only true loot containers qualify; switches / computer panels and
        // other usable-but-not-lootable placeables carry HasInventory == 0
        // and a null repository, and must never get an "empty" tag.
        if (*reinterpret_cast<int*>(base + kPlaceableHasInventoryOffset) == 0) {
            return false;
        }
        void* repo = *reinterpret_cast<void**>(
            base + kPlaceableItemRepositoryOffset);
        if (!repo) return false;
        int count = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(repo) +
            kItemRepositoryItemCountOffset);
        return count <= 0;
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

bool IsDoorOpen(void* serverDoor) {
    if (!serverDoor) return false;
    __try {
        return *(reinterpret_cast<unsigned char*>(serverDoor) +
                 kDoorOpenStateOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// generic_type → material lookup. Generated by joining genericdoors.2da
// (the appearance table) against placeableobjsnds.2da on soundapptype.
// Rows 0-12 lack soundapptype — classified by label keyword. Rows past
// 64 don't exist in stock K1; modded entries fall through to Metal (the
// default armortype for ~80% of stock rows).
//
// Regenerate this table by running build/dump-2da against
// build/2da-extracted/{genericdoors,placeableobjsnds}.2da when the
// upstream 2DAs change (e.g. K1CP update).
namespace {
using M = DoorMaterial;
constexpr int kDoorMaterialTableSize = 65;
constexpr M kDoorMaterialTable[kDoorMaterialTableSize] = {
    /* 0  Wood_strong   */ M::Wood,
    /* 1  Fancy         */ M::Metal,
    /* 2  Porticullis   */ M::Metal,
    /* 3  Porticullis2  */ M::Metal,
    /* 4  Rusted        */ M::Metal,
    /* 5  Metal         */ M::Metal,
    /* 6  Stone_evil    */ M::Stone,
    /* 7  Stone         */ M::Stone,
    /* 8  Jeweled       */ M::Metal,
    /* 9  Wood_weak     */ M::Wood,
    /* 10 ForceField    */ M::Metal,
    /* 11 Test          */ M::Metal,
    /* 12 ForceField2   */ M::Metal,
    /* 13 ManaanDoor1   */ M::Metal,
    /* 14 ManaanDoor2   */ M::Metal,
    /* 15 KorribanDoor1 */ M::Wood,
    /* 16 KorribanDoor2 */ M::Stone,
    /* 17 RakataDoor1   */ M::Stone,
    /* 18 RakataDoor2   */ M::Metal,
    /* 19 TarisDoor1    */ M::Metal,
    /* 20 TarisDoor2    */ M::Metal,
    /* 21 TarisDoor3    */ M::Metal,
    /* 22 TarisDoor4    */ M::Metal,
    /* 23 TatooineDoor1 */ M::Metal,
    /* 24 SithDoor1     */ M::Metal,
    /* 25 SithDoor2     */ M::Metal,
    /* 26 KashDoor1     */ M::Wood,
    /* 27 DantooineDoor1*/ M::Metal,
    /* 28 KorribanDoor3 */ M::Wood,
    /* 29 TatooineDoor2 */ M::Metal,
    /* 30 TatooineDoor3 */ M::Wood,
    /* 31 RakataDoor3   */ M::Stone,
    /* 32 KashDoor2     */ M::Wood,
    /* 33 CzerkaDoor1   */ M::Metal,
    /* 34 DantooineDoor2*/ M::Metal,
    /* 35 DantooineDoor3*/ M::Metal,
    /* 36 DantooineDoor4*/ M::Metal,
    /* 37 TarisDoor5    */ M::Metal,
    /* 38 UnnamedDoor1  */ M::Metal,
    /* 39 SithDoor3     */ M::Metal,
    /* 40 KorribanDoor4 */ M::Wood,
    /* 41 KorribanDoor5 */ M::Stone,
    /* 42 KorribanDoor6 */ M::Stone,
    /* 43 KorribanDoor7 */ M::Stone,
    /* 44 KashDoor3     */ M::Wood,
    /* 45 TarisDoor6    */ M::Metal,
    /* 46 TarisDoor7    */ M::Metal,
    /* 47 SithDoor4     */ M::Metal,
    /* 48 Hammerhead1   */ M::Metal,
    /* 49 TarisDoor8    */ M::Metal,
    /* 50 SithDoor5     */ M::Metal,
    /* 51 TarisDoor9    */ M::Metal,
    /* 52 StarForgeDoor1*/ M::Metal,
    /* 53 ManaanDoor3   */ M::Metal,
    /* 54 ManaanDoor4   */ M::Metal,
    /* 55 ManaanDoor5   */ M::Metal,
    /* 56 ManaanDoor6   */ M::Metal,
    /* 57 RakataDoor4   */ M::Stone,
    /* 58 BrokenDoor    */ M::Metal,
    /* 59 SithDoor1NoPr */ M::Metal,
    /* 60 ManaanDoor2NPr*/ M::Metal,
    /* 61 TarisDoor10   */ M::Metal,
    /* 62 SithForceField*/ M::Metal,
    /* 63 UnknownWld_Dr */ M::Metal,
    /* 64 YavinDoor1    */ M::Metal,
};
}  // namespace

DoorMaterial GetDoorMaterial(void* serverDoor) {
    if (!serverDoor) return DoorMaterial::Metal;
    uint8_t generic_type = 0;
    __try {
        generic_type = *(reinterpret_cast<unsigned char*>(serverDoor) +
                         kDoorGenericTypeOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return DoorMaterial::Metal;
    }
    if (generic_type >= kDoorMaterialTableSize) return DoorMaterial::Metal;
    return kDoorMaterialTable[generic_type];
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
    bool ok = ExtractTextOrStrRef(waypoint,
                                  kWaypointMapNoteLocOffset,
                                  kWaypointMapNoteLocOffset + 4,
                                  outBuf, bufSize);
    return ok && outBuf[0] != '\0';
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

void* GetServerApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

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
        constexpr uintptr_t kAddrGetModuleResourceName = 0x004c4b80;
        struct { char* p; int len; } out = {nullptr, 0};
        using PFN = void* (__thiscall*)(void*, void*);
        reinterpret_cast<PFN>(kAddrGetModuleResourceName)(module, &out);
        return ReadCExoString(&out, 0, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

bool IsLoadingSaveGame() {
    void* serverApp = GetServerApp();  // CServerExoApp facade (AppManager+0x8)
    if (!serverApp) return false;
    __try {
        // CServerExoApp::GetLoadFromSaveGame(this) — returns
        // this->internal->load_from_savegame (decompile-verified). The getter
        // does the facade→internal deref itself, so we pass the facade.
        constexpr uintptr_t kAddrGetLoadFromSaveGame = 0x004af050;
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

uint32_t GetMapPinFlags(void* mapPin) {
    if (!mapPin) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(mapPin) + kMapPinFlagsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
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
    // CExoString at +0x100; engine_reads::ExtractTextOrStrRef tries the
    // inline c_string first, falls back to the strref at the same
    // CExoString's +0x04 slot via TLK lookup. Map pins served over the
    // wire from the server carry inline text; quest-script-created pins
    // may carry strref-only. ExtractTextOrStrRef handles both.
    bool ok = ExtractTextOrStrRef(mapPin,
                                  kMapPinNoteTextOffset,
                                  kMapPinNoteStrrefOffset,
                                  outBuf, bufSize);
    return ok && outBuf[0] != '\0';
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

namespace {

typedef void (__thiscall* PFN_CollisionMeshLocalToWorld)(void* this_,
                                                         Vector* output,
                                                         Vector* localPoint);

// Min XY-length² (~5cm) to skip near-vertical edges that lack a meaningful
// 2D footprint. K1 walkmeshes contain near-vertical step-side edges with
// sub-cm horizontal drift; those break downstream clustering and aren't
// navigable walls in 2D. Matches Pillar 1's kEndpointTolMeters.
constexpr float kMinEdgeXYLengthSq = 2.5e-3f;

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

// Read the three uint32 vertex indices for face `f` from a contiguous
// face-index array. SEH-guarded — returns false on bad pointer. Output
// is left untouched on fault.
bool ReadFaceVertexIndices(unsigned char* faces, uint32_t f, uint32_t outV[3]) {
    __try {
        auto* face = reinterpret_cast<uint32_t*>(
            faces + static_cast<size_t>(f) * kWalkmeshFaceStride);
        outV[0] = face[0];
        outV[1] = face[1];
        outV[2] = face[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Transform a pair of local vertices through the room's
// CCollisionMesh::LocalToWorld. SEH-guarded; on fault falls back to the
// local copies (correct when world_coords=1, the common runtime case
// for room walkmeshes).
void TransformEdgeEndpoints(void* surfaceMesh,
                            PFN_CollisionMeshLocalToWorld fn,
                            const Vector& localA, const Vector& localB,
                            Vector& outWorldA, Vector& outWorldB) {
    Vector la = localA;
    Vector lb = localB;
    outWorldA = la;
    outWorldB = lb;
    __try {
        fn(surfaceMesh, &outWorldA, &la);
        fn(surfaceMesh, &outWorldB, &lb);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outWorldA = la;
        outWorldB = lb;
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
        uint32_t v[3] = {0, 0, 0};
        if (!ReadFaceVertexIndices(faces, f, v)) continue;
        int adj[3] = {0, 0, 0};
        __try {
            adj[0] = adjacencies[f * 3 + 0];
            adj[1] = adjacencies[f * 3 + 1];
            adj[2] = adjacencies[f * 3 + 2];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }

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
            uint32_t va = v[e];
            uint32_t vb = v[(e + 1) % 3];

            Vector localA = {0, 0, 0}, localB = {0, 0, 0};
            __try {
                localA = vertices[va];
                localB = vertices[vb];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            Vector worldA, worldB;
            TransformEdgeEndpoints(surfaceMesh, fnLocalToWorld,
                                   localA, localB, worldA, worldB);

            float xy_dx = worldB.x - worldA.x;
            float xy_dy = worldB.y - worldA.y;
            if (xy_dx * xy_dx + xy_dy * xy_dy < kMinEdgeXYLengthSq) {
                continue;
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

// Global triangle-edge index used by the portal-coincidence filter in
// BuildAreaWallCache. Every triangle edge from every room is recorded
// here regardless of adjacency value; the filter then asks, for each
// emitted adjacency=-1 wall edge, "does any *other* room have a
// coincident edge in its triangle list?" — if yes, the walkmesh
// extends across into that other room, so the edge is a portal and
// gets dropped.
//
// Sized at 16384 for headroom over K1's largest observed area
// (~6000 triangle edges in dense Taris). 16384 × 28 B = ~460 KB static
// — comfortable for our patch's memory budget. Module-static + the
// single-threaded patch model means no reentrancy concerns; each
// BuildAreaWallCache invocation overwrites the contents.
constexpr int kMaxGlobalTriEdges = 16384;
static Vector s_globalEdgeA[kMaxGlobalTriEdges];
static Vector s_globalEdgeB[kMaxGlobalTriEdges];
static int    s_globalEdgeRoom[kMaxGlobalTriEdges];

// Mirror of ScanRoomWallEdges but emits every triangle edge regardless
// of adjacency value. The 5cm² XY-length filter is retained — vertical
// / near-vertical edges aren't useful as 2D portal matches and bloat
// the index. Returns the count of edges this room contributed to the
// running total (written to the global arrays only while there's space).
int ScanRoomAllTriangleEdges(void* surfaceMesh, int roomId,
                             int alreadyWritten) {
    if (!surfaceMesh) return 0;
    auto* mesh = reinterpret_cast<unsigned char*>(surfaceMesh);

    Vector*   vertices    = nullptr;
    uint32_t  faceCount   = 0;
    void*     faceIndices = nullptr;
    __try {
        vertices    = *reinterpret_cast<Vector**>(
            mesh + kCollisionMeshVerticesOffset);
        faceCount   = *reinterpret_cast<uint32_t*>(
            mesh + kCollisionMeshFaceCountOffset);
        faceIndices = *reinterpret_cast<void**>(
            mesh + kCollisionMeshFacesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!vertices || !faceIndices || faceCount == 0) return 0;

    auto fnLocalToWorld = reinterpret_cast<PFN_CollisionMeshLocalToWorld>(
        kAddrCollisionMeshLocalToWorld);
    auto* faces = reinterpret_cast<unsigned char*>(faceIndices);

    int emitted = 0;
    for (uint32_t f = 0; f < faceCount; ++f) {
        uint32_t v[3] = {0, 0, 0};
        if (!ReadFaceVertexIndices(faces, f, v)) continue;

        for (int e = 0; e < 3; ++e) {
            uint32_t va = v[e];
            uint32_t vb = v[(e + 1) % 3];
            Vector localA = {0, 0, 0}, localB = {0, 0, 0};
            __try {
                localA = vertices[va];
                localB = vertices[vb];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            Vector worldA, worldB;
            TransformEdgeEndpoints(surfaceMesh, fnLocalToWorld,
                                   localA, localB, worldA, worldB);
            float xy_dx = worldB.x - worldA.x;
            float xy_dy = worldB.y - worldA.y;
            if (xy_dx * xy_dx + xy_dy * xy_dy < kMinEdgeXYLengthSq) continue;

            int slot = alreadyWritten + emitted;
            if (slot < kMaxGlobalTriEdges) {
                s_globalEdgeA[slot]    = worldA;
                s_globalEdgeB[slot]    = worldB;
                s_globalEdgeRoom[slot] = roomId;
            }
            ++emitted;
        }
    }
    return emitted;
}

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

    // Portal filtering — the "edges of walkable areas" rule.
    //
    // KOTOR's walkmesh joins rooms via portals, not per-triangle
    // adjacency. When room A and room B share a walkable boundary,
    // room A's triangle on its side marks the boundary edge with
    // adjacency=-1 (no neighbour within A). In *some* cases room B
    // does the same → the old symmetric "both sides adj=-1" seam pair
    // caught it. But K1 also has plenty of asymmetric portals where
    // only one side has adj=-1; the other side has a finite-adjacency
    // value pointing at room B's own neighbour. The old filter missed
    // these — they survived as phantom walls.
    //
    // The right rule: a wall exists only where *no* triangle (in any
    // room) sits on the other side of an adj=-1 edge. To check that,
    // we look for a coincident edge in any *other* room's triangle
    // list, regardless of that edge's own adjacency value. Hit → the
    // walkmesh extends across into another room → portal → drop. No
    // hit → boundary of the walkable union → real wall → keep.
    //
    // This is a strict superset of the old symmetric pair filter
    // (matches between adj=-1 edges still match, plus new asymmetric
    // matches). Replaces the old filter outright.
    //
    // Cost: O(N · G) where N = adj=-1 walls in outBuf (~500-1000) and
    // G = total triangle edges across all rooms (~3000-6000). 5M cheap
    // float compares per area-load; once per area-enter.
    int written = (total < maxEdges) ? total : maxEdges;

    // Build the global triangle-edge index — every triangle edge from
    // every room, regardless of adjacency value.
    int globalCount = 0;
    for (uint32_t i = 0; i < roomCount; ++i) {
        void* room = roomBase + static_cast<size_t>(i) * kRoomStride;
        void* sm   = GetRoomSurfaceMesh(room);
        if (!sm) continue;
        int contributed = ScanRoomAllTriangleEdges(sm, static_cast<int>(i),
                                                   globalCount);
        globalCount += contributed;
    }
    bool globalOverflowed = false;
    if (globalCount > kMaxGlobalTriEdges) {
        acclog::Write("AreaWalls",
            "global edge index OVERFLOW: discovered=%d kMaxGlobalTriEdges=%d "
            "— portal filter may miss matches against the tail (raise the cap)",
            globalCount, kMaxGlobalTriEdges);
        globalCount = kMaxGlobalTriEdges;
        globalOverflowed = true;
    }

    // Endpoint match tolerance. LocalToWorld involves matrix math, so
    // a pair of "identical" edges from two different rooms may not be
    // bit-equal — allow ~1cm of slack per coordinate. Squared so we
    // can compare against squared distance and avoid a sqrt.
    constexpr float kSeamEpsSq = 1e-4f;  // ~1cm² in world units
    auto coincident = [&](const Vector& p, const Vector& q) {
        float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        return (dx * dx + dy * dy + dz * dz) < kSeamEpsSq;
    };

    // Per-wall portal flag. Static so we don't burden the stack at the
    // BuildAreaWallCache scope; single-threaded patch ⇒ no reentrancy.
    constexpr int kMaxPortalFlags = 8192;
    static bool s_isPortal[kMaxPortalFlags];
    int flagN = (written < kMaxPortalFlags) ? written : kMaxPortalFlags;
    for (int i = 0; i < flagN; ++i) s_isPortal[i] = false;

    int portalMatches = 0;
    for (int i = 0; i < flagN; ++i) {
        const WallEdge& e = outBuf[i];
        for (int g = 0; g < globalCount; ++g) {
            if (s_globalEdgeRoom[g] == e.room_id) continue;
            bool match =
                (coincident(e.a, s_globalEdgeA[g]) &&
                 coincident(e.b, s_globalEdgeB[g])) ||
                (coincident(e.a, s_globalEdgeB[g]) &&
                 coincident(e.b, s_globalEdgeA[g]));
            if (match) {
                s_isPortal[i] = true;
                ++portalMatches;
                break;  // one match suffices to classify as portal
            }
        }
    }

    int kept = 0;
    for (int i = 0; i < flagN; ++i) {
        if (s_isPortal[i]) continue;
        if (kept != i) outBuf[kept] = outBuf[i];
        ++kept;
    }
    // Tail beyond kMaxPortalFlags (only possible if maxEdges >
    // kMaxPortalFlags AND the area has that many edges) — unfilterable,
    // append unchanged. Pathological; we log if it ever happens.
    if (written > flagN) {
        int tail = written - flagN;
        for (int i = 0; i < tail; ++i) {
            outBuf[kept + i] = outBuf[flagN + i];
        }
        kept += tail;
        acclog::Write("AreaWalls",
            "portal filter exceeded flag buffer (written=%d flagN=%d) — "
            "%d trailing edges unfiltered",
            written, flagN, tail);
    }

    acclog::Write("AreaWalls",
        "portal filter: emitted=%d -> kept=%d (dropped %d as portals via "
        "coincidence against %d global edges%s)",
        written, kept, written - kept, globalCount,
        globalOverflowed ? " [OVERFLOWED]" : "");

    // Same-room duplicate dedup. K1's walkmesh has two recurring
    // patterns where the per-room scan above emits a wall edge twice
    // from the same room:
    //
    //   1. Exact 3D duplicate. Two faces in one room both have
    //      adjacency=-1 on the same physical edge with different
    //      `materials[]` entries (non-manifold authoring). Both edges
    //      have the same 3D endpoints (possibly reversed direction).
    //
    //   2. Step / slanted-face pair. The bottom edge of a step is
    //      stored as a flat wall at Z=0, AND the slanted face of the
    //      step is stored as a separate wall going from the step's
    //      top (Z=2.25 typical) down to the same Z=0 foot. The two
    //      edges share one 3D endpoint exactly (the foot where flat
    //      meets slanted) and the other endpoint matches in XY but
    //      not Z. Same 2D footprint either way.
    //
    // We dedup both: drop the second copy per matched pair, keep the
    // first. material_id isn't read by any production path (logging
    // only), so the surviving copy's material is irrelevant.
    //
    // The match rule is:
    //   - 2D XY footprints match (either direction), AND
    //   - at least one endpoint pair matches in full 3D within
    //     `kSeamEpsSq` tolerance.
    //
    // The 3D-shared-endpoint requirement distinguishes step/slope
    // pairs (always share the foot at Z=0) from genuine multi-floor
    // geometry — lower-corridor wall vs upper-corridor wall at the
    // same XY but with both endpoints at different Z. Multi-floor
    // pairs have no 3D-shared endpoint and survive this pass, which
    // matters when an area has decks / balconies / overhead walkways
    // (Endar Spire, Manaan habitat domes, etc.).
    //
    // The cross-room seam pass above explicitly skips same-room pairs
    // because dropping BOTH copies (the cross-room rule for portal
    // seams) would lose the wall entirely. Here we drop just ONE.
    //
    // Diagnostic motivation: per-edge anomaly dumps from patch-
    // 20260513-080349 + -081234 showed the two patterns above. After
    // this pass, Pillar 1's clustering produces single straight-
    // segment surfaces instead of 2-edge "lens" anomalies, and
    // walltopo gets clean inputs.
    //
    // O(N²) over `kept`. Same cost class as the cross-room pass; runs
    // once per area-load.
    auto xyCoincident = [&](const Vector& p, const Vector& q) {
        float dx = p.x - q.x, dy = p.y - q.y;
        return (dx * dx + dy * dy) < kSeamEpsSq;
    };
    int sameRoomDups = 0;
    int i = 0;
    while (i < kept) {
        int j = i + 1;
        while (j < kept) {
            if (outBuf[i].room_id != outBuf[j].room_id) {
                ++j;
                continue;
            }
            // 2D footprint match in either direction.
            bool xyMatch =
                (xyCoincident(outBuf[i].a, outBuf[j].a) &&
                 xyCoincident(outBuf[i].b, outBuf[j].b)) ||
                (xyCoincident(outBuf[i].a, outBuf[j].b) &&
                 xyCoincident(outBuf[i].b, outBuf[j].a));
            // At least one endpoint pair shared exactly in 3D —
            // distinguishes step/slope pairs (always share the foot)
            // from multi-floor walls (no 3D endpoint in common).
            bool sharesEndpoint3D =
                coincident(outBuf[i].a, outBuf[j].a) ||
                coincident(outBuf[i].a, outBuf[j].b) ||
                coincident(outBuf[i].b, outBuf[j].a) ||
                coincident(outBuf[i].b, outBuf[j].b);
            if (xyMatch && sharesEndpoint3D) {
                outBuf[j] = outBuf[--kept];   // swap-remove
                ++sameRoomDups;
            } else {
                ++j;
            }
        }
        ++i;
    }
    if (sameRoomDups > 0) {
        acclog::Write("AreaWalls",
            "same-room dedup: dropped %d duplicate wall edges "
            "(non-manifold faces / step+slanted-face pairs; multi-floor "
            "walls preserved)",
            sameRoomDups);
    }

    return kept;
}


bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint,
                            bool ignoreZ) {
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

        // 3D guard: the XY test alone treats a wall on another floor as a
        // blocker whenever its 2D projection lands on a→b. Wall edges are
        // walkmesh floor-boundary edges, so their z tracks the floor they
        // bound; reject the hit when the wall edge sits more than a
        // same-floor tolerance from the ray at the crossing point. This is
        // the entire remaining over-block population (validated via the
        // nav-graph crosscheck) — every runtime false-block occurred at
        // elevated z against a ground-floor wall.
        //
        // Skipped when ignoreZ: callers with untrustworthy endpoint z (the
        // waypoint smoother feeds 2D nav nodes) must run pure 2D, or the
        // guard silently drops real same-floor walls when the ray's z is
        // bogus and routes the path straight through them.
        if (!ignoreZ) {
            float rayZ  = a.z   + t * (b.z   - a.z);
            float wallZ = w.a.z + u * (w.b.z - w.a.z);
            float dz = rayZ - wallZ;
            if (dz < 0.0f) dz = -dz;
            if (dz > kWallCrossZToleranceM) continue;
        }

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
