// Player state readers — position, facing, area.
//
// Layer: engine/ (pure read-side helpers, SEH-guarded; no engine re-entry,
// no menu-side state). Phase 1 foundation; consumers land in Phase 2+.
//
// Address chain (investigation Q1 + Phase 1 lay-off 4 xref-trace
// re-verification, 2026-05-03):
//
//   *kAddrAppManagerPtr → AppManager wrapper
//     → wrapper +0x4 → CClientExoApp* (real app instance)
//       → CClientExoApp::GetPlayerCreature() @0x5ed540 → CSWCCreature* (client)
//         → server_object @+0xf8 → CSWSObject* (server)
//           → position @+0x90 (Vector), orientation @+0x9c (Vector, z=0)
//
//   CSWSObject::GetArea() @0x4cb120 → area handle (CSWSArea*; opaque here)
//
// The +0x4 indirection between APP_MANAGER_PTR and CClientExoApp wasn't
// documented in investigation Q1's prose ("APP_MANAGER_PTR (0x7a39fc) →
// CClientExoApp instance" implied a single deref). DumpBytes at three
// independent callers of GetPlayerCreature (0x5fba8d, 0x60541a, 0x605451)
// all show the canonical pattern:
//   8b 0d fc 39 7a 00      MOV ECX, [0x007A39FC]    ; appManager
//   8b 49 04               MOV ECX, [ECX+0x4]       ; → CClientExoApp*
//   e8 ?? ?? ?? ??         CALL GetPlayerCreature
// — so the +0x4 indirection is real and consistent.
//
// We deliberately use the server-side CSWSObject layout (+0x90 / +0x9c) and
// not the client CSWCObject layout (+0x24 / +0x30) — the two are independent
// per investigation Q1 and the server is the authoritative source.
//
// Functions return false / nullptr cleanly when any pointer in the chain is
// null (main menu, between-area loads, early DLL attach). Every dereference
// is __try-wrapped so corrupted/freed engine state can't fastfail the
// process — same convention as engine_reads.h.
//
// Functions are namespaced under `acc::engine`; addresses and offsets at
// file scope, matching engine_manager.h / engine_offsets.h conventions.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::engine {

// World position of the controlled player creature.
// Returns false when no player creature is loaded (main menu, area-load
// transitions) or when the address chain faults (engine teardown).
bool GetPlayerPosition(Vector& out);

// Heading vector of the controlled player creature. The engine zeroes the
// z component for object facing — see Q1 (heading is a 2D unit vector,
// not a quaternion). We propagate that convention.
bool GetPlayerFacing(Vector& out);

// Yaw in degrees, [0, 360), with 0° = +X = east, CCW positive. Matches
// ExecuteCommandGetFacing's normalization. Returns false for the same
// reasons as GetPlayerFacing, plus when the heading is degenerate
// (x=y=0, which the engine only emits transiently during spawn).
bool GetPlayerYawDegrees(float& out);

// Returns the player's current area as an opaque CSWSArea* handle, or
// nullptr if no player is loaded. Caller treats the pointer opaquely until
// engine_area lands in a later phase (consumers don't exist yet).
void* GetPlayerArea();

// World-space position of the gameplay camera. KOTOR's orbital camera
// follows the player at a ~3m offset (and orbits around the player when
// A/D is pressed), so this position is distinct from the player's. The
// pair (player_pos, camera_pos) gives us the camera's look direction
// without dead-reckoning: `normalize(player_pos - camera_pos)` is the
// direction the camera is currently pointing, because KOTOR's orbital
// camera always looks at the character.
//
// Chain walk:
//   *kAddrAppManagerPtr  -> AppManager
//     +0x04             -> CClientExoApp
//       +0x04           -> CClientExoAppInternal
//         +0x18         -> CSWCModule
//           +0x40       -> Camera*
//             +0x7C     -> Vector position (Gob.position at Gob+0x78,
//                          Gob embedded in Camera at Camera+0x04)
//
// Verified live 2026-05-11 via the probe_camera_state F12 probe:
// three samples around a stationary player showed camera positions
// 3.2m / 3.2m / 1.1m from the player and visibly orbiting around the
// player as A/D was held.
//
// Returns false on null at any chain link or SEH fault. The output
// is in the same world frame as GetPlayerPosition.
bool GetCameraPosition(Vector& out);

