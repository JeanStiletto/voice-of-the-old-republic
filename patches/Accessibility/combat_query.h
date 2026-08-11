// Combat read-side queries. Read-only over creature_stats @+0xa74; no
// engine re-entry beyond documented accessors. Each entry self-gates on
// player loaded.
//
// Surfaces:
//   - Leader-change auto-announce.
//   - Cycle/passive-narrate enrichment for Creature-kind targets.
//   - Bare H self-status (HP + effects + equipped weapons).

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::combat::query {

// Polls active leader name; speaks it on change.
void TickLeaderChangeAutoAnnounce();

// Append HP/AC/faction suffix to outBuf. True iff target is a Creature
// kind and resolved cleanly. targetServerObject is already resolved by
// caller; not re-resolved here.
bool BuildTargetCombatBrief(void* targetServerObject,
                            const char* targetName,
                            char* outBuf, size_t outBufSize);

// Bare H — HP + active effects (deduped, named) + equipped weapons.
// No name or distance (always self, always zero).
void SpeakSelfStatus();

void PollWin32SelfStatusHotkey();

// Resolve the display name of the item equipped in one inventory slot
// (creature → CSWInventory @+kCreatureInventoryOffset → slot handle →
// display name; SEH-guarded per hop). slotHandleOffset is one of the
// kInventory*HandleOffset constants. False on empty slot / any miss.
// Shared with weapon_set_watch (post-swap weapon naming).
bool ReadEquippedItemName(void* serverCreature, size_t slotHandleOffset,
                          const char* slotLabel,
                          char* outBuf, size_t outBufSize);

}  // namespace acc::combat::query
