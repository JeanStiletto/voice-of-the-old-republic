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
// Auto-restore: SetPlayerInputEnabled(false) arms a module-static timer
// that flips back to enabled=true after 3 seconds via
// TickPlayerInputRestore(). Each new disable extends the window. Callers
// that need to restore earlier (SEH fault on the gated dispatch, manual
// cancel) can call SetPlayerInputEnabled(true) directly — idempotent.
bool SetPlayerInputEnabled(bool enabled);

// Per-tick auto-restore. Call from OnUpdate. No-op when no disable
// session is active. Cheap when idle (one timestamp compare).
void TickPlayerInputRestore();

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
