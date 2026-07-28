# menus_abilities.cpp (351 lines)

Dedicated input handler for the in-game "Fähigkeiten" screen
(CSWGuiInGameAbilities): a two-level submenu (tab level: Up/Down pick
Skills/Powers-if-Jedi/Feats, Enter drills in; list level: Up/Down browse
entries clamped, Esc returns to tab level). Skills are driven entirely by us
via the engine's `OnEnterSkill`; Feats/Powers forward to the engine's own
chart nav (`HandleInputEvent` codes 0x31/0x32), pre-clamped so it never wraps.
Speaks name + rank/bonus/total only on the Skills tab (Feats/Powers repaint
leaves those stale at "0"). All engine calls are SEH-guarded `__thiscall`
invocations at fixed addresses from `engine_offsets.h`.

## Declarations (in source order)

- L23 — `namespace acc::menus::abilities`
- L30 — `void* SelectedRow(void* panel)`
  note: ability_listbox row at selection_index; SEH-guarded
- L52 — `void CallOnEnterSkill(void* panel, void* row)`
  note: `__thiscall(this, row)` — typedef MUST carry the arg or `ret 4` corrupts the frame
- L67 — `void CallPanelInput(void* panel, int code)`
  note: feeds panel-internal codes 0x29 (tab cycle) / 0x31,0x32 (chart step) to the engine's own HandleInputEvent
- L80 — `bool ChartCanStep(void* panel, int tab, bool down)`
  note: pre-clamp so Feats/Powers chart nav doesn't wrap (engine's own wraps)
- L99 — `bool PowersAvailable(void* panel)`
- L118 — `void SwitchToTab(void* panel, int tab)`
  note: On*ButtonPressed purgeSize is 4 despite Ghidra `(void)` signature — typedef must carry a dummy arg
- L132 — `int BuildAvailableTabs(void* panel, int outTabs[3])`
- L142 — `int ReadTab()`
  note: reads CGuiInGame.field139_0xbc0 via ResolveGuiInGame; -1 on failure
- L157 — `bool ReadLabel(void* panel, size_t offset, char* buf, size_t bufSize)`
- L168 — `void AppendPair(void* panel, size_t labelOff, size_t valueOff, char* msg, size_t msgSize)`
- L189 — `void AnnounceDetail(void* panel, const char* prefix, bool withStats)`
  note: withStats only appended on Skills tab — Feats/Powers repaint leaves rank/bonus/total stale
- L212 — `void SpeakTabName(void* panel, int tab)`
- L226 — `bool s_drilled` / `void* s_drilledPanel`
  note: two-level drill state; resets to tab level on panel-pointer change
- L231 — `bool IsAbilitiesPanel(void* panel)`
- L237 — `void RefreshDetail(void* panel)`
  note: only Skills tab needs the repaint (Feats/Powers self-maintain via engine's OnEnter*)
- L247 — `void BrowseList(void* panel, int tab, ListBoxNavOp op)`
- L271 — `bool HandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)`
  note: two-level tab/list dispatcher; Esc arrives as kInputEsc2 (0xdf) for a real keypress, must match both Esc codes
