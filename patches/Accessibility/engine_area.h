// Per-area object iteration + room lookup. SEH-guarded; no engine
// re-entry beyond CSWSArea::GetRoom.
//
// CSWSArea:
//   +0x190 game_objects      ulong*  — HANDLE array (not CSWSObject**).
//   +0x194 game_object_count int
//   +0x230 rooms             CSWSRoom* — POINTER to an inline-stride
//                            (0x4c) array; deref before indexing.
//   +0x25c room_names        CExoString*
//
// CSWSArea::GetRoom @0x4bb600(this, Vector*, int* outRoomIndex /*nullable*/)
//   → CSWSRoom*
//
// CSWSObject base:
//   +0x008 object_kind  uint8 GAME_OBJECT_TYPES
//   +0x090 position     Vector
//
// Handle resolution chain:
//   *kAddrAppManagerPtr → AppManager → +0x4 CClientExoApp* /
//     +0x8 CServerExoApp*. The server path is used for object
//     resolution (CSWSObject::GetArea @0x4cb120 confirms +0x8 via
//     `mov ecx,[eax+0x8]`).
//   CServerExoApp::GetObjectArray @0x004aed70 → CGameObjectArray*.
//   CGameObjectArray::GetGameObject(id, &outPtr) @0x004d8230 → bool.
//
// game_objects[] is an array of handles, not pointers. Initial code
// treated entries as CSWSObject** and got kind-byte garbage; iteration
// now resolves each handle through GetGameObject.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector
#include "engine_rebase.h"
#include "engine_offsets_select.h"

namespace acc::engine {

// GAME_OBJECT_TYPES at CSWSObject +0x8. Do not reorder — engine values.
enum class GameObjectKind : int {
    Area         = 4,
    Creature     = 5,
    Item         = 6,
    Trigger      = 7,
    Projectile   = 8,
    Placeable    = 9,
    Door         = 10,
    AreaOfEffect = 11,
    Waypoint     = 12,
    Encounter    = 13,
    Store        = 14,
    Sound        = 16,
};

// Convenience wrapper around engine_player::GetPlayerArea.
void* GetCurrentArea();

// -1 on null / fault. Reads ONE byte — the field is uint8 (max 0x10).
// Reading 4 bytes works "by luck" for objects whose three trailing
// bytes happen to be zero; for the rest the high bytes carry adjacent
// field data and the wide read fails every kind comparison.
int GetObjectKind(void* gameObject);

// CGameObject.id @+0x4. Inverse of ResolveServerObjectHandle. 0 on
// null/fault.
uint32_t GetObjectHandle(void* gameObject);

// Two independent handle namespaces. Both return nullptr for invalid
// sentinels (0, 0xFFFFFFFF, kInvalidObjectId 0x7F000000); both SEH-guard.
//
// Server-side: AppManager → CServerExoApp → GetObjectArray →
// GetGameObject. Same chain AreaObjectIterator uses. Handles look like
// 0x000000XX.
void* ResolveServerObjectHandle(uint32_t handle);

// Client-side: AppManager → CClientExoApp → GetGameObject → CSWCObject*,
// then +0xf8 server_object → CSWSObject*. LastTarget handles (high bit
// set, 0x800000XX) need this path; the server-side CGameObjectArray
// won't find them.
void* ResolveClientObjectHandle(uint32_t handle);

// Same first step WITHOUT the +0xf8 chain — returns the CLIENT object
// (CSWCObject* / CSWCCreature*) for a high-bit client handle. Needed by engine
// calls that operate on the client creature directly (e.g. dialogue initiation
// via CSWCCreature::ActionInitiateDialog). nullptr on sentinel / miss / fault.
void* ResolveClientObject(uint32_t handle);

// CSWSObject +0x90. False on null / fault.
bool GetObjectPosition(void* gameObject, Vector& out);

// CSWSArea::GetRoom @0x4bb600. Captures the room index the engine
// resolved (GetRoom's third arg). outIndex = -1 on miss/fault; key into
// room_names[]. nullptr outside any room.
void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex);

// Centroid of the middle face in world space. Input for per-room
// walkmesh probes (terrain shape classification etc.).
//
// outFailReason codes: 0=ok, 1=bad args, 2=roomIdx≥roomCount,
// 3=rooms ptr null, 4=surface_mesh null ("void" rooms — skybox/
// placeholders without walkmesh), 5=vertices/faces empty, 6=SEH.
bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld,
                                int* outFailReason = nullptr);

// CSWSArea.name CExoLocString @+0x150 (inline → TLK strref @+0x154);
// falls back to CSWSArea.tag CExoString @+0x158 (modder-assigned).
bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize);

// CSWSArea.room_names[index] — CExoString* array, stride 8. NOT
// localized — these are .lyt-room ids like "m02_03e" / "stunt_01_main".
// The transitions consumer wraps with a "Room:" prefix.
bool GetRoomDisplayName(void* area, int roomIndex,
                        char* outBuf, size_t bufSize);

// Per-kind localized name lookup:
//   Door      CSWSDoor.loc_name           @+0x39c
//   Creature  CSWSCreature.creature_stats @+0xa74 → first_name @+0x14
//   Placeable CSWSPlaceable.loc_name      @+0x228
//   Item      CSWSItem.localized_name     @+0x280
//   Waypoint  CSWSWaypoint.localized_name @+0x238
//   Trigger   CSWSTrigger.localized_name  @+0x228
//
// CExoLocString byte-matches CExoString; ExtractTextOrStrRef tries
// inline then TLK strref at +4. Final fallback CSWSObject.tag @+0x18
// (modder-assigned id).
bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize);

