// Player state readers — position, facing, area. SEH-guarded.
//
// Chain:
//   *kAddrAppManagerPtr → AppManager → +0x4 CClientExoApp →
//   CClientExoApp::GetPlayerCreature @0x5ed540 → CSWCCreature* →
//   server_object @+0xf8 → CSWSObject* → position @+0x90, orientation
//   @+0x9c (z=0 for facing — engine 2D unit vector, not quaternion).
//
//   CSWSObject::GetArea @0x4cb120 → CSWSArea*.
//
// The +0x4 between AppManager and CClientExoApp wasn't in Q1's prose but
// every caller of GetPlayerCreature uses it (verified at 0x5fba8d,
// 0x60541a, 0x605451). Server layout (+0x90/+0x9c) is the authoritative
// source — client layout (+0x24/+0x30) is a parallel cache.
//
// All functions return false/nullptr cleanly on null at any chain link
// or SEH fault.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"

namespace acc::engine {

bool GetPlayerPosition(Vector& out);

// 2D unit vector — engine zeroes z on object facing.
bool GetPlayerFacing(Vector& out);

// 0° = +X = east, CCW positive, [0, 360). False when no player loaded or
// heading is degenerate (transient during spawn).
bool GetPlayerYawDegrees(float& out);

// Opaque CSWSArea*.
void* GetPlayerArea();

// World position of the gameplay camera. The orbital camera follows the
// player at ~3m offset and orbits on A/D; (player - camera) is the
// look direction without dead-reckoning (camera always looks at the
// character).
//
// Chain through +0x18 CSWCModule + +0x40 Camera* + +0x7C Vector (Gob
// embedded at +0x04, position at Gob+0x78).
bool GetCameraPosition(Vector& out);

// Opaque CSWSCreature* for callers that need to thiscall on the player
// creature (e.g. AddMoveToPointAction) without redoing the chain walk.
void* GetPlayerServerCreature();

// Reads CClientExoAppInternal::player_character_name (+0x294) via
// CClientExoApp::GetPlayerCharacterName @0x5edab0. Engine owns the
// CExoString lifetime; we copy bytes into outBuf.
//
// Why not GetObjectName: the PC's CSWSCreatureStats.first_name is
// empty in vanilla saves (chargen writes to the client app slot, not
// the stats field), so the generic creature-name path falls through
// to `tag` which is also empty for the PC. Companion NPCs have
// populated first_name and resolve normally.
//
// False on chain fault or empty stored CExoString (main menu / pre-chargen).
bool GetPlayerCharacterName(char* outBuf, size_t bufSize);

// Active LEADER (Tab cycles which party member is leader). Mirrors the
// GetPlayerServerObject chain but stops at the client pointer.
void* GetClientLeader();

// Three resolution paths, in order:
//   1. GetObjectDisplayNameByHandle — engine's universal localised-name
//      accessor (same one sighted UI uses). "Trask Ulgo" etc.
//   2. Direct stats.first_name via ExtractTextOrStrRef (pure memory).
//   3. CClientExoApp::GetPlayerCharacterName slot — the PC's chargen name.
//      The PC's stats.first_name is empty in vanilla saves, so this is
//      the canonical path when leader == PC.
//
// MUST be gated on GetPlayerPosition. Path 1 routes through
// CClientExoApp::GetObjectName, which writes through a stack CExoString
// and trips /GS → uncatchable __fastfail on the PC handle during the
// chargen→world transient. GetPlayerPosition closes that window.
//
// outBuf is always NUL-terminated on entry (even on early-return).
bool GetActiveLeaderName(char* outBuf, size_t bufSize);

// Wraps CSWPlayerControl::SetEnabled @0x006792e0. Two writes behind
// it: CSWPlayerControl.enabled at +0xc, and CSWCCreature::SwitchMode
// (creature mode 0=AI / 1=player).
//
// armAutoRestore=true (autowalk shape): TickPlayerInputRestore flips
// control back when the AI action queue drains, never enqueues (grace), or
// stalls with no movement. No time cap while the PC is progressing, so long
// walks finish naturally. A repeat disable while a session is active does
// NOT re-arm (no window extension — the janicebug livelock guard).
//
// armAutoRestore=false (view mode shape): timer stays inert; caller
// owns the matching SetPlayerInputEnabled(true). Missing it freezes
// the player permanently (modulo a future disable + auto-restore).
//
// Explicit SetPlayerInputEnabled(true) always clears the session.
bool SetPlayerInputEnabled(bool enabled, bool armAutoRestore = true);

// Restore tick — queue-drain / ceiling driven. Cheap when idle (one flag
// check); reads the action queue only while a disable session is active.
void TickPlayerInputRestore();

// Diagnostic tick — logs player action-queue depth changes (delta only).
// Used to validate queue behaviour across combat / autowalk / dialog.
void TickActionQueueDiag();

// Walks CServerExoApp.party_table @+0x1b770 (via GetServerPartyTable):
//   +0x0 pt_num_members  (active followers, 0..2 in normal play — the PC
//                          is the implicit leader and is NOT counted here)
//   +0x4 pt_member_ids[] (NPC *roster slot indices* 0..8, e.g. 2=Carth,
//                          6=Mission — NOT object handles)
//
// Each slot index is resolved to the live creature's object handle via
// CSWPartyTable::GetNPCObject, so outHandles are real handles comparable to
// GetObjectHandle(). Slots that don't resolve to a live creature are
// skipped. Returns count written; 0 on early-init / SEH / empty roster.
int GetPartyMembers(uint32_t* outHandles, int maxCount);

// Active CSWPartyTable*. Exposed so callers that need NPC-slot
// availability/selectability can hit the engine's thiscalls directly.
void* GetServerPartyTable();

// CSWPartyTable::GetIsNPCAvailable @0x005636B0. True iff companion at
// slot is recruited and in the active roster.
bool PartyTableIsNPCAvailable(int npcSlot);

// CSWPartyTable::GetNPCSelectability @0x005637C0. True iff currently
// allowed to pick — false on story-locked but available companions.
bool PartyTableIsNPCSelectable(int npcSlot);

// Walks GetNPCObject → GetCreatureByGameObjectID → universal-name
// accessor. False on unavailable / unresolved / SEH; outBuf is always
// NUL-terminated.
bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize);

}  // namespace acc::engine

