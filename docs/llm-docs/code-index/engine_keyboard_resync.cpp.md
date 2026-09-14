# engine_keyboard_resync.cpp (~300 lines)

Keyboard transition-stream re-synchronisation. Wraps the one call through which `CExoInputInternal::GetEvents` fetches the per-frame keyboard batch (`CExoRawInputInternal::GetKeyboardBuffer`), folds the batch into a 256-key shadow of what the engine has been told, reads the true level state with `IDirectInputDevice8::GetDeviceState`, and appends a synthetic release for any key the engine still holds down that the device reports up — after two consecutive frames of disagreement with no transition for that key. Presses are never synthesised; a normal frame passes through untouched. Fixes the stranded-key family (camera spins after a dialogue, party walks off) caused by the engine draining its DirectInput buffer on every SetActive edge and on a lost device without ever re-reading the state. Both games. Talks to engine_input (raw-input layout), engine_rebase, engine_game, log. Model: gui-and-input-internals.md "keyboard_transition_stream".

## Declarations (in source order)

- `kAddrGetKeyboardBuffer = Pick(0x005e3ae0, 0x00732c40)` — the engine's reader (K2 located by its 0x1400 batch allocation)
- `kAddrGetEventsKeyboardCall = Pick(0x005e25fe, 0x0072c986)` — the CALL rel32 inside GetEvents that is rewritten
- `struct DeviceBuffer { entries[5]*, size, reserved }` — CExoDeviceBuffer (0xc bytes)
- `kVtGetDeviceState = 0x24` — IDirectInputDevice8 vtable slot 9
- `kConfirmFrames = 2` — disagreement frames before a release is appended
- `g_shadowDown[256]`, `g_lastTransitionFrame[256]`, `g_disagreeFrames[256]`, `g_pressLostLogged[256]` — POD state (SEH-safe, file scope)
- `void Resync(rawInput, buf, readerRc)` — the post-read pass (SEH; gates on active / keyboard mode / device; device change resets the shadow; logs `KeyboardResync.Device` edges when GetDeviceState fails, `synthesised release for DIK 0x..` per repair, one line per lost-press episode)
- `int __fastcall GetKeyboardBufferResynced(rawInput, edx, buf)` — the stand-in for the CALL: forwards to the real reader, then Resync unless a fault disabled it
- `void InstallKeyboardStreamResync()` (public) — verifies the site is `E8` + rel32 → reader, rewrites the rel32, logs `installed` / the reason it declined. K2 from DllMain, K1 from EnsureSessionBringup.
