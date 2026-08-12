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

#include "engine_app.h"
#include "engine_offsets.h"
#include "engine_rebase.h"
#include "engine_offsets_select.h"

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

// Camera heading in the engine yaw frame (radians, 0=+X/East, CCW+),
// read from the module camera's orientation quaternion at Camera+0x88
// (Gob+0x84). The engine Quaternion layout is w,x,y,z (w first); yaw is
// the heading of the quaternion-derived forward vector:
//   fwd.x = 2(x*y - z*w),  fwd.y = 1 - 2(x*x + z*z),  yaw = atan2(fwd.y, fwd.x)
// This matches the position-derived (player - camera) heading exactly and
// stays valid when the camera sits directly above the player (where the
// position-derived path degenerates). Returns false on null chain / SEH.
// Convention + engine Yaw() @0x4a9f40 verified by decompile 2026-06-11.
bool GetCameraYawRadians(float& outRad);

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

// KOTOR 2 only: the client creature in party slot `slot` (0..2) — the slot
// space the engine's medical-item target-pick consumes (its handler resolves
// the picked slot through this same accessor; slot 0 = the controlled
// leader, witnessed via PopulateMenus building the action columns from
// slot 0). nullptr on KOTOR 1, empty slot, or fault.
void* GetPartyCreatureBySlotK2(int slot);

// CSWCCreature* → its CSWSCreature*, game-appropriately: KOTOR 1 reads the
// tested server_object field, KOTOR 2 calls the engine's own
// CSWCCreature::GetServerCreature (the client layout shifted there — same
// dual path GetPlayerServerObject uses for the leader). nullptr on fault.
void* ClientToServerCreature(void* clientCreature);

// Active leader's action-queue "append" knob — CSWCCreature.field200_0x440
// bit 0 (the combat-mode bit combat_diag reads as `cm`).
// CSWGuiMainInterface::DoPersonalAction and CSWGuiTargetActionMenu::
// DoTargetAction both branch on it: bit 0 → clear the queue then dispatch
// (replace); bit 1 → skip the clear (append). It is the engine's native
// Shift-held "queue this action" flag. Our synthetic dispatch never goes
// through the engine's shift capture, so the bit sits at its natural value
// (0 out of combat) and every dispatch overwrites the previously queued
// action — set it to 1 just around the dispatch to stack actions, then
// restore.
//
// Sets bit 0 to `on` (0/1) via read-modify-write (other bits preserved).
// Returns the PREVIOUS bit-0 value (0/1) to pass back for restore, or -1 on
// resolution / SEH failure (no write performed).
int SetLeaderQueueModeBit(int on);

// True when ANY party member's per-creature combat bit (CSWCCreature
// field200_0x440 bit 0) is set — the real "is the encounter active" signal.
// The engine's global combat_mode (GetCombatMode) only reflects the controlled
// leader and gets re-synced to the new leader on every Tab, so it reads as
// "combat ended" when you switch to a peaceful member mid-fight; OR-ing the
// per-member bit avoids that. Client-side, SEH-guarded; false on any fault or
// before the party is wired up.
bool IsAnyPartyMemberInCombat();

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

// Player's pending AI-action count (CSWSObject.action_nodes). -1 = unreadable
// this tick (no live creature / fault); 0 = drained; >0 = actions pending.
int GetPlayerActionQueueDepth();

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

// CSWPartyTable.pt_solomode @+0x190 — the flag the engine's own
// ExecuteCommandGetSoloMode @0x00546af0 returns and SetSoloMode @0x00565500
// writes (both decompiled 2026-07-16). False on no party table / fault.
bool GetSoloMode();

// CSWPartyTable::GetIsNPCAvailable @0x005636B0. True iff companion at
// slot is recruited and in the active roster.
bool PartyTableIsNPCAvailable(int npcSlot);

// CSWPartyTable::GetNPCSelectability @0x005637C0. True iff currently
// allowed to pick — false on story-locked but available companions.
bool PartyTableIsNPCSelectable(int npcSlot);