// GetObjectName split in two, for callers that need to put something
// BETWEEN the name and its suffix.
//
// GetObjectBaseName is the per-kind localized name on its own — the half
// that never changes while the area is live. AppendObjectStateSuffix adds
// what GetObjectName would have appended for a door (state word, transition
// destination, description); it is a no-op for every other kind.
// GetObjectName is exactly the two composed, so existing callers are
// unaffected.
//
// Narration uses the split so the same-name ordinal can group by the STABLE
// half and land right after the name: "T\xfc""r 3, verriegelt" rather than
// "T\xfc""r, verriegelt 3". Grouping by the enriched string made a door's
// number a function of its live lock/open state, so opening or unlocking one
// door renumbered every other door in the area.
bool GetObjectBaseName(void* gameObject, char* outBuf, size_t bufSize);
void AppendObjectStateSuffix(void* gameObject, char* outBuf, size_t bufSize);

// True when this door was locked at the moment the current area's door
// snapshot was taken — i.e. at area load, before the player could touch
// anything. kDoorLockedOffset is the LIVE flag; this is the frozen one.
//
// Narration groups its door numbering on this rather than the live flag so a
// door keeps its slot when the player picks its lock: "T\xfc""r 2, verriegelt"
// becomes "T\xfc""r 2, entriegelt", and the other locked doors keep their
// numbers. False for non-doors, for doors already unlocked at load, and for
// doors that first appeared after the snapshot.
bool WasDoorLockedAtAreaLoad(void* serverDoor);

// Drops the door lock snapshot; the next query rebuilds it from the live
// area. Wired to the area-transition reset chain in transitions.cpp. (The
// snapshot also self-rebuilds when the area pointer changes; this covers the
// case where a new area lands on the freed one's address.)
void ResetDoorLockSnapshot();

// CSWSObject.tag CExoString @+0x18 — the modder-assigned, locale-INDEPENDENT
// object id (e.g. "tar03_mission031"). Unlike GetObjectName this never
// localizes and never falls back; it's the raw tag or nothing. This is the
// stable identity component used for the discovery-cycling persistence key.
// False on empty / fault; outBuf NUL-terminated on entry.
bool GetObjectTag(void* gameObject, char* outBuf, size_t bufSize);

// CSWSArea.tag CExoString @+0x158 — the area's modder-assigned id. NOTE: in
// practice this is almost always the GFF default "untitled" (modders rarely set
// area Tag), so it is NOT a usable per-area key. Prefer GetCurrentAreaResName.
// False on empty / fault; outBuf NUL-terminated on entry.
bool GetAreaTag(void* area, char* outBuf, size_t bufSize);

// Current module's resource name via CSWSModule::GetModuleResourceName
// @0x004c4b80 — the stable, language-INDEPENDENT module/area resref (e.g.
// "manm26aa"). KOTOR modules are single-area, so this is the canonical per-area
// id (unlike the area GFF Tag, which defaults to "untitled"). Walks the server
// module chain; no `area` arg needed. False on no module / fault; outBuf
// NUL-terminated on entry.
bool GetCurrentAreaResName(char* outBuf, size_t bufSize);

// One global plot NUMBER (engine stores it as a byte) by name, via
// CServerExoApp::GetGlobalVariableTable @0x004aee60 + GetValueNumber
// @0x00529240. Returns -1 on any null link / fault; the engine writes 0 for an
// unknown name. Language-independent (globals are keyed by ASCII name). Read
// live — plot scripts mutate these. Both games (K2 twins @0x0051C890 /
// @0x00654860 — see the call site for the witnesses).
//
// This is the ONLY reader: swoop_race used to carry a byte-identical private
// copy and now calls this.
int ReadGlobalNumber(const char* name);

// CServerExoApp::GetLoadFromSaveGame @0x004af050 — the engine's own "I am
// loading a saved game" flag (CServerExoAppInternal.load_from_savegame).
// Set true by CSWGuiSaveLoad::LoadGame / CGuiInGame::DoQuickLoad for the
// duration of any save-game load (main-menu OR in-game OR F9 quickload),
// and held false during a plain save and during normal play. The clean
// signal for suppressing the message-buffer replay + in-game-GUI
// reconstruction burst the engine produces while restoring a save. False
// on no server app / fault.
bool IsLoadingSaveGame();

// Wraps CClientExoApp::GetObjectName(ulong, CExoString*) — the engine's
// universal accessor. Better than GetObjectName(obj,...) from a handle
// because it handles empty first_name with the template + appearance.2da
// + racialtypes.2da fallbacks. Routes client/server handles correctly.
// outBuf NUL-terminated on entry.
bool GetObjectDisplayNameByHandle(uint32_t handle,
                                  char* outBuf, size_t bufSize);

// Pillar 4 sub-state predicates (refine the kind filter):
//   IsUsablePlaceable    CSWSPlaceable.usable @+0x328 OR
//                        has_inventory @+0x324. Drops scenery placeables.
//   IsLandmarkWaypoint   CSWSWaypoint.has_map_note @+0x228.
//   IsTransitionTrigger  CSWSTrigger.transition_destination @+0x30c
//                        (loc-string presence: inline text, or a valid
//                        TLK strref — the 0xFFFFFFFF sentinel means no
//                        destination, i.e. trap / banter / script
//                        trigger, NOT a transition).
bool IsUsablePlaceable(void* placeable);
bool IsLandmarkWaypoint(void* waypoint);
bool IsTransitionTrigger(void* trigger);

// Copies the trigger's footprint polygon (CSWSTrigger.geometry — Vector
// array in WORLD space, count @+0x284, data @+0x288) into `out`, up to
// maxVerts. Returns the number of vertices written; 0 on null / non-sane
// count / fault. Authored trigger polygons are 3..8 vertices (GIT
// Geometry list); the engine stores them pre-translated, so consumers can
// run point-in-polygon against player world coordinates directly.
int GetTriggerGeometry(void* trigger, Vector* out, int maxVerts);

