# announce_facing.h (24 lines)

Public interface for the facing-readout hotkey: direction on the compass voice, then area (world) or room (map) on the screen reader. Camera (not character) facing is used because A/D rotates the camera and W only snaps the character on commit — every other orient cue references camera direction.

## Declarations (in source order)

- L22 — `void PollWin32()` — Win32-polled entry point (AltGr unbound in kotor.ini, engine keymap drops unbound scancodes)