// Resolves a PartySelection roster slot to a display name. Tries the engine's
// live creature via GetNPCObject: first the client-side universal-name
// accessor, then — when that can't see the handle (the Endar Spire slot-0
// occupant Trask is a live server object with no client-side name) — the
// server creature's stats.first_name (else its tag), so the tutorial slot
// names the actual occupant rather than the fixed-roster "Bastila". Falls back
// to the fixed-roster name table when no live creature resolves (companion
// recruited but not in the current module). False on unavailable / unresolved
// / SEH; outBuf is always NUL-terminated.
bool GetPartyNpcNameForSlot(int npcSlot, char* outBuf, size_t bufSize);

}  // namespace acc::engine

// kAddrAppManagerPtr and the client/server hop offsets live in engine_app.h
// (included above) — one home for the whole resolve chain.

// CClientExoApp::GetPlayerCreature (facade). KOTOR 2's facade at 0x0073F450
// is byte-witnessed as `MOV ECX,[this+4]; CALL 0x0078CEE0` — the same
// facade→internal thunk shape KOTOR 1 has, and the internal is identified by
// the dispatcher's free-look case reading the same +0x138 guard field off its
// result that KOTOR 1's case 0xd0 does.
const uintptr_t kAddrGetPlayerCreature = acc::addr::Pick(0x005ED540, 0x0073F450);
// K2 twin found by callee fingerprint (calls the banked GetObjectArray facade
// 0x0051C080 then CGameObjectArray::GetGameObject 0x0053DFB0 on [this+0x90],
// then the +0x28 AsArea vtable slot — K1's body line for line) and by source
// order (sits immediately before GetGender 0x00545460, as K1's does).
const uintptr_t kAddrCSWSObjectGetArea = acc::addr::Pick(0x004CB120, 0x005453C0);

// CSWCObject.server_object — same for every client object, and the offset is
// now WITNESSED identical on both games: KOTOR 2's own
// CSWCObject::GetServerObject (0x007F2540, K1 0x0063D4B0) tests and fills
// `[this+0xf8]`, with the same `[this+0xe4]` detach guard beside it.
//
// This was left Todo through Batch 3c on the theory that every KOTOR 2
// consumer branches to the engine resolver instead. That was wrong:
// ResolveClientObjectHandle reads the field directly, so on KOTOR 2 it read
// through the poison offset, faulted inside its SEH guard and returned null —
// which is why passive narration and the Q/E re-announce logged
// "handle ... failed to resolve, silent" for every hovered/cycled object
// (patch-20260801-225529.log). Establishing the offset fixes both.
const size_t kClientObjectServerObjectOffset = acc::off::Same(0xf8);

// CSWCCreature::GetServerCreature — __thiscall(void) → CSWSCreature*. The
// engine's own client→server resolver (vtable-dispatched internally, so it is
// layout-proof where the raw field read is not). KOTOR 1 entry from Lane's
// XML; KOTOR 2 entry identified from the dispatcher's free-look case, where
// it fills the same GetPlayerCreature → GetServerCreature slot KOTOR 1's
// decompile shows.
const uintptr_t kAddrGetServerCreature = acc::addr::Pick(0x0060FB20, 0x0077D800);

// CSWSObject.Position / .Orientation. KOTOR 2 values from the seeded
// kotor2_steam_aspyr.db. Every CSWSObject field shifts by exactly +4 there
// (AreaId 0x8c→0x90, Position 0x90→0x94, Orientation 0x9c→0xa0) — one 4-byte
// field inserted above them, not a reshuffle.
const size_t kServerObjectPositionOffset    = acc::off::Pick(0x90, 0x94);
const size_t kServerObjectOrientationOffset = acc::off::Pick(0x9c, 0xa0);

// CClientExoAppInternal.player_control @+0x2a0. The facade → internal hop
// itself is engine_app.h's GetClientAppInternal(). K2 witnessed inside the
// real SetInputClass (0x007B3050, the +0x9c writer): its restore-world case
// does `if ([internal+0x2a0]) SetEnabled(1)` — same slot, same object.
const size_t kClientAppPlayerControlOffset = acc::off::Same(0x2a0);

// K2 SetEnabled witnessed by decompile + listing: stores enabled at [this+0xc],
// compares player_id [this+4] against 0x7f000000, resolves the creature via
// CClientExoApp::GetCreatureByGameObjectID (0x0073F550), then SwitchMode(0/1)
// (0x00776DB0) — KOTOR 1's body line for line.
const uintptr_t kAddrCSWPlayerControlSetEnabled = acc::addr::Pick(0x006792E0, 0x00865250);

