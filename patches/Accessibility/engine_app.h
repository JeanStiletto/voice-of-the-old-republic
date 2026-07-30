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

// CAppManager* — .data global, deliberately NOT passed through acc::addr::R().
// R() covers .text only and .data is byte-stable across the builds it exists
// for (engine_rebase.h, and the .data section note in
// engine_offsets_addresses.h).
constexpr uintptr_t kAddrAppManagerPtr = 0x007A39FC;

// CAppManager.client @+0x4 → CClientExoApp*, .server @+0x8 → CServerExoApp*.
constexpr size_t kAppManagerClientAppOffset = 0x4;
constexpr size_t kAppManagerServerAppOffset = 0x8;

// CServerExoApp mirrors the client facade/internal split: the public facade is
// 8 bytes (vtable@0, internal@4) and the internal carries the state. Verified
// via CServerExoApp::GetPartyTable @0x004aee70 (MOV EAX,[ECX+4]; ADD EAX,...).
constexpr size_t kServerExoAppInternalOffset = 0x4;

namespace acc::engine {

// *kAddrAppManagerPtr. Null during very early init and after teardown.
void* GetAppManager();

// CServerExoApp facade (AppManager +0x8). The server side owns world truth,
// AI and the party table.
void* GetServerApp();

// CServerExoAppInternal (facade +0x4). Read state off this, not off the
// facade — an earlier walk used facade+0x1b770 for the party table and got
// random heap back.
void* GetServerAppInternal();

}  // namespace acc::engine