// Reads one bit of the object's fixed NWScript local-boolean table
// (CSWVarTable @ CSWSObject+0x110, `ulong local_booleans[3]` — the table
// GetLocalBoolean/SetLocalBoolean script commands hit; bit layout
// confirmed against CSWVarTable::GetLocalBoolean @0x0059b000). Valid
// index 0..95. False on null / out-of-range / fault. NOT the named
// CSWSScriptVarTable at +0x100 — see persistence-scriptvartable.md for
// the two-table split.
bool GetObjectLocalBoolean(void* gameObject, int index);

// True only when `gameObject` is a loot container (kind == Placeable AND
// has_inventory != 0) whose CItemRepository currently holds zero items —
// i.e. a chest/footlocker/corpse the player has already emptied (or that
// spawned empty). Self-gates on kind so it's safe to call for any object;
// returns false for creatures, doors, pure usable placeables (switches /
// computer panels with has_inventory == 0), and on any fault. Read live
// at narration time, not cached — looting changes the count.
bool IsEmptyContainer(void* gameObject);

// CSWSDoor.open_state +0x2cc. state >= 1 covers both opening anim and
// fully open. False on null/fault/state==0.
bool IsDoorOpen(void* serverDoor);

// CSWSDoor Static flag +0x3c0 (UTD Static). A static door is
// non-interactive set dressing — the engine never lets anyone open it and
// offers no actions on it. We label these "cosmetic" in narration and
// exclude them from the room-shape door announcements (a cosmetic door in a
// corridor must not read as a way through). True only when the flag is
// non-zero; false on null/fault.
bool IsDoorStatic(void* serverDoor);

// A door the player can NEVER use — Static, with (on KOTOR 2) no lock
// evidence to the contrary. K2 flags real locked doors Static (Harbinger
// HammerHeadDoor2), so there Static alone is not enough: a live lock, an
// open state, or locked-at-load re-classifies the door as real. This is
// the predicate narration, numbering, and the room-shape door snapshot
// must share — they must never disagree on what "kosmetisch" means.
bool IsDoorCosmetic(void* serverDoor);

// Root-cause fix (2026-07-19, rev 2): the Endar Spire opening HOLDS the global
// fade at alpha=1.0 (engine re-asserts it every frame, so it can't be cleared)
// after the fade animation finishes, while the player keeps world control. That
// gates MainLoop's DoPassiveSelection off, freezing the Q/E candidate halo and
// passive narration. Call once per frame from OnUpdate: when the only blocker
// is the held obscuring fade (world input, area displayed, fade finished), it
// drives DoPassiveSelection directly so targeting + narration stay live. The
// visible fade is left untouched. Returns true when it drove a pass.
bool MaybeDrivePassiveSelection();

// Derived from CSWSDoor.generic_type +0x2a1 via a static 65-entry table
// (genericdoors.2da ⋈ placeableobjsnds.2da on soundapptype). Rows 0-12
// don't set soundapptype; classified by label keyword. Metal is the
// fallback for unknown indices.
enum class DoorMaterial { Metal, Wood, Stone };

// Metal on null / fault / out-of-range.
DoorMaterial GetDoorMaterial(void* serverDoor);

// CSWSWaypoint.map_note_enabled +0x22c. Engine's fog-of-war gate for
// map-note text — disabled until the player discovers via map-pin trigger.
bool IsMapNoteEnabled(void* waypoint);

// Replica of the write path in ExecuteCommandSetMapPinEnabled(TRUE)
// @0x0054b460: guard on has_map_note (+0x228), then set the int at
// +0x22c on the SERVER waypoint. The engine action's follow-up client
// message only updates CSWCMapPin quest markers (a GetMapPin miss for
// GIT waypoints), so this field write is the complete effect for map
// notes: the in-game map, our cycle/beacon, and save serialization all
// read this field. False on null / no-map-note / fault.
bool EnableMapNote(void* waypoint);

// AppManager → CServerExoApp → GetModule → CSWSModule.area_map @+0x218.
// Shared by map_ui_cursor (cursor projection + fog) and cycle_state
// (map-context fog filter).
void* GetAreaMap();

// CSWSAreaMap::IsWorldPointExplored @0x00579210. Used to filter map
// cycle output so we don't narrate undiscovered landmarks.
bool IsWorldPointExplored(void* areaMap, const Vector& pos);

// Fog-of-war grid cell size in world metres. The engine reveals the map
// around each party member per tick via SetWorldPointExplored(pos, 1) —
// the member's own cell plus its 4 orthogonal neighbours — so this cell
// size is the scale of the sighted player's map-note discovery (a note
// appears once its cell is explored, i.e. from ~1-2 cell-widths away).
// Full model: ingame-screens-reference.md fog_of_war_exploration_model.
// False on null map or degenerate grid fields; outputs untouched then.
bool GetFogCellSizeM(void* areaMap, float& outCellXM, float& outCellYM);

// CSWSAreaMap::GetMapRotateCCWFromWorldOrientation @0x00578ed0.
// Engine runs Yaw(orientation) + 0/90/180/270 (per area map's own
// orientation tag). Returns the rotation to apply to the compass-arrow
// sprite (player facing in map-local space, CCW-from-+X like engine
// yaw). Callers convert to compass-frame via EngineYawToCompass.
// Engine returns x87 float10 via ST(0); wrapper casts to float.
bool GetMapRotateCCWFromWorldOrientation(void* areaMap,
                                         const Vector& orientation,
                                         float& outDegCCW);

// Back-pointer at CSWSArea +0x2d0. CSWCArea owns the map-pin array
// (quest objective markers, NWScript-placed pins). Mirror set at
// area-load.
void* GetClientArea(void* serverArea);

int GetMapPinCount(void* clientArea);

// Pointer-array semantics (CSWCMapPin**, 4-byte stride, each slot a
// heap-allocated 0x110-byte pin). Lane's PlaceHolder typing as a
// singular pointer is misleading.
void* GetMapPinAt(void* clientArea, int i);

