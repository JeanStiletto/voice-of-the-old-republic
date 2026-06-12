// Help system — two surfaces over one tagged keybind catalog.
//
//   F1 (HelpMenuOpen)   — opens a navigable, screen-reader-friendly list of
//                         every important binding, grouped under section
//                         headers. Up/Down read the list, Home/End jump to
//                         the ends, Enter re-reads the current line, Esc or
//                         F1 close. Works in EVERY context (in-world, any
//                         menu, dialog, map) because it is a synthetic in-DLL
//                         overlay driven by a global Win32 poll — no engine
//                         panel of its own. Mirrors examine_view's model.
//
//   Ctrl+F1 (HelpContext) — speaks the subset of the catalog tagged for the
//                         screen the user is currently on (in-world / menu /
//                         map / action menu / dialog / container / store).
//                         Generalises the per-screen "what's important here"
//                         announcement the Pazaak board already does on open.
//
// The catalog (kEntries in help.cpp) is the single source of truth. Each
// entry carries a `grp` (which F1 section it lives under — every entry is
// listed) and a curated `ctx` bitmask (which screens speak it on Ctrl+F1 —
// kept short so a context summary stays digestible). F1 is exhaustive;
// Ctrl+F1 is the essentials, ending with a "press F1 for the full list" hint.
//
// Localised strings live in strings.h (HelpGroup* / HelpKey* / HelpContext*).

#pragma once

namespace acc::help {

// True while the F1 list overlay is interactive. Menu input hooks
// (menus.cpp manager hook, in-world Esc consume in input_pipeline) gate on
// this so the underlying panel / world doesn't also act on the nav keys the
// overlay owns.
bool IsMenuOpen();

// Open / close / toggle the F1 list. OpenMenu pauses the world via
// BeginOverlayPause only when opened from pure in-world context (matching
// examine_view); in a menu the engine panel already holds the world.
void OpenMenu();
void CloseMenu();

// Global per-tick poll. Drives F1 (toggle the list), Ctrl+F1 (speak the
// current screen's keys), and — while the list is open — the list's own
// Up/Down/Home/End/Enter/Esc navigation via the hotkey registry. Claims the
// nav edges it consumes so downstream in-world pollers (cycle, interact,
// examine) don't double-act. Call early in core_tick::Dispatch.
void PollWin32();

// Self-disarm hook. Closes the list if the world drops out from under an
// in-world open (area load / teardown), so a stranded overlay can't survive
// a transition with a stale pause hold. Cheap when the list is closed.
void Tick();

}  // namespace acc::help
