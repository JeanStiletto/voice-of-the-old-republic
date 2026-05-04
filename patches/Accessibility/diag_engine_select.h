// Engine select-system diagnostic — logs Q / E / Tab keypresses so we can
// correlate them against `LastTarget changed` and `PassiveNarrate:` log
// lines and see what the engine's built-in target-cycle does.
//
// Per investigation Q6, the engine has its own target-cycle primitive
// (`CClientExoAppInternal::SelectNearestObject @0x005fb050`). Web sources
// confirm Q / E are wired to "cycle target right-to-left / left-to-right"
// and Tab is "switch active party leader" in vanilla KOTOR 1.
//
// Layer: diag/ (pure observation; no engine re-entry, no game-state
// mutation). Single-purpose; removable in one commit once we've used the
// data to decide whether to delegate our `,` / `.` cycle to the engine's
// SelectNearestObject or keep our own filter.
//
// Two questions this answers:
// 1. Does pressing Q / E populate `LastTarget`? (If yes, our
//    passive_narrate watcher will already pick up the engine's target
//    cycle for free — no integration code needed.)
// 2. What categories of object does the engine include in its cycle? (Our
//    Pillar 4 filter has six locked categories; the engine's selection
//    might be combat-only, or include all interactables, or something
//    in between. Comparing the handles the engine cycles to vs. the
//    handles our filter accepts tells us the gap.)

#pragma once

namespace acc::diag::engine_select {

// OnUpdate per-tick poll. Logs Q / E / Tab rising edges with the current
// LastTarget handle (so we can see "user pressed Q while LastTarget was
// 0xX, then a tick later LastTarget became 0xY"). Self-gates on
// foreground window + GetPlayerPosition (silent in menus / chargen).
void Tick();

}  // namespace acc::diag::engine_select
