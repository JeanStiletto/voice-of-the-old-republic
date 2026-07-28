# input_pipeline.h (70 lines)

Declares the engine-input hooks (upstream of the GUI manager) and two
self-expiring poll-vs-event race latches shared across the DLL. The Esc
latch defeats the case where the Win32-poll-driven overlay close (unified
menu / combat queue / examine view / help) beats the engine's own Esc event
to OnClientHandleInputEvent, which would otherwise let Esc fall through and
pop the engine's pause menu on top of an already-closed overlay. The editbox
latch stops the confirm-Enter on a save-name popup from also firing an
in-world interact once the world unpauses.

## Declarations (in source order)

- L27 — `unsigned int NextSeq()` — monotonic counter shared with menus.cpp's manager-side log so streams interleave comparably
- L44 — `void NoteOverlayEscClosed()` / `bool ConsumeOverlayEscLatch()`
  note: self-expiring, single-shot; window is short (poll and engine event land same/adjacent frame)
- L66 — `void NoteEditboxSubmitClosed()` / `bool ConsumeEditboxSubmitLatch()`
  note: set by the editbox monitor's Enter handler, consumed same-tick by interact::PollHotkey