// CSWCMapPin embeds CSWCObject at +0; position at +0x24 (standard
// CGameObject offset). Z is the live server-written value.
bool GetMapPinPosition(void* mapPin, Vector& out);

// CSWCMapPin.reference_number +0x108 (the engine's GetMapPin lookup key).
// NOT a flag bitfield: for engine map-note pins this is the source
// waypoint's CLIENT object id (set by HandleServerToPlayerMapPinEnabled /
// ...ReferenceNumber), which always carries the 0x80000000 client-namespace
// high bit. So it CANNOT discriminate the mod's saved markers from engine
// note pins — use acc::map_user_markers::IsUserMarkerPin (identity) for
// that. Kept as a raw field accessor; no current caller.

// CSWCMapPin.enabled +0xfc. SetMapPinEnabled toggles without removing
// the array slot — filter callers check before surfacing text.
bool IsMapPinEnabled(void* mapPin);

// CSWCMapPin.note_text CExoString @+0x100. Set by the wire packet
// handler after operator_new(0x110).
bool GetMapPinNoteText(void* mapPin, char* outBuf, size_t bufSize);

// Replicates the engine pattern from
// HandleServerToPlayerMapPinReferenceNumber @0x652d60:
//   operator_new(0x110) → CSWCMapPin ctor → field writes (pos +0x24,
//   enabled +0xfc, note_text via CExoString::operator= +0x100, flags
//   +0x108, subtype +0x10c) → CSWCArea::AddMapPin @0x606d90.
//
// referenceNumber keys SetMapPinEnabled / GetMapPin. The engine uses a
// per-player counter from 1; we reserve high-half range to avoid collisions.
//
// `name` is copied into a heap CExoString via the engine's operator= —
// matched to ~CSWCMapPin's free.
//
// In-area only. Persistence (NW_MAP_PIN_*_{N} ScriptVarTable strings)
// is a separate follow-up; for the saved-marker hotkey, in-area is
// acceptable. Pins vanish on area transition.
bool CreateMapPin(void*       clientArea,
                  const Vector& pos,
                  const char* name,
                  uint32_t    referenceNumber,
                  void**      outPin = nullptr);

// CSWSWaypoint.map_note CExoLocString @+0x230 (strref @+0x234).
// BioWare-authored display labels ("Bridge", "Cargo Hold", ...).
bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize);

// Snapshots data pointer + size at construction. The engine rebuilds
// game_objects on area-load, never mid-frame, so single-tick scans are
// safe. Per-Next() resolution via cached CGameObjectArray*. All reads
// SEH-guarded; construction fault yields immediate exhaustion.
class AreaObjectIterator {
public:
    explicit AreaObjectIterator(void* area);

    // Skips 0-handle slots + handles GetGameObject rejects (engine
    // treats some as sentinel during area-unload bookkeeping).
    void* Next();

    int   SnapshotSize() const { return size_; }

private:
    uint32_t* handles_;
    int       size_;
    int       index_;
    void*     objectArray_;  // CGameObjectArray* (server-side master table)
};

}  // namespace acc::engine

// CSWSArea::GetRoom — __thiscall. On K1 the third arg is an int*
// outRoomIndex (NULL-passable per PositionWalkable's decomp). The K2 twin
// has a FOUR-arg signature — (pos, outAux, inOutRoomIndex) — so callers
// must go through GetRoomAtIndexed, which selects the matching typedef per
// game; calling the K2 twin with the K1 shape imbalances the stack (see
// the PFN_CSWSAreaGetRoomK2 note in engine_area.cpp).
const uintptr_t kAddrCSWSAreaGetRoom = acc::addr::Pick(0x004BB600, 0x0054b1d0);

// Handle-resolution chain (server-side master object table). The
// AppManager → CServerExoApp hop itself is engine_app.h's GetServerApp().
const uintptr_t kAddrCServerExoAppGetObjectArray = acc::addr::Pick(0x004AED70, 0x0051C080);
const uintptr_t kAddrCGameObjectArrayGetGameObject = acc::addr::Pick(0x004D8230, 0x0053DFB0);

// Client-side resolver: CClientExoApp::GetGameObject(ulong) -> CSWCObject*.
// Direct one-call wrapper around the client-side game object array (the
// inner CGameObjectArray pointer is held inside the CClientExoApp
// instance; we never need to touch it directly because GetGameObject
// hides the array layer). Verified live 2026-05-04 — see
// ResolveClientObjectHandle's docs.
//
// K2 facade found by facade-cluster alignment (K1 order GetPlayerCreature
// +4 slots → GetGameObject) and confirmed by body: 0x0073F4D0 unwraps the
// handle and calls the ALREADY-BANKED CGameObjectArray::GetGameObject
// (0x0053DFB0, below) on the array at [internal+0x14]. Same thiscall
// object*(id) signature as KOTOR 1's thunk.
const uintptr_t kAddrCClientExoAppGetGameObject = acc::addr::Pick(0x005ED580, 0x0073F4D0);