// Returns the player's server-side creature (CSWSCreature*, downcastable
// from CSWSObject*) as an opaque pointer, or nullptr if no player is
// loaded. Same chain walk as the position/facing readers — exposed so
// other engine_* / guidance_* consumers can call thiscall methods on the
// player creature (e.g. AddMoveToPointAction) without duplicating the
// AppManager → CClientExoApp → GetPlayerCreature → server_object chain.
void* GetPlayerServerCreature();

// Reads the player's chosen character name (the first name entered at
// chargen). Wraps `CClientExoApp::GetPlayerCharacterName @0x5edab0`,
// which returns the live `CExoString*` backed by
// `CClientExoAppInternal::player_character_name @+0x294`. Bytes are
// copied into the caller's buffer; the engine-owned CExoString stays
// alive for the session.
//
// Why a dedicated path rather than going through `GetObjectName` on the
// player creature: the player's `CSWSCreatureStats.first_name`
// CExoLocString is empty in vanilla saves (chargen writes the chosen
// name to `CClientExoAppInternal::player_character_name`, not the stats
// field), so the generic creature-name path falls all the way through
// to `tag` which is also empty for the PC. Confirmed live 2026-05-05
// via diag_engine_select Tab logs: `id=0x7fffffff name=[]` for the
// player creature, while companion NPCs (e.g. Trask, `name=[end_trask]`)
// resolve normally through the generic path.
//
// Returns false on chain failure (no app, SEH-caught fault) OR when the
// stored CExoString is empty (main-menu / pre-chargen state).
bool GetPlayerCharacterName(char* outBuf, size_t bufSize);

// Resolve the *active leader's* client-side CSWCCreature. Mirrors the
// chain in GetPlayerServerObject but stops at the client pointer (no
// chain through +0xf8 to the server object). Walks AppManager →
// CClientExoApp → GetPlayerCreature() (which the engine wires to the
// currently-controlled party member, *not* the chargen PC — Tab-swapping
// to Trask makes this return Trask's CSWCCreature, as confirmed by the
// DiagSelect Tab probe). Returns nullptr at any null link or fault.
void* GetClientLeader();

// Resolve the *currently controlled* leader's display name (Tab cycles
// which party member is leader — companions or PC). Three resolution
// paths, tried in order:
//   1. GetObjectDisplayNameByHandle on the leader's handle — engine's
//      universal localized-name accessor, the same one sighted UI uses.
//      Gives "Trask Ulgo", "Carth Onasi", etc. for companions.
//   2. Direct stats.first_name read via ExtractTextOrStrRef (pure
//      memory path, no engine accessor). Covers companion saves where
//      Path 1 returns empty.
//   3. CClientExoAppInternal::player_character_name slot via
//      CClientExoApp::GetPlayerCharacterName — the PC's chargen-set
//      name. The PC's stats.first_name is empty in vanilla saves
//      (see project_pc_name_lives_in_client_exoapp) so this is the
//      canonical path when leader == PC.
//
// Callers MUST gate on GetPlayerPosition before invoking — the engine
// accessor on Path 1 routes through CClientExoApp::GetObjectName which
// writes through a stack CExoString and trips /GS → uncatchable
// __fastfail on the PC handle during the chargen→world transient
// (bisected 2026-05-19). GetPlayerPosition closes that window.
//
// Returns true on non-empty name written to outBuf. Buffer is always
// NUL-terminated on entry (outBuf[0] = '\0' even on early-return).
bool GetActiveLeaderName(char* outBuf, size_t bufSize);

// Toggle the per-tick player-input movement clobber. enabled=true =
// player drives the creature directly (vanilla); enabled=false = input
// loop skips the per-tick movement override, AI actions execute
// unimpeded. Wraps CSWPlayerControl::SetEnabled @ 0x006792e0 — two
// writes happen behind the call: (1) CSWPlayerControl.enabled at +0xc,
// (2) CSWCCreature::SwitchMode on the live player creature, which
// updates the creature's mode tag (0=AI, 1=player). See memory entry
// `project_player_control_toggle.md` for the RE.
//
// Returns false on chain failure (no app, no player_control, SEH-caught
// fault). True if the call dispatched.
//
// Auto-restore (autowalk lifecycle): when enabled=false and
// armAutoRestore=true (default), arms a module-static timer that flips
// back to enabled=true after 3 seconds via TickPlayerInputRestore().
// Each new disable extends the window. This is the autowalk shape —
// "disable for one engine dispatch, auto-restore so we don't strand
// the player if the dispatch faults / the user moves on".
//
// Sustained-disable (view mode lifecycle): callers needing the disable
// to last until they explicitly re-enable should pass armAutoRestore=
// false. The timer stays inert; the caller is responsible for the
// matching SetPlayerInputEnabled(true) — failing to do so leaves the
// player permanently frozen (modulo a future disable + auto-restore).
//
// Calling SetPlayerInputEnabled(true) always clears the timer
// regardless of armAutoRestore — explicit re-enable wins.
bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore = true);

