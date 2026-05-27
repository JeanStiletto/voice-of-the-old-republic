// Combat read-side queries. Read-only over creature_stats @+0xa74; no
// engine re-entry beyond documented accessors. Each entry self-gates on
// player loaded.
//
// Surfaces:
//   - Selected-PC stat block + leader-change auto-announce.
//   - Cycle/passive-narrate enrichment for Creature-kind targets.
//   - Shift+H Examine panel — fires CGuiInGame::ShowExamineBox and
//     a tick monitor speaks the rendered message_box.
//   - Bare H self-status (HP + effects + equipped weapons).

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::combat::query {

// False if no player loaded.
bool SpeakSelectedPcStatBlock();

// Polls active leader name; fires SpeakSelectedPcStatBlock on change.
void TickLeaderChangeAutoAnnounce();

// Append HP/AC/faction suffix to outBuf. True iff target is a Creature
// kind and resolved cleanly. targetServerObject is already resolved by
// caller; not re-resolved here.
bool BuildTargetCombatBrief(void* targetServerObject,
                            const char* targetName,
                            char* outBuf, size_t outBufSize);

// Resolves cycle/LastTarget focus, fires ShowExamineBox, speaks opener.
// Actual examine text is spoken by TickExaminePanel after the engine
// asynchronously populates the panel.
void HotkeyShiftH();

void TickExaminePanel();

void PollWin32Hotkey();

// Bare H — HP + active effects (deduped, named) + equipped weapons.
// No name or distance (always self, always zero).
void SpeakSelfStatus();

void PollWin32SelfStatusHotkey();

}  // namespace acc::combat::query
