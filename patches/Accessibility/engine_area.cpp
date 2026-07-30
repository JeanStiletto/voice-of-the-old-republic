#include "engine_area.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine_app.h"     // GetServerApp
#include "engine_player.h"  // GetPlayerArea
#include "engine_reads.h"   // ExtractTextOrStrRef, ReadCExoString
#include "log.h"            // seam-filter telemetry
#include "strings.h"        // door state suffix lookup (DoorOpen/DoorLocked)
#include "engine_rebase.h"

namespace acc::engine {

namespace {

typedef void* (__thiscall* PFN_CSWSAreaGetRoom)(void* this_,
                                                Vector* pos,
                                                int* outRoomIndex);
typedef void* (__thiscall* PFN_GetObjectArray)(void* this_);
typedef bool  (__thiscall* PFN_GetGameObject)(void* this_,
                                              uint32_t id,
                                              void** out);

// CServerExoApp → GetObjectArray() → CGameObjectArray*. The walk to the
// server app is guarded inside GetServerApp(); the __try here covers the
// engine call, which can still fault during teardown / very early init.
void* GetServerObjectArray() {
    void* serverApp = GetServerApp();
    if (!serverApp) return nullptr;
    __try {
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
    return handle == 0u || handle == 0xFFFFFFFFu || handle == kInvalidObjectId;
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

}  // namespace

void* ResolveClientObject(uint32_t handle) {
    if (IsSentinelHandle(handle)) return nullptr;

    void* clientApp = GetClientApp();
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

    // Static doors are non-interactive set dressing — the engine never lets
    // anyone open them (it offers no actions at all). They're frequently also
    // flagged locked in the blueprint, but "verriegelt" misleads the player
    // into hunting for a key/slice that doesn't exist. Label them "kosmetisch"
    // instead and skip the rest of the suffix (state/transition/description are
    // all meaningless on a door that can't be used).
    if (IsDoorStatic(serverDoor)) {
        AppendCommaSeparated(outBuf, bufSize,
            acc::strings::Get(acc::strings::Id::DoorCosmetic));
        return;
    }

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
    if (IsSentinelHandle(handle)) return false;

    void* clientApp = GetClientApp();
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
        // CSWSTrigger.transition_destination: inline text pointer at +0
        // with length-or-strref at +4 (same dual-slot convention
        // ExtractTextOrStrRef consumes for doors). A trigger with no
        // destination has a null text pointer and the GFF "no strref"
        // sentinel 0xFFFFFFFF in the +4 slot.
        //
        // The old probe read the first 12 bytes as a Vector and tested
        // != 0.0f — but the 0xFFFFFFFF sentinel decodes as NaN, and
        // NaN != 0.0f is true, so EVERY destination-less trigger (traps,
        // banter/dialogue triggers, shield triggers) classified as a
        // transition: they polluted the Übergang cycling category and
        // fired the Transition proximity cue on approach (2026-07-16
        // Südlicher Strand session). Mirror ExtractTextOrStrRef's
        // resolution order instead, with LookupTlk's strref validity
        // bounds, minus the actual TLK call (this predicate runs per
        // object per tick in the change detector).
        unsigned char* p = reinterpret_cast<unsigned char*>(trigger) +
                           kTriggerTransitionDestOffset;
        const char* text = *reinterpret_cast<const char**>(p);
        uint32_t    aux  = *reinterpret_cast<uint32_t*>(p + 4);
        if (text) return aux > 0;  // inline text: aux is its length
        // No inline text: aux is the TLK strref (0 / -1 / out-of-range
        // all mean "no destination", matching LookupTlk).
        return aux != 0 && aux != 0xFFFFFFFFu && aux <= 0x100000u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int GetTriggerGeometry(void* trigger, Vector* out, int maxVerts) {
    if (!trigger || !out || maxVerts <= 0) return 0;
    __try {
        unsigned char* p = reinterpret_cast<unsigned char*>(trigger);
        int count = *reinterpret_cast<int*>(p + kTriggerGeometryCountOffset);
        Vector* verts =
            *reinterpret_cast<Vector**>(p + kTriggerGeometryOffset);
        // Authored polygons are 3..8 vertices; anything outside a sane
        // range is a mis-read (wrong object kind / stale pointer), not a
        // real shape.
        if (!verts || count < 3 || count > 32) return 0;
        if (count > maxVerts) count = maxVerts;
        for (int i = 0; i < count; ++i) out[i] = verts[i];
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool GetObjectLocalBoolean(void* gameObject, int index) {
    if (!gameObject || index < 0 || index >= 96) return false;
    __try {
        uint32_t* words = reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(gameObject) +
            kObjectVarTableOffset);
        return (words[index >> 5] & (1u << (index & 31))) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TrapDetectedByAnyOf(void* gameObject,
                         const uint32_t* ids, int idCount) {
    if (!gameObject || !ids || idCount <= 0) return false;
    size_t listOff = 0;
    int kind = GetObjectKind(gameObject);
    if (kind == static_cast<int>(GameObjectKind::Trigger)) {
        listOff = kTriggerTrapDetectedListOffset;
    } else if (kind == static_cast<int>(GameObjectKind::Door)) {
        listOff = kDoorTrapDetectedListOffset;
    } else if (kind == static_cast<int>(GameObjectKind::Placeable)) {
        listOff = kPlaceableTrapDetectedListOffset;
    } else {
        return false;
    }
    __try {
        unsigned char* base = reinterpret_cast<unsigned char*>(gameObject);
        uint32_t* data  = *reinterpret_cast<uint32_t**>(base + listOff);
        int       count = *reinterpret_cast<int*>(base + listOff + 4);
        if (!data || count <= 0) return false;
        if (count > 64) count = 64;  // sanity cap on a corrupt read
        for (int i = 0; i < count; ++i) {
            for (int j = 0; j < idCount; ++j) {
                if (data[i] == ids[j]) return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
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

bool IsDoorStatic(void* serverDoor) {
    if (!serverDoor) return false;
    __try {
        return *reinterpret_cast<uint32_t*>(
                   reinterpret_cast<unsigned char*>(serverDoor) +
                   kDoorStaticOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Root-cause fix (2026-07-19, rev 2): the Endar Spire opening HOLDS the global
// fade panel at alpha=1.0 for a long scripted stretch *after* the fade
// animation finishes (IsFadeDone), while the player still has full world
// control. The engine re-asserts that alpha every frame — proven: an alpha=0
// write from our tick is reverted before MainLoop's gate reads it, so clearing
// the fade can never win. MainLoop gates DoPassiveSelection behind
// !IsGlobalFadeObscuring (alpha > 0.001), which freezes the Q/E candidate halo
// AND passive hover-narration. For a sighted player a black screen means
// "nothing to target"; a blind player is actively navigating that (to them
// irrelevant) black screen and needs targeting + narration live.
//
// So rather than fight the fade, DRIVE the selection ourselves: when the ONLY
// thing MainLoop is waiting on is the held obscuring fade (input is normal
// world, area is displayed, the fade is finished — not mid-animation), call
// DoPassiveSelection directly. It rebuilds the halo from GetNearestObjects and
// refreshes passive narration — exactly what the engine would do if the fade
// weren't pinning the gate shut. We only act while MainLoop itself is skipping
// it (obscuring), so we never double-drive, and we leave the visible fade
// untouched (it's the engine's to own). Returns true when it drove a pass.
// SEH-guarded. Offsets: gui_in_game +0x40, fade +0x6c, panel.alpha +0x4c,
// input_class +0x9c, area_not_ready +0x288.
bool MaybeDrivePassiveSelection() {
    constexpr size_t kClientInternalOffset = 0x4;    // CClientExoApp -> internal
    constexpr size_t kGuiInGameOffset      = 0x40;   // internal.gui_in_game
    constexpr size_t kFadeOffset           = 0x6c;   // CGuiInGame.fade
    constexpr size_t kFadeAlphaOffset      = 0x4c;   // CSWGuiFade.panel.alpha
    constexpr size_t kInputClassOffset     = 0x9c;   // internal.input_class
    constexpr size_t kAreaNotReadyOffset   = 0x288;  // internal.area_not_ready
    const uintptr_t kAddrIsGlobalFading = acc::addr::R(0x0062ac60);  // __thiscall(gui)->int
    const uintptr_t kAddrDoPassiveSelection = acc::addr::R(0x005fa5a0);  // __thiscall(internal,float)
    using PFN_Fade      = int(__thiscall*)(void*);
    using PFN_DoPassive = void(__thiscall*)(void*, float);

    static bool s_driving = false;  // rising-edge log latch

    void* clientApp = GetClientApp();
    if (!clientApp) { s_driving = false; return false; }

    __try {
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) + kClientInternalOffset);
        if (!internal) { s_driving = false; return false; }
        unsigned char* base = reinterpret_cast<unsigned char*>(internal);

        void* guiInGame = *reinterpret_cast<void**>(base + kGuiInGameOffset);
        void* fade = guiInGame ? *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(guiInGame) + kFadeOffset) : nullptr;
        if (!fade) { s_driving = false; return false; }

        float alpha = *reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(fade) + kFadeAlphaOffset);
        uint32_t inputClass =
            *reinterpret_cast<uint32_t*>(base + kInputClassOffset);
        uint32_t areaNotReady =
            *reinterpret_cast<uint32_t*>(base + kAreaNotReadyOffset);

        // Drive only when MainLoop is skipping DoPassiveSelection *purely*
        // because of the held obscuring fade: obscuring (alpha > 0.001), normal
        // world input, area displayed, and the fade FINISHED (not an active
        // transition — leave those brief windows alone).
        bool obscuring = alpha > 0.001f;
        bool worldReady = (inputClass == 0 || inputClass == 4) &&
                          areaNotReady == 0;
        if (!obscuring || !worldReady) { s_driving = false; return false; }

        int fading = reinterpret_cast<PFN_Fade>(kAddrIsGlobalFading)(guiInGame);
        if (fading != 0) { s_driving = false; return false; }

        reinterpret_cast<PFN_DoPassive>(kAddrDoPassiveSelection)(internal, 0.016f);

        if (!s_driving) {
            s_driving = true;
            acclog::Write("FadeUnstick",
                "obscuring fade held (alpha=%.2f) with world input — driving "
                "DoPassiveSelection directly to keep Q/E halo + narration live",
                alpha);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_driving = false;
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