// Per-tick auto-restore. Call from OnUpdate. No-op when no disable
// session is active. Cheap when idle (one timestamp compare).
void TickPlayerInputRestore();

// Snapshot the active party-member roster. Walks
// `CServerExoApp.party_table @+0x1b770` (CSWPartyTable):
//   pt_num_members @+0x0  — current member count (1..3 in normal play)
//   pt_member_ids[11] @+0x4 — handles to the member creatures, index 0
//     is the chargen PC, then the recruited companions in roster order.
//
// Writes up to `maxCount` handles into `outHandles` and returns the
// actual count (clamped to `maxCount`). 0 on early-init / SEH fault /
// when pt_num_members is implausible (>11). Handles can then be
// resolved via engine_area::ResolveServerObjectHandle to reach the
// server creature for combat_round walks etc.
//
// Handle namespace: pt_member_ids are server-side game object ids
// (low bit; high-bit-clear). The chain matches every other server-
// side handle reader we have. Callers needing the client-side variant
// can pass the result through ResolveClientObjectHandle directly.
int GetPartyMembers(uint32_t* outHandles, int maxCount);

// Returns the active server-side CSWPartyTable*, or nullptr at any null
// link / SEH fault. Same chain walk GetPartyMembers uses, exposed so
// callers needing more than the active-roster handles (NPC-slot
// availability, selectability) can invoke the engine's CSWPartyTable
// thiscalls directly.
void* GetServerPartyTable();

// Wraps CSWPartyTable::GetIsNPCAvailable @0x005636B0. Returns true if
// companion at roster slot `npcSlot` (0..8) has been recruited and is
// part of the player's active roster (i.e. the panel-side OnPanelAdded
// loop would resolve a creature for this slot). Returns false on bogus
// slot index, null table, or SEH fault.
bool PartyTableIsNPCAvailable(int npcSlot);

// Wraps CSWPartyTable::GetNPCSelectability @0x005637C0. Returns true if
// the engine currently *allows* the player to pick this companion right
// now — i.e. the slot is not story-locked. A recruited but
// non-selectable companion (e.g. busy with a forced plot subroutine)
// returns false here but true from PartyTableIsNPCAvailable.
bool PartyTableIsNPCSelectable(int npcSlot);

// Resolves the display name for companion `npcSlot` by walking the
// engine's GetNPCObject → CServerExoApp::GetCreatureByGameObjectID →
// universal-name accessor chain. Returns true on non-empty name; false
// when the slot is unavailable, the creature can't be resolved, or any
// part of the chain faults under SEH. outBuf is always NUL-terminated.
bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize);

}  // namespace acc::engine

// AppManager wrapper. *kAddrAppManagerPtr holds an AppManager*; the live
// CClientExoApp* lives at *(appManager + kAppManagerClientAppOffset).
// Both layers are nullptr early in DLL attach until the engine populates
// them.
constexpr uintptr_t kAddrAppManagerPtr           = 0x007A39FC;
constexpr size_t    kAppManagerClientAppOffset   = 0x4;

// CClientExoApp::GetPlayerCreature — __thiscall(void) -> CSWCCreature*.
// SARIF CONFIRMED.
constexpr uintptr_t kAddrGetPlayerCreature = 0x005ED540;

// CSWSObject::GetArea — __thiscall(void) -> CSWSArea*. SARIF CONFIRMED.
constexpr uintptr_t kAddrCSWSObjectGetArea = 0x004CB120;

// CSWCObject.server_object — pointer to the matching server-side CSWSObject.
// Same offset for every client object (creatures, items, doors, etc.).
constexpr size_t kClientObjectServerObjectOffset = 0xf8;

// CSWSObject.position / orientation. Server-side layout.
constexpr size_t kServerObjectPositionOffset    = 0x90;
constexpr size_t kServerObjectOrientationOffset = 0x9c;

