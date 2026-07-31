// AppManager resolve primitives — the root every engine object walk starts at.
//
// Everything the engine owns hangs off one .data global, the CAppManager
// singleton, which carries two facades:
//
//   *kAddrAppManagerPtr → CAppManager → +0x4 CClientExoApp  (UI / client side)
//                                     → +0x8 CServerExoApp  (world / AI truth)
//
// Each facade is an 8-byte public shell (vtable@0, internal@4) whose
// *Internal carries the real state — see project_client_server_architecture
// for which side answers which question.
//
// Why this file exists
// --------------------
// Before Phase 3 this walk was hand-rolled at every site that needed it: SIX
// different names for the same +0x8 hop, three private copies of the
// AppManager pointer, and per-site SEH guards that some callers remembered
// and others did not. engine_subscreen.cpp said the quiet part out loud —
// "we duplicate the constant locally ... because the latter are file-local".
// A missing guard on one of these walks is precisely the F2 crash class, so
// the guard belongs in one place rather than in every caller's good
// intentions.
//
// Every primitive here is SEH-guarded and yields nullptr on a null link or a
// fault, so a caller only ever has to test for nullptr. Callers that go on to
// CALL an engine function through the returned pointer still need their own
// __try around that call — the guard here covers the walk, not what you do
// with the result.
//
// K2 port note: the three constants and the functions below are the entire
// seam. On a KOTOR 2 executable the global's address changes (and possibly
// the hop offsets); nothing else in the codebase has to know.

#pragma once

#include <cstddef>
#include <cstdint>
#include "engine_offsets_select.h"
#include "engine_rebase.h"

// CAppManager* — a .data global, so PickGlobal rather than R(): .data is
// byte-stable across the KOTOR 1 builds and is never rebased (engine_rebase.h,
// and the .data section note in engine_offsets_addresses.h).
//
// KOTOR 2 value from KPatchManager's seeded kotor2_steam_aspyr.db
// (APP_MANAGER_PTR), whose KOTOR 1 value matches this one exactly. The engine's
// whole globals block relocated as a unit — every pointer the two databases
// share moved by the same 0x2A1AA8.
const uintptr_t kAddrAppManagerPtr = acc::addr::PickGlobal(0x007A39FC, 0x00A1B4A4);

// CAppManager.client @+0x4 → CClientExoApp*, .server @+0x8 → CServerExoApp*.
// Identical in both games — from the same seeded database, whose KOTOR 1
// values also match ours exactly. That agreement between two independent
// reverse-engineering efforts is what makes its KOTOR 2 column trustworthy.
const size_t kAppManagerClientAppOffset = acc::off::Same(0x4);
const size_t kAppManagerServerAppOffset = acc::off::Same(0x8);

// Both facades are 8 bytes (vtable@0, internal@4) and the *Internal carries
// the state. Server side verified via CServerExoApp::GetPartyTable @0x004aee70
// (MOV EAX,[ECX+4]; ADD EAX,...); the client split is the same shape.
const size_t kClientExoAppInternalOffset = acc::off::Todo(0x4);
const size_t kServerExoAppInternalOffset = acc::off::Todo(0x4);

// The client side continues into the world view:
//   CClientExoAppInternal → +0x18 CSWCModule → +0x40 Camera.
const size_t kClientInternalModuleOffset = acc::off::Todo(0x18);
const size_t kCSWCModuleCameraOffset     = acc::off::Todo(0x40);

namespace acc::engine {

// *kAddrAppManagerPtr. Null during very early init and after teardown.
void* GetAppManager();

// CClientExoApp facade (AppManager +0x4). The client side owns the UI, the
// GUI manager, camera and player control.
void* GetClientApp();

// CClientExoAppInternal (facade +0x4) — the real client state: player_control
// @+0x2a0, CGuiInGame, the options block, input_class @+0x9c.
void* GetClientAppInternal();

// CSWCModule (client internal +0x18) — the client's view of the loaded
// module. Null between modules and during load.
void* GetClientModule();

// The active camera (CSWCModule +0x40). Position, orientation quaternion and
// the behavior list hang off it.
void* GetCamera();

// CServerExoApp facade (AppManager +0x8). The server side owns world truth,
// AI and the party table.
void* GetServerApp();

// CServerExoAppInternal (facade +0x4). Read state off this, not off the
// facade — an earlier walk used facade+0x1b770 for the party table and got
// random heap back.
void* GetServerAppInternal();

}  // namespace acc::engine