// Map + fog-of-war chain.
const uintptr_t kAddrCServerExoAppGetModule = acc::addr::Pick(0x004AE6B0, 0x0051bd90);
const size_t    kModuleAreaMapOffset                 = acc::off::Pick(0x218, 0x238);
// K2 twin 0x005F73F0, decompile-confirmed: the same bit-test body (explored
// dwords at +0, count +4, res-x +8; grid index -> `1 << (idx & 0x1f)` test),
// calling the K2 GetMapPixelFromWorldCoord/GetGridPixelFromMapPixel twins.
const uintptr_t kAddrCSWSAreaMapIsWorldPointExplored = acc::addr::Pick(0x00579210, 0x005F73F0);
// __thiscall(this, Vector by value). Returns float10 via ST(0).
// BYTES_PURGED=12 (callee pops 3-float Vector).
// K2 twin 0x005F7170, disasm-confirmed: atan2 on the vector then the
// NorthAxis switch on [this+0x10]; called from the K2
// SetPartyMemberWorldOrientation twin exactly as K1's is. Same 12-byte
// by-value Vector, same ST(0) return.
const uintptr_t kAddrCSWSAreaMapGetMapRotateCCW = acc::addr::Pick(0x00578ED0, 0x005F7170);
// CSWSAreaMap fog grid geometry (Initialize @0x00578c60): fixed 440x256
// map-pixel space, MapResX cells across, MapResY = MapResX * 256/440
// truncated; world-units-per-map-pixel from the .are Map calibration.
// map_ui_cursor keeps a local copy of the transform fields (+0x18..+0x24)
// for its cursor projection.
// CSWSAreaMap fog grid is byte-identical between games — witnessed in K2's
// area loader (SetMapData @0x005f6b90: explored-bits ptr +0, bit-dword count
// +4, NorthAxis +0x10, 1/zoom +0x14, world origin +0x20/+0x24), and the
// world-per-pixel transform read at +0x18/+0x1c exactly as KOTOR 1.
const size_t kAreaMapResXOffset        = acc::off::Same(0x8);
const size_t kAreaMapResYOffset        = acc::off::Same(0xc);
const size_t kAreaMapWorldPerPxXOffset = acc::off::Same(0x18);
const size_t kAreaMapWorldPerPxYOffset = acc::off::Same(0x1c);

// CSWCMapPin allocation chain. operator_new at 0x43e1b0 is matched to
// the _free that CExoString::operator= and ~CSWCMapPin invoke.
// K2 twins, all witnessed in the engine's own script pin-creator 0x0082D670
// (new(0x110) + ctor + SetPosition + note assign + field stores + AddMapPin
// — the exact sequence CreateUserMarkerPin replicates):
//   operator new 0x00919723 (the allocator every K2 ctor call this port has
//   decompiled goes through), pin ctor 0x00893460 (RTTI vtable store +
//   the same +0xfc/+0x10c zero-inits), AddMapPin 0x007A9640 (appends to
//   the client area's pin array at +0x1c8 via the shared array-append
//   0x0083EA60), CExoString::operator=(char*) 0x007338D0 (strlen /
//   free-old / realloc / copy — K1's exact shape).
const uintptr_t kAddrOperatorNew = acc::addr::Pick(0x0043E1B0, 0x00919723);  // __cdecl(ulong)
const uintptr_t kAddrCSWCMapPinCtor = acc::addr::Pick(0x00692540, 0x00893460);
const uintptr_t kAddrCExoStringAssignFromCString = acc::addr::Pick(0x005E5140, 0x007338D0);  // __thiscall(CExoString*, char*)
const uintptr_t kAddrCSWCAreaAddMapPin = acc::addr::Pick(0x00606D90, 0x007A9640);  // __thiscall(CSWCArea*, pin)

// Server→client back-pointer (CSWSArea ends at +0x2d0 preceded by
// CPathfindInformation* at +0x2cc).
// Still Todo on K2 DELIBERATELY: no K2 witness found (no tiny attach writer
// in the client-area region), and GetClientArea routes around it there via
// the client-module chain — the route K2's own pin creator uses. Resolve
// only if a caller ever needs a NON-current area's client twin on K2.
const size_t kAreaClientAreaBackOffset = acc::off::Todo(0x2d0);

// CSWCModule.area — the CURRENT client area. K2-only consumer (GetClientArea's
// K2 branch); witnessed in the script pin-creator 0x0082D670, whose
// [module+0x48] read feeds AddMapPin's `this`.
const size_t kClientModuleAreaOffset = acc::off::Kotor2Only(0x48);

// CSWCArea.map_pins (pointer array; 4-byte stride confirmed via
// AddMapPin / ClearAllMapPins / GetMapPin decomps).
// K2: the triple shifted +4 — its AddMapPin 0x007A9640 appends through a
// CExoArrayList AT +0x1c8 ({data, size, cap} = +0x1c8/+0x1cc/+0x1d0).
const size_t kClientAreaMapPinsOffset       = acc::off::Pick(0x1c4, 0x1c8);
const size_t kClientAreaMapPinsCountOffset  = acc::off::Pick(0x1c8, 0x1cc);
const size_t kClientAreaMapPinsCapOffset    = acc::off::Pick(0x1cc, 0x1d0);

// CSWCMapPin layout is IDENTICAL on K2 (size 0x110 both games), witnessed in
// the K2 pin ctor 0x00893460 (+0xfc/+0x10c zero-inits), the script creator
// 0x0082D670 (flags +0x108, subtype=1 +0x10c, enabled=1 +0xfc) and the
// SetPosition virtual 0x007EEF90 (Vector store at [this+0x24]).
const size_t kMapPinPositionOffset = acc::off::Same(0x24);   // Vector (CGameObject base)
const size_t kMapPinEnabledOffset  = acc::off::Same(0xfc);   // int
const size_t kMapPinNoteTextOffset = acc::off::Same(0x100);  // CExoString
// Literal (kCExoStringStride is forward-declared below) — every
// CExoString in this header pairs strref at +0x4.
// const, not constexpr: derived from kMapPinNoteTextOffset, which is now
// resolved per game at load time. The +0x4 is the strref's position INSIDE the
// CExoString, so it rides along with whatever the text offset turns out to be
// and needs no marker of its own.
const size_t kMapPinNoteStrrefOffset = kMapPinNoteTextOffset + 0x4;
const size_t kMapPinFlagsOffset    = acc::off::Same(0x108);  // uint32 reference-number / quest bitfield
const size_t kMapPinSubtypeOffset  = acc::off::Same(0x10c);  // int (1 = user-placed note pin)

