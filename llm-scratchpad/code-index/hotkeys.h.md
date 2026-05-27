# hotkeys.h (157 lines)

Centralised hotkey registry. Single source of truth for every mod-added binding. Per-tick pollers query Pressed(Action) for rising edge + modifier match + KOTOR foreground. Runtime-mutable via Set() for future rebind UI. Does not use the engine's [Keymapping] (no modifier-combo support there).

## Declarations (in source order)

- L13 — `namespace acc::hotkeys`
- L18 — `enum ModifierBit : uint32_t`
  note: kModShift=1, kModCtrl=2, kModAlt=4, kModAltGr=8
- L27 — `enum class Action : int`
  note: InteractTarget, InteractForceRadial, TargetKey1..3, PersonalKey1..4, ActionBarOpen1..4, TargetActionOpen1..3, LevelUpOpen, ExamineOpen, CombatQueueOpen, StatBlockSpeak, SelfStatusAnnounce, NavUp/Down/Left/Right/Home/End, SubmenuEsc, QueueClearAll, ContainerGiveMode, StoreModeToggle, CycleItemPrev/Next, CycleCategoryPrev/Next, AnnounceFocus, PathfindFocus, PathfindFocusForce, BeaconFocus, AnnounceDegrees, PartyLeaderAnnounce, CameraOrient, SaveMarkerAtCursor, ViewModeToggle, CameraStateProbe, EditboxReReadUp/Down/Submit/Cancel, ProbePathfind, ProbeAudioCycle, ProbeAudioFire, ProbeCameraDump, ProbeMouseLookToggle, ProbeCameraDistDump, ProbeCameraDistClampToggle, COUNT
- L108 — `struct Binding`
  note: vk, altVk (layout portability), modsRequired, modsForbidden
- L116 — `void BeginTick()`
- L117 — `void EndTick()`
- L121 — `bool Pressed(Action a)`
  note: rising edge + foreground gate; idempotent within a tick
- L125 — `bool Held(Action a)`
  note: pure held-state, no edge, no foreground gate
- L130 — `void Consume(Action a)`
  note: forces last==now so Pressed returns false for rest of tick
- L136 — `void ClaimRisingEdge(Action a)`
  note: pre-claims NEXT rising edge; use from sites firing before BeginTick (manager hook window)
- L139 — `bool ShiftHeld()`
- L140 — `bool CtrlHeld()`
- L141 — `bool AltHeld()`
- L142 — `bool AltGrHeld()`
- L144 — `bool IsForegroundGame()`
- L148 — `Binding Get(Action a)`
- L149 — `void Set(Action a, Binding b)`
- L150 — `bool IsUserRebindable(Action a)`
  note: returns false for diagnostic probes
- L154 — `const char* Name(Action a)`
- L155 — `const char* Describe(Action a)`
  note: rotating static buffer; multiple calls in one log line stay valid
