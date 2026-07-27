# engine_input.h (133 lines)

Engine input-code translation + DirectInput acquire/release control surface (pure read-side plus the SetActive drive primitives). Also carries file-scope (non-namespaced) pre-translation input-code constants used by menu hooks.

## Declarations (in source order)

- L8 — `const char* InputIndexName(int code)`
- L13 — `int ManagerTranslateCode(int code)`
- L29 — `bool EnsureInputAcquired()` — cold-start DirectInput wake, byte-for-byte HideLoadScreen replica
- L45 — `bool ForceReacquireInput()` — forced 0->1 edge, software equivalent of alt-tab
- L56 — `bool ReleaseInput()` — deactivate half, makes input mirror foreground
- L75 — `void RequestInputReacquire()` — set-only, coalescing, safe from any thread
- L84 — `void RequestInputRelease()` — counterpart for focus-loss
- L89 — `void DrainPendingReacquire()` — call once per tick from the engine thread
- L95-98 — `kInputNavUp=0xb6`, `kInputNavDown=0xb7`, `kInputNavLeft=0xb8`, `kInputNavRight=0xb9`
- L99-103 — `kInputEnter1=0xb5`, `kInputEnter2=0xbb`, `kInputEsc1=0xb4`, `kInputEsc2=0xdf`, `kInputActivate=0x27`
- L109 — `kInputDefaultAction = 0xef` — keymap.2da `DefaultAction` row (default key R), keyboard twin of Mouse 1
- L114-115 — `kInputHome=32`, `kInputEnd=33` — raw InputIndices, no [Keymapping] entry in stock ini
- L121-122 — `kInputCatFirst=0x201`, `kInputCatLast=0x202` — internal routing codes (NOT engine InputIndices), synthesised by interact_hotkey for Ctrl+Home/End category jump
- L128-132 — `kInputKbLeftShift=24`, `kInputKbRightShift=25`, `kInputKbComma=103`, `kInputKbPeriod=104`, `kInputKbAnnounce=105`
  note: kInputKbAnnounce keeps `,` `.` `-`/`/` contiguous across QWERTZ/QWERTY