// CSWSArea offsets. Lane's SARIF (CSWSArea SIZE=0x2d4).
// K2 values witnessed in CSWSArea's loader (0x00523870), destructor
// (0x0052b4e0) and GetRoom (0x0054b1d0). The game-object list and rooms array
// consolidated on K2: rooms count and array are adjacent (+0x250/+0x254),
// where KOTOR 1 kept them apart (+0x268/+0x230). RoomName strings moved to
// +0x280 (stride still 8). Name/Tag took the shallow +4 shift.
const size_t kAreaGameObjectsOffset      = acc::off::Pick(0x190, 0x194);
const size_t kAreaGameObjectCountOffset  = acc::off::Pick(0x194, 0x198);
const size_t kAreaRoomsOffset            = acc::off::Pick(0x230, 0x254);  // CSWSRoom* (deref first)
const size_t kAreaNameLocOffset          = acc::off::Pick(0x150, 0x154);  // CExoLocString
const size_t kAreaTagOffset              = acc::off::Pick(0x158, 0x15c);  // CExoString fallback
const size_t kAreaRoomNamesOffset        = acc::off::Pick(0x25c, 0x280);  // CExoString*
const size_t kAreaRoomCountOffset        = acc::off::Pick(0x268, 0x250);  // ulong
const size_t kCExoStringStride           = acc::off::Same(0x8);

const size_t kRoomStride = acc::off::Same(0x4c);

// CSWSObject base. kServerObjectPositionOffset (0x90) lives in engine_player.h.
// CGameObject.ObjectType — identical in both games (seeded
// kotor2_steam_aspyr.db); CGameObject is the shallow root that KOTOR 2 did not
// grow. Still a uint8 read, not a wider one — see the kind-enum note.
const size_t kObjectKindOffset = acc::off::Same(0x8);   // uint8 GAME_OBJECT_TYPES
const size_t kObjectTagOffset  = acc::off::Same(0x18);  // CExoString fallback id

// Per-subclass localized-name offsets (CExoLocString unless noted). K2 door
// values witnessed in CSWSDoor's load (0x0061a210) / save (0x0061bfe0) /
// GetFirstName (+0x3ec) — the door body grew, so these took a large per-field
// shift rather than the shallow +4.
const size_t kDoorLocNameOffset            = acc::off::Pick(0x39c, 0x3ec);
const size_t kDoorGenericTypeOffset        = acc::off::Pick(0x2a1, 0x2e1);  // byte → genericdoors.2da row
const size_t kDoorLockedOffset             = acc::off::Pick(0x2c4, 0x304);  // undefined4 (bool)
const size_t kDoorOpenStateOffset          = acc::off::Pick(0x2cc, 0x31c);  // byte
const size_t kDoorDescriptionOffset        = acc::off::Pick(0x3a4, 0x3f4);
const size_t kDoorStaticOffset             = acc::off::Pick(0x3c0, 0x410);  // undefined4 (UTD Static flag)
const size_t kDoorTransitionDestOffset     = acc::off::Pick(0x3c8, 0x418);
// DUPLICATE of kCreatureStatsPointerOffset in engine_offsets_fields.h — same
// CSWSCreature.creature_stats field under a second name. Kept in sync by hand;
// worth collapsing to one declaration. KOTOR 2 value from the seeded
// kotor2_steam_aspyr.db.
const size_t kCreatureStatsPtrOffset       = acc::off::Pick(0xa74, 0x1198);  // CSWSCreatureStats*
// CSWSCreatureStats.first_name — K2 +0x34 (LastName +0x3c, Description +0x58),
// witnessed in the stats save (0x006b3d10).
const size_t kCreatureStatsFirstNameOffset = acc::off::Pick(0x14, 0x34);
const size_t kPlaceableLocNameOffset       = acc::off::Pick(0x228, 0x268);
// CSWSItem.localized_name — KOTOR 2 +0x2c0, observed in its upgrade
// OnEnterSlot, which builds the installed mod's display name from
// `installed[slot] + 0x2c0` exactly where KOTOR 1's builds it from
// `installed[slot] + 0x280`. Note the delta is +0x40, NOT the +4 the shallow
// CSWSObject fields take — CSWSItem's own body grew as well, so the sibling
// LocName offsets below (placeable / waypoint / trigger) must NOT be assumed to
// follow this one; each needs its own witness.
const size_t kItemLocNameOffset            = acc::off::Pick(0x280, 0x2c0);
const size_t kWaypointLocNameOffset        = acc::off::Pick(0x238, 0x278);
const size_t kTriggerLocNameOffset         = acc::off::Pick(0x228, 0x268);

// Pillar 4 sub-state. K2 placeable values from CSWSPlaceable load
// (0x005ef570): Useable +0x380, HasInventory +0x37c, item repository +0x3c4.
const size_t kPlaceableUsableOffset        = acc::off::Pick(0x328, 0x380);  // "Useable" GFF flag
// "HasInventory" GFF flag. Decompiling CSWSPlaceable::LoadPlaceable shows
// ReadFieldBYTE("HasInventory") is stored to +0x324 (the Ghidra struct
// mislabels +0x334 as has_inventory — that field is something else and
// reads 0 even on real loot containers). CSWSPlaceable::OpenInventory
// gates the container-GUI open on this same +0x324 != 0, then derefs
// item_repository, confirming it as the authoritative "is a lootable
// container" flag.
const size_t kPlaceableHasInventoryOffset  = acc::off::Pick(0x324, 0x37c);
// CSWSPlaceable.item_repository @+0x36c → CItemRepository. The repo's
// live item count sits at +0x10 (items_list @+0xc). Confirmed by
// decompiling CItemRepository::GetItemInRepository / ItemListGetItem /
// CalculateContentsWeight — all loop `i < this->item_count` over
// `items_list[i]`. Reading the count is a single dword load, so the
// emptiness test is O(1) (no list walk, no per-item handle resolve).
const size_t kPlaceableItemRepositoryOffset = acc::off::Pick(0x36c, 0x3c4);
const size_t kItemRepositoryItemCountOffset = acc::off::Same(0x10);
const size_t kWaypointHasMapNoteOffset     = acc::off::Pick(0x228, 0x268);
// CSWSTrigger.transition_destination — a CExoLocString holding the
// human-readable "to X" exit label (e.g. "Zur Oberstadt"). Read as a
// LocString by GetObjectName's Trigger case. IsTransitionTrigger tests
// presence structurally: inline text pointer at +0 (with length at +4),
// else the +4 slot is the TLK strref, where the GFF sentinel 0xFFFFFFFF
// means "no destination" (trap / banter / script trigger).
// CSWSTrigger band took a uniform +0x40 shift on K2 (localized_name
// 0x228→0x268 witnessed in the trigger load 0x00620250); transition_destination
// inherits it. DERIVED from the band shift, not directly witnessed — confirm
// against a K2 trigger-load "TransitionDestination" read before trusting the
// transition-exit label on K2.
const size_t kTriggerTransitionDestOffset  = acc::off::Pick(0x30c, 0x34c);  // CExoLocString

