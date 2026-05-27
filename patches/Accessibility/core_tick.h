// Mod-wide per-tick dispatcher.
//
// Owns the OnUpdate hook (CSWGuiManager::Update @ 0x0040ce76). Fans out
// to each subsystem in a fixed order — explicit calls in Dispatch() so
// the file reads as the canonical "what fires per tick" list.

#pragma once

namespace acc::tick {

void Dispatch();

}  // namespace acc::tick
