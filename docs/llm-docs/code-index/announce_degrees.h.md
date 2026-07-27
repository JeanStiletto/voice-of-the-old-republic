# announce_degrees.h (20 lines)

Public interface for the exact-heading hotkey. Camera (not character) facing is used because A/D rotates the camera and W only snaps the character on commit — every other orient cue references camera direction.

## Declarations (in source order)

- L18 — `void PollWin32()` — Win32-polled entry point (AltGr unbound in kotor.ini, engine keymap drops unbound scancodes)
