// KOTOR Accessibility — Tab leader-change announce.
//
// Pillar 2 sub-feature. When the user presses Tab in-world the engine
// cycles control to the next living party member; we read the resulting
// leader creature and speak its name. Repetition is intentional: in solo-
// mode (one party member, or a story beat that strips companions), Tab
// pressing on the same creature still speaks the name — confirming "still
// solo" rather than swallowing the keypress silently.
//
// Gate: the Tab press is suppressed when foreground UI is capturing input
// (drilled sub-screen, dialog, container, etc.). Same predicate as the
// Enter gate in interact_hotkey — both call IsForegroundUiBlocking() in
// engine_panels so the policy stays single-sourced.
//
// Win32-polled (GetAsyncKeyState VK_TAB) — Tab is logical(0xce), reaches
// the manager hook directly, but the announce belongs at the per-tick
// dispatcher level (state observation, per the hook-vs-poll principle).

#pragma once

namespace acc::party_leader_announce {

// Per-frame tick from core_tick::Dispatch. Reads Tab rising-edge, applies
// the UI-block gate, then speaks the controlled creature's name. No-op
// when Tab is not pressed, KOTOR window isn't foreground, or the player
// is not yet loaded.
void Tick();

}  // namespace acc::party_leader_announce
