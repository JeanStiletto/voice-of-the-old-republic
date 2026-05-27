// Party-member-name cache.
//
// Used by the combat msg-bus filter to decide whether an incoming
// "X attacks Y" line targets a party member (PC or active follower).
// Snapshots names from the engine's display-name accessor — same bytes
// the engine puts into the message buffer — so comparison is locale-
// clean without a separate map for party names.
//
// Lazy refresh on IsPartyMember if the cache is older than the backstop
// (1s). Roster changes only on recruit/swap/dismiss — rare and manual.

#pragma once

namespace acc::combat {

// Case-sensitive, byte-exact. Lazily refreshes per refresh window.
bool IsPartyMember(const char* name);

// Force refresh on next call. Optional — time-based backstop catches
// roster changes anyway.
void InvalidatePartyCache();

}  // namespace acc::combat
