// Party-member-name cache.
//
// Used by the combat msg-bus filter to decide whether an incoming
// "X ist erfolgreich mit Angriff auf Y" line targets a party member
// (PC or active follower). The cache snapshots names returned by the
// engine's display-name accessor — same byte sequences the engine
// puts into the message buffer — so name comparison is locale-clean
// without needing a separate locale map for party names.
//
// Polling strategy: lazy refresh on IsPartyMember() if the cache is
// older than kRefreshIntervalMs. The engine party roster only changes
// on recruit / swap / dismiss (rare, manual), so a 1-second backstop
// catches every realistic update without per-call cost.

#pragma once

namespace acc::combat {

// Returns true if `name` (a NUL-terminated string from the engine's
// message buffer) matches any active party-member name. Case-sensitive,
// byte-exact — relies on engine emitting identical bytes for the same
// creature across the message buffer and the display-name accessor.
//
// Lazily refreshes the internal cache on first call per refresh window.
bool IsPartyMember(const char* name);

// Force a refresh on the next IsPartyMember call (e.g. on
// combat-mode entry edge). Optional — the time-based backstop catches
// roster changes anyway.
void InvalidatePartyCache();

}  // namespace acc::combat
