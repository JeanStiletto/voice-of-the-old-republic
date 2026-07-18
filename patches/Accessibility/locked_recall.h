// Locked-object story-bark recall.
//
// Story-locked doors and containers explain themselves only once. The engine's
// generic "This object is locked" feedback (strref 1437) fires on every failed
// open, but the flavour bark that says WHY ("Security lock. Access code
// required." — an ownerless BarkBubble raised by the object's fail-to-open
// script) plays a single time and is then guarded off game-side. A blind player
// who misses that first bark never hears the reason again; the door just keeps
// repeating the unhelpful generic line.
//
// This module caches each locked object's story bark the first time it fires,
// keyed by the object's (stable) narrated-target handle, and replays it on
// subsequent locked interactions with the same object.
//
// Wiring:
//   * RegisterMsgRule() — adds the strref-1437 rule to the feedback router
//     (msg_router). The rule never consumes the line (the generic "locked"
//     still speaks); it notes the interacted object and, on a repeat attempt,
//     queues the cached explanation for replay.
//   * MaybeCapture()    — called from the BarkBubble handler for every fresh
//     bark; stores an ownerless bark as the explanation for the most-recently
//     locked object.
//   * Tick()            — flushes a queued replay (one line, after the generic
//     "locked" line the router already spoke this frame).

#pragma once

namespace acc::locked_recall {

// Register the strref-1437 feedback rule with the message router. Call once,
// alongside the other Register*MsgRules() from the router bootstrap.
void RegisterMsgRule();

// Offer a freshly-surfaced bark for capture. `ownerless` is true when the bark
// resolved to no speaker (the shape of a system/script bark). No-ops unless a
// locked interaction happened within the capture window and the object has no
// cached explanation yet.
void MaybeCapture(const char* barkText, bool ownerless);

// Flush a queued replay. Runs one line of speech, appended after the generic
// "locked" line the router already spoke. Cheap no-op when nothing is pending.
void Tick();

}  // namespace acc::locked_recall
