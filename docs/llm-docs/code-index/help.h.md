# help.h (55 lines)

Public interface for the two-surface help system: F1 opens/toggles a
navigable synthetic overlay listing every keybind (grouped, works in every
context); Ctrl+F1 speaks only the subset tagged for the current screen. The
catalog (kEntries) lives in help.cpp; localised strings in strings.h
(HelpGroup*/HelpKey*/HelpContext*).

## Declarations (in source order)

- L34 — `bool IsMenuOpen()`
  note: menus.cpp manager hook + input_pipeline's in-world Esc consume both gate on this
- L39 — `void OpenMenu()`
- L40 — `void CloseMenu()`
- L47 — `void PollWin32()`
  note: call early in core_tick::Dispatch; claims nav edges it consumes so cycle/interact/examine don't double-act
- L52 — `void Tick()`
  note: self-disarm on world teardown during an in-world open; cheap no-op when closed