// BioWare-authored map-note labels (CSWSWaypoint SIZE=0x240). K2 values from
// the waypoint save (0x00629040): HasMapNote +0x268, MapNoteEnabled +0x26c,
// MapNote(Loc) +0x270 — the +0x40 waypoint-band shift.
const size_t kWaypointMapNoteEnabledOffset = acc::off::Pick(0x22c, 0x26c);
const size_t kWaypointMapNoteLocOffset     = acc::off::Pick(0x230, 0x270);

// Trap ("mine") detected-by bookkeeping — engine model in
// docs/llm-docs/mine-trap-model.md. Each trappable kind carries a
// CExoArrayList<ulong> of SERVER object ids that have detected its trap
// (party detection adds every party member at once). Layout at the given
// offset: data pointer at +0, count at +4. Offsets verified against the
// CSWSCreature::UpdateMineCheck decompile (the engine's own consumer).
// K2 witnessed 2026-08-02 in the K2 UpdateMineCheck twin 0x0056C310 (found
// via its unique 400.0f/9.0f detect-radius constants; K1 caller decompile
// matches branch for branch). The K2 build calls the un-inlined list
// helpers (contains 0x00476620 / append-unique 0x005210A0) with
// ECX = obj + offset: trigger branch `add ecx,0x2e8`, door `add ecx,0x32c`,
// placeable `add ecx,0x370`. Same {data,count} layout at +0/+4.
const size_t kTriggerTrapDetectedListOffset   = acc::off::Pick(0x2a8, 0x2e8);
const size_t kTriggerIsTrapOffset             = acc::off::Pick(0x2bc, 0x2fc);  // undefined4, != 0 on mines

// CSWSTrigger footprint polygon (world-space Vector array). K2 +0x40 band
// shift (count 0x284→0x2c4, ptr 0x288→0x2c8), witnessed in the trigger save
// (0x00621ba0) Geometry serialization.
const size_t kTriggerGeometryCountOffset      = acc::off::Pick(0x284, 0x2c4);  // int
const size_t kTriggerGeometryOffset           = acc::off::Pick(0x288, 0x2c8);  // Vector*

// Fixed NWScript local-variable table (CSWVarTable: ulong[3] boolean bits
// + byte[8] numbers) embedded at CSWSObject+0x110 (K2 +0x114). This is the
// mislabeled `script_var_table_2` — see persistence-scriptvartable.md. K2
// witnessed in the object serializer thunk (0x00540660: this+0x114 →
// SWVarTable load), which sits one slot above the +0x104 script_var_table.
const size_t kObjectVarTableOffset            = acc::off::Pick(0x110, 0x114);
const size_t kDoorTrapDetectedListOffset      = acc::off::Pick(0x2dc, 0x32c);
const size_t kPlaceableTrapDetectedListOffset = acc::off::Pick(0x318, 0x370);

// Walkmesh wall-edge extraction. Pillar 1 foundation.
//
// CSWSArea.rooms @+0x230 → CSWSRoom*, stride 0x4c, count @+0x268.
// CSWSRoom +0x3c → CSWRoomSurfaceMesh*.
// CSWRoomSurfaceMesh:
//   +0x00 CSWCollisionMesh mesh (embedded 0x88 bytes)
//   +0x88 SurfaceMeshAdjacency* adjacencies (face_count entries)
//   +0x8c CExoArrayList<SurfaceMeshEdge> edges
// CSWCollisionMesh:
//   +0x04 world_coords (int, 1 = vertices already world-space)
//   +0x50 vertex_count, +0x54 vertices (Vector*)
//   +0x58 face_count,   +0x60 face_indices (3 ulongs/face)
//   +0x64 materials (ulong* → surfacemat.2da)
// SurfaceMeshAdjacency.indices[3]: -1 = perimeter (WALL); else edge_id,
// face_id = edge_id / 3.
//
// CSWCollisionMesh::LocalToWorld @0x596aa0 __thiscall(this, out, local).
// BYTES_PURGED=8. Short-circuits when world_coords != 0; we always call.

// The whole surface/collision-mesh block is byte-identical between games —
// witnessed in K2's BWM writer (0x005ea490), which serializes vertex_count
// +0x50 / vertices +0x54 / face_count +0x58 / faces +0x60 / materials +0x64
// exactly where KOTOR 1 does, and CSWSRoom's constructor (0x005ff440) storing
// the surface mesh at +0x3c with adjacencies at +0x88. This is expected:
// walkmesh geometry is base-engine code KOTOR 2 did not touch.
const size_t kRoomSurfaceMeshOffset            = acc::off::Same(0x3c);
const size_t kCollisionMeshVerticesOffset      = acc::off::Same(0x54);
const size_t kCollisionMeshFaceCountOffset     = acc::off::Same(0x58);
const size_t kCollisionMeshFacesOffset         = acc::off::Same(0x60);
const size_t kCollisionMeshMaterialsOffset     = acc::off::Same(0x64);
const size_t kSurfaceMeshAdjacenciesOffset     = acc::off::Same(0x88);

