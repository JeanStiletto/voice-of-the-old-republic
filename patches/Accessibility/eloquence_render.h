// Eloquence (IBM ECI) render-to-memory — the NVDA IBMTTS add-on's voice,
// rendered by the mod so it can be played positionally.
//
// We NEVER ship or copy ECI.DLL (no distribution rights). This only loads an
// engine the user already installed with the IBMTTS add-on, from the add-on's
// own folder, and only in-process: the add-on's ECI.DLL is 32-bit, like the
// game. Paths and voice settings come from NVDA's own config
// (%APPDATA%\nvda\nvda.ini: [ibmeci] TTSPath / dllName, and
// [speech] [[ibmeci]] voice / variant / rate / rateBoost / pitch /
// inflection / volume / hsz / rgh / bth / sampleRate), mapped the way the
// add-on's driver maps them (synthDrivers/ibmeci.py + _ibmeci.py).
//
// Rendering runs on a short-lived worker thread: ECI delivers its audio
// callbacks as window messages to the thread that created the engine handle,
// so that thread pumps its own queue until synthesis ends.

#pragma once

#include "prism.h"  // prism::RenderedSpeech

namespace acc::eloquence {

// Renders `text` (game codepage; must be cp1252-compatible for the Western
// ECI languages) with the user's NVDA Eloquence voice and settings — except
// volume: the samples are rendered at full ECI volume and NVDA's volume comes
// back in out.volume for the player to apply. Blocks
// until the render ends (5 s cap). False when no installed engine is found,
// it fails to load, or synthesis produces no audio; the reason is logged
// under "Eloquence".
bool RenderToMemory(const char* text, prism::RenderedSpeech& out);

}  // namespace acc::eloquence
