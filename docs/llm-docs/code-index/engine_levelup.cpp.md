# engine_levelup.cpp (306 lines)

Implementation of engine_levelup.h. Dispatches the level-up wizard via two
candidate engine surfaces (CSWGuiInGameCharacter's handler first, CGuiInGame's
top-level variant as fallback), then replicates three of the four things a
real sub-screen open would establish (input_class=2, sw_gui_status=3,
BeginOverlayPause) — minus the Character panel, whose input handler would
re-code the wizard's nav keys. Talks to engine_panels, engine_player,
engine_offsets, engine_subscreen, log.

## Declarations (in source order)

- L14 — `namespace acc::engine_levelup`
- L16 — `namespace { ... }` (anonymous, TU-local addresses + helpers)
- L23 — `const uintptr_t kAddrCGuiInGameShowLevelUpGUI = R(0x0062dc00)`
  note: fallback path; returns 0 without opening a panel when level_up_mode==0, same gate as the InGameCharacter variant.
- L42 — `const uintptr_t kAddrCSWGuiInGameCharacterShowLevelUpGUI = R(0x006b0bb0)`
  note: btn_levelup click handler; RE'd via runtime byte-dump, not static decompile.
- L50 — `const uintptr_t kAddrCGuiInGameSetLevelUpMode = R(0x00628650)`
  note: writes CGuiInGame.level_up_mode (+0x10C); 0=block, 1=allow.
- L59 — `const uintptr_t kAddrCSWSCreatureStatsCanLevelUp = R(0x005a6810)`
  note: pure read-only predicate; exactly the Charakterblatt btn_levelup enabled-state check.
- L61 — `typedef uint32_t (__thiscall* PFN_CanLevelUp)(void*)`
- L65 — `constexpr size_t kCGuiInGameCharacterSlotOffset = 0x14`
- L67 — `typedef uint32_t (__thiscall* PFN_ShowLevelUpGUI)(void*, int)`
- L68 — `typedef void (__thiscall* PFN_SetLevelUpMode)(void*, int)`
- L73 — `void* GetInGameCharacterPanel(void* gui)`
  note: reads CGuiInGame slot +0x14; nullptr if the Charakterblatt panel was never instantiated this session.
- L88 — `bool SetLevelUpMode(void* gui, int mode)`
  note: calls the engine setter rather than poking the field directly, to preserve unknown engine-side invariants.
- L118 — `const uintptr_t kAddrCGuiInGameSetSWGuiStatus = R(0x0062aa00)`
  note: status 3 = sub-screen owns input, 4 = finishing; the ONE thing taken from a full sub-screen open (not the Character panel it also adds).
- L121 — `void DriveSWGuiStatus(void* gui, int status, int p2)`
- L140 — `bool s_pauseHeld`, `bool s_sawPanel`, `bool s_openingLevelUp` (module statics)
  note: s_openingLevelUp covers the SkillInfoBox sub-panel first-sight, which fires before the wizard lands on the modal stack.
- L153 — `void NoteLevelUpOpened(void* gui)`
  note: idempotent; sets input_class=2 + sw_gui_status=3 + BeginOverlayPause(LevelUp) — replicates 3 of the 4 real sub-screen-open effects, omitting the Character panel add.
- L174 — `bool PlayerCanLevelUp()`
  note: walks client leader → server creature (+0xf8) → creature_stats (+0xa74) → CanLevelUp thiscall.
- L201 — `bool TriggerLevelUp()`
  note: sets level_up_mode=1 unconditionally after the CanLevelUp gate; tries CSWGuiInGameCharacter path first, falls back to CGuiInGame variant on fault or null char panel.
- L283 — `bool IsOpeningLevelUp()`
- L285 — `void TickLevelUpPause()`
  note: waits until HasActiveLevelUpPanel() has been observed true at least once (s_sawPanel) before releasing the pause, so it doesn't release in the frame before the panel registers.