// __thiscall(void) → CExoString*. Backed by player_character_name @+0x294.
const uintptr_t kAddrCClientExoAppGetPlayerCharacterName = acc::addr::R(0x005EDAB0);

// CServerExoApp mirrors the facade/internal split:
//   public facade = 8 bytes (vtable@0, internal@4).
//   CSWPartyTable is embedded in the INTERNAL at +0x1b770.
// Verified via CServerExoApp::GetPartyTable @0x004aee70 decompile
// (MOV EAX, [ECX+4]; ADD EAX, 0x1b770; RET).
//
// Earlier walks read from facade+0x1b770 — wrong; returned random heap
// (all 1s) for avail/selectable arrays. Internal+0x1b770 matches the
// per-portrait flag word the engine sets in OnPanelAdded. The facade and
// internal hops themselves are engine_app.h's GetServerAppInternal().
// K2 values witnessed in CSWPartyTable::SaveTableInfo (0x005fb1a0): the
// server-internal → party-table hop is +0x1f0b4 (three witnesses); the table
// stores num_members at +0, member ids at +0x8 (the +4 slot became
// num_puppets on K2), and PT_SOLOMODE at member[0x8e] i.e. +0x238.
const size_t    kServerInternalPartyTableOffset = acc::off::Pick(0x1b770, 0x1f0b4);
const size_t    kPartyTableNumMembersOffset    = acc::off::Same(0x0);
const size_t    kPartyTableMemberIdsOffset     = acc::off::Pick(0x4, 0x8);
const size_t    kPartyTableSoloModeOffset      = acc::off::Pick(0x190, 0x238);  // pt_solomode ulong
constexpr int       kPartyTableMaxMembers          = 11;

// CSWPartyTable thiscalls used by the PartySelection extractor (same
// accessors CSWGuiPartySelection::OnPanelAdded uses to build portraits).
//
// K2 twins (2026-08-01, Batch 3, decompile-confirmed): GetNPCObject
// 0x005FAAF0 reproduces K1's body exactly — avail check, cached id at
// table+0x1c+slot*4 vs 0x7f000000, template-load with CSWSCreature
// (0x7f000000,0), the +0x9c dead-check + resurrection branch, cache stamp.
// Bounds are <0xc (K2's 12-NPC roster). Its sibling 0x005FAD70 is the
// K2-only PUPPET variant (array at +0x14c, 3 slots) — do not confuse them.
// GetIsNPCAvailable 0x005FA960 reads the avail array at table+0x4c.
// GetNPCSelectability has NO confirmed K2 twin yet — the shape-adjacent
// 0x005FA9C0 (array at +0x11c) lacks K1's avail gate and 0xff default and
// may be K2's influence accessor, so it stays R(); PartyTableIsNPCSelectable
// declines under its SEH on KOTOR 2.
const uintptr_t kAddrCSWPartyTableGetIsNPCAvailable = acc::addr::Pick(0x005636B0, 0x005FA960);
const uintptr_t kAddrCSWPartyTableGetNPCSelectability = acc::addr::R(0x005637C0);
const uintptr_t kAddrCSWPartyTableGetNPCObject = acc::addr::Pick(0x00564700, 0x005FAAF0);

// PartySelection renders 9 portraits in a 3x3 grid.
//
// KOTOR 2 HAS 12, NOT 9 — read off its CSWGuiPartySelection constructor, which
// builds party_data as 12 elements of 0x478 where KOTOR 1 builds 9 of 0x454.
// This one deliberately stays `constexpr` and stays 9, because it is one of the
// handful of constants C++ will not let go runtime: it sizes the real array
// `kCompanionNamesBySlot[]` in engine_player_party.cpp. Widening it to 12 there
// would leave three null names, and making it runtime does not compile.
//
// So KOTOR 2 needs its own roster table rather than a wider shared one — which
// it needs anyway, since kCompanionNamesBySlot holds KOTOR 1 story characters
// and none of them are in KOTOR 2. Until that exists, the bound is correct for
// KOTOR 1 and merely truncating on KOTOR 2 (slots 9..11 read as unavailable),
// which is the safe direction to be wrong in: no out-of-range name lookup.
constexpr int kPartyRosterSlotCount = 9;
