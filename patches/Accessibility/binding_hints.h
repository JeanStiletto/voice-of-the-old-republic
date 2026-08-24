// Which device's wording the player hears — the one decision point.
//
// KOTOR has no input MODE. Keyboard and pad are live at the same time, always,
// on both games: nothing in the engine is ever "in controller mode", and the
// mod's own bindings are the same way (the pad's D-Pad and the keyboard's
// comma / period drive the same object cycle). So the question this module
// answers is NOT "what can the player press" — the answer to that is always
// "either" — but the narrower, purely presentational one:
//
//     when a help line or a tutorial hint has to NAME a control,
//     which name does it use?
//
// The convention every console-era game follows is presence: a controller that
// is plugged in and answering means controller prompts, otherwise keyboard
// prompts. That is what ControllerActive() implements, over pad::Connected()
// (see pad_input.h for how presence is established and how an unplug becomes
// visible again).
//
// Why it lives in its own TU rather than being `if (pad::Connected())` at each
// call site: there are ~60 hint strings across four surfaces (the F1 list, the
// Ctrl+F1 context summary, the silent tutorial.2da popups, and the Endar Spire
// dialogue hints). Sixty copies of the predicate is sixty chances for two
// surfaces to disagree about which device the player is on — the F1 list
// naming the D-Pad while the tutorial popup two seconds later names comma and
// period. One predicate, asked from one place, cannot do that.
//
// Adding a controller wording to an existing hint is two steps and no logic:
//   1. add a `<Name>Pad` id next to the keyboard id in strings.h, and its text
//      to each strings_<lang>.cpp;
//   2. add the pair to kTwins in binding_hints.cpp.
// Call sites keep calling Text() and never change.

#pragma once

#include "strings.h"

namespace acc::binding_hints {

// True when help and tutorial text should name controller buttons instead of
// keyboard keys. Ask this — never pad::Connected() — for anything the player
// HEARS, so every surface flips together.
bool ControllerActive();

// The device-appropriate wording for a hint, given its KEYBOARD string id.
//
// Keyboard ids stay the identity of a hint everywhere in the code — call sites,
// tables, logs — because the keyboard wording is the one that always exists.
// The controller wording is an optional twin looked up here.
//
// Falls back to the keyboard text whenever there is no twin for this id, or the
// twin is empty in the active language (a translation that has not caught up
// yet). Never returns null; returns "" only when the keyboard string is itself
// empty, which callers already treat as "no hint, leave the vanilla text".
const char* Text(acc::strings::Id keyboardId);

// The id Text() would resolve to. For the rare caller that has to hold onto an
// id rather than a pointer (the help list stores ids in its row table and
// resolves them at speak time).
acc::strings::Id IdFor(acc::strings::Id keyboardId);

}  // namespace acc::binding_hints