// *kAddrAppManagerPtr → AppManager; +0x4 → CClientExoApp.
constexpr uintptr_t kAddrAppManagerPtr           = 0x007A39FC;
constexpr size_t    kAppManagerClientAppOffset   = 0x4;

constexpr uintptr_t kAddrGetPlayerCreature = 0x005ED540;
constexpr uintptr_t kAddrCSWSObjectGetArea = 0x004CB120;

// CSWCObject.server_object — same for every client object.
constexpr size_t kClientObjectServerObjectOffset = 0xf8;

constexpr size_t kServerObjectPositionOffset    = 0x90;
constexpr size_t kServerObjectOrientationOffset = 0x9c;

// CClientExoApp facade is 8 bytes (vtable@0, internal@4);
// CClientExoAppInternal carries the real state.
constexpr size_t kClientExoAppInternalOffset = 0x4;

// CClientExoAppInternal.player_control @+0x2a0.
constexpr size_t kClientAppPlayerControlOffset = 0x2a0;

constexpr uintptr_t kAddrCSWPlayerControlSetEnabled = 0x006792E0;

// __thiscall(void) → CExoString*. Backed by player_character_name @+0x294.
constexpr uintptr_t kAddrCClientExoAppGetPlayerCharacterName = 0x005EDAB0;

// CServerExoApp mirrors the facade/internal split:
//   public facade = 8 bytes (vtable@0, internal@4).
//   CSWPartyTable is embedded in the INTERNAL at +0x1b770.
// Verified via CServerExoApp::GetPartyTable @0x004aee70 decompile
// (MOV EAX, [ECX+4]; ADD EAX, 0x1b770; RET).
//
// Earlier walks read from facade+0x1b770 — wrong; returned random heap
// (all 1s) for avail/selectable arrays. Internal+0x1b770 matches the
// per-portrait flag word the engine sets in OnPanelAdded.
constexpr size_t    kAppManagerServerOffsetPlayer  = 0x8;
constexpr size_t    kServerExoAppInternalOffset    = 0x4;
constexpr size_t    kServerInternalPartyTableOffset = 0x1b770;
// Legacy alias kept for source compatibility while old (incorrect) path
// callers are audited.
constexpr size_t    kServerExoAppPartyTableOffset  = kServerInternalPartyTableOffset;
constexpr size_t    kPartyTableNumMembersOffset    = 0x0;
constexpr size_t    kPartyTableMemberIdsOffset     = 0x4;
constexpr int       kPartyTableMaxMembers          = 11;

// CSWPartyTable thiscalls used by the PartySelection extractor (same
// accessors CSWGuiPartySelection::OnPanelAdded uses to build portraits).
constexpr uintptr_t kAddrCSWPartyTableGetIsNPCAvailable     = 0x005636B0;
constexpr uintptr_t kAddrCSWPartyTableGetNPCSelectability   = 0x005637C0;
constexpr uintptr_t kAddrCSWPartyTableGetNPCObject          = 0x00564700;

// PartySelection renders 9 portraits in a 3x3 grid.
constexpr int kPartyRosterSlotCount = 9;