const size_t kWalkmeshFaceStride               = acc::off::Same(0xc);   // 3 × ulong

// K2 twin 0x005EE7B0, found via KOTOR 2 ShowObject's renderDEV door path
// (the same 18-call LocalToWorld pattern as K1's 0x005f9c60) and verified by
// body: identity-copy when world_coords [this+4] is set, else Vector::op+
// with this->position (+0x2c) and quaternion rotate with this->orientation
// (+0x38). Same __thiscall (this, Vector* out, Vector* in) signature.
const uintptr_t kAddrCollisionMeshLocalToWorld = acc::addr::Pick(0x00596aa0, 0x005EE7B0);

namespace acc::engine {

// True when `gameObject`'s trap has been detected by any of the given
// SERVER object ids (pass the current party's ids). Kind-dispatched to
// the per-kind detected-by list (kTriggerTrapDetectedListOffset etc.
// above); false for untrappable kinds, empty lists, null/fault. idCount
// small (party ≤ 4 incl. PC).
bool TrapDetectedByAnyOf(void* gameObject,
                         const uint32_t* ids, int idCount);

// One perimeter walkmesh edge — a triangle side that has no neighbour
// (`SurfaceMeshAdjacency.indices[e] == -1`) and therefore corresponds to a
// physical wall in the room. Endpoints are in WORLD space (LocalToWorld
// already applied at extraction time, so consumers do not need the room's
// CSWCollisionMesh anymore — the edge is self-contained).
struct WallEdge {
    Vector a;
    Vector b;
    int    room_id;       // CSWSArea-local room index (0..room_count-1)
    int    material_id;   // surfacemat.2da row id for the face this edge bounds
};

// Walks every room in `area`, then every face in each room's
// CSWRoomSurfaceMesh, and emits one WallEdge for each triangle side whose
// adjacency is `-1` (perimeter / wall sentinel). Endpoints are transformed
// into world space via CSWCollisionMesh::LocalToWorld so callers can compute
// distances against the player's world position directly.
//
// Seam filtering: when called with a buffer (outBuf != nullptr), the
// raw per-room scan is followed by a pairwise pass that drops "edges"
// where two different rooms emit the same world-space endpoints. K1
// joins rooms through portals/AABB rather than via triangle adjacency,
// so room-boundary triangles in both rooms carry adjacency=-1 and would
// otherwise appear as walls — even though the engine considers them
// walkable. This pass eliminates those phantom walls. Endpoint match
// uses ~1cm tolerance for LocalToWorld float variance.
//
// outBuf non-null + maxEdges > 0: writes up to min(emitted, maxEdges)
//   post-filter edges; returns post-filter count (REAL walls).
// outBuf null OR maxEdges <= 0: returns pre-filter DISCOVERED count
//   for buffer-sizing telemetry. filtered ≤ discovered.
//
// SEH-guarded per room; faults skip that room. Walkmesh is immutable
// per area-load (doors are separate collision meshes), so cache once.
int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges);

// One walkmesh floor triangle, all three corners in WORLD space
// (LocalToWorld applied at extraction, same as WallEdge). The .wok mesh
// is a 2.5D floor surface — no ceilings — so the triangle under an
// (x,y) point IS the floor there; a plane interpolation over the three
// corners gives the exact height even on ramps.
struct FloorTri {
    Vector a;
    Vector b;
    Vector c;
};

// Walks every room's surface mesh and emits every face as a FloorTri.
// No material or adjacency filtering — non-walkable faces (grates,
// hazard strips) still sit at floor height and are valid height
// witnesses. Returns the discovered count; writes up to maxTris.
// SEH-guarded per room, same contract as BuildAreaWallCache.
int BuildAreaFloorCache(void* area, FloorTri* outBuf, int maxTris);

// Same-floor z tolerance for the segment-vs-wall test below (world units
// = metres). A wall edge whose z at the crossing point differs from the
// ray's z by more than this is on another floor and is not a blocker —
// see the 3D guard in SegmentCrossesWalkmesh. 2m clears step/slope edge
// authoring (~2.25m faces dedup elsewhere) while excluding genuine
// inter-deck stacks. Confirmed against the engine nav graph: a 3D-aware
// crosscheck found zero nav-edge/wall crossings across 6 diverse areas
// (wall cache is phantom-free), and every runtime over-block occurred at
// elevated z against ground-floor walls — i.e. pure 2D-projection noise
// this tolerance removes. Shared with room_topology::LogNavWallCrossings.
constexpr float kWallCrossZToleranceM = 2.0f;

// Segment-vs-perimeter test in the XY plane, with an optional 3D guard: a
// wall is only a blocker when its edge sits within kWallCrossZToleranceM of
// the ray at the crossing point. Without the guard, a wall on a different
// floor whose 2D projection happens to lie on a→b would falsely block
// (the engine's nav graph proves these are not real obstacles). Returns
// closest qualifying hit along a→b (smallest t in [0,1]).
//
// ignoreZ: when true, the z guard is skipped and the test is pure 2D.
// Callers whose endpoints carry no trustworthy z must set this — notably
// the waypoint smoother, which feeds 2D nav-graph nodes (no height field;
// see PathPoint layout in guidance_pathfind.h). A 2D test there fails safe:
// it can only over-block (keep a redundant waypoint), never miss a real
// wall and route through it. The default (false) keeps the guard for the
// room-shape/cursor consumers, where a phantom cross-floor wall would
// instead corrupt a spoken description.
//
// False on: null walls, wallCount<=0, a==b. Degenerate edges (a==b)
// skipped.
//
// Pulls from spatial::change_detector::GetCachedWalls; this is the
// consumer surface, not the build path.
bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint,
                            bool ignoreZ = false);

}  // namespace acc::engine