// CClientExoApp.internal @+0x4 → CClientExoAppInternal*. Per Lane's
// type DB, the public CClientExoApp facade is 8 bytes (vtable@0,
// internal@4); CClientExoAppInternal carries the real state.
constexpr size_t kClientExoAppInternalOffset = 0x4;

// CClientExoAppInternal.player_control @+0x2a0 → CSWPlayerControl*.
// Per Lane's type DB.
constexpr size_t kClientAppPlayerControlOffset = 0x2a0;

// CSWPlayerControl::SetEnabled — __thiscall(int) -> void. Single named
// API that gates per-tick input-driven movement and toggles the
// creature's mode (player/AI/driving). See header doc on
// SetPlayerInputEnabled for the full xref.
constexpr uintptr_t kAddrCSWPlayerControlSetEnabled = 0x006792E0;

// CClientExoApp::GetPlayerCharacterName — __thiscall(void) -> CExoString*.
// SARIF CONFIRMED ("CExoString * __thiscall GetPlayerCharacterName(void)").
// Returns the live CExoString* for the chargen-set player name, backed by
// CClientExoAppInternal::player_character_name @+0x294.
constexpr uintptr_t kAddrCClientExoAppGetPlayerCharacterName = 0x005EDAB0;

// CServerExoApp → CServerExoAppInternal → CSWPartyTable. The public
// CServerExoApp facade is 8 bytes (vtable@0, internal@4), mirroring the
// CClientExoApp / CClientExoAppInternal split — verified live via the
// `CServerExoApp::GetPartyTable @0x004aee70` decompile (`MOV EAX,
// [ECX+4]; ADD EAX, 0x1b770; RET`), which only matches the layout when
// the table is embedded inside the internal at +0x1b770.
//
// Earlier party-table walks in this file (commit 2026-05-15) skipped
// the +0x4 indirection on the server side because the `pt_num_members`
// happened to read as 0 (consistent with "no active party" early in
// the game). The full PartySelection panel walk exposed the mistake:
// reading from CServerExoApp+0x1b770 returned random heap (all 1s) for
// the avail/selectable arrays, while reading from
// CServerExoAppInternal+0x1b770 matches the per-portrait flag word the
// engine itself sets in OnPanelAdded.
constexpr size_t    kAppManagerServerOffsetPlayer  = 0x8;  // mirror of engine_area
constexpr size_t    kServerExoAppInternalOffset    = 0x4;
constexpr size_t    kServerInternalPartyTableOffset = 0x1b770;
// Legacy alias used by GetPartyMembers / party_cache — kept for source
// compatibility while we audit which callers care about the old
// (incorrect) path vs. the corrected chain.
constexpr size_t    kServerExoAppPartyTableOffset  = kServerInternalPartyTableOffset;
constexpr size_t    kPartyTableNumMembersOffset    = 0x0;
constexpr size_t    kPartyTableMemberIdsOffset     = 0x4;
constexpr int       kPartyTableMaxMembers          = 11;

// CSWPartyTable thiscalls used by the PartySelection portrait extractor.
// `GetIsNPCAvailable(int slot)` / `GetNPCSelectability(int slot)` are the
// same accessors CSWGuiPartySelection::OnPanelAdded calls when building
// each portrait button (decompile at 0x006beeb0). `GetNPCObject(int slot,
// int instanceIndex, int someFlag)` returns the server-side game object
// id of the companion's live creature (or 0 if not spawned).
//
// `kAddrCServerExoAppGetCreatureByGameObjectID` resolves that id to a
// CSWSCreature*, mirroring the OnPanelAdded chain. We don't currently
// dereference the result — display-name resolution goes through the
// universal handle accessor in engine_area — but the address is kept
// here for symmetry with the rest of the chain.
constexpr uintptr_t kAddrCSWPartyTableGetIsNPCAvailable     = 0x005636B0;
constexpr uintptr_t kAddrCSWPartyTableGetNPCSelectability   = 0x005637C0;
constexpr uintptr_t kAddrCSWPartyTableGetNPCObject          = 0x00564700;

// Maximum companion roster slot. The PartySelection panel renders 9
// portraits in a 3x3 grid (CSWGuiPartySelection.party_data[9] per
// the Ghidra struct), one per roster slot.
constexpr int kPartyRosterSlotCount = 9;
