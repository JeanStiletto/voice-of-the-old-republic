# unified_action_menu.h (94 lines)

Public surface for the unified in-world action menu — one navigable menu
over BOTH the engine's target-action rows (CSWGuiTargetActionMenu, 3
categories) and the personal action bar (CSWGuiMainInterface, up to 4
populated categories), replacing the former radial_menu/target_action_menu/
actionbar_menu split. Navigation: Left/Right = category, Up/Down = entry
(clamp), Home/End = first/last entry, Ctrl+Home/End = first/last category,
Shift+arrow = speak full description, Enter = fire, Esc = cancel. Entry
points: ArmFromRadial (Shift+Enter), OpenTarget (Shift+1/2/3), OpenPersonal
(Shift+4/5/6/7); bare 1..7 fire instantly via the engine and never reach
this module. Engine read/primitive layers live in engine_radial (target)
and engine_actionbar (personal); this module only orchestrates + speaks.

## Declarations (in source order)

- L36 — `namespace acc::unified_menu`
- L43 — `bool OpenTarget(int row)`
  note: requires a focused/narrated target; never falls through to personal
- L48 — `bool OpenPersonal(int col)`
  note: always available; folds in the target block if a target is focused
- L56 — `bool ArmFromRadial(const char* name, uint32_t targetHandle)`
  note: opens on the first populated TARGET category; returns false (arms nothing) so the caller speaks the existing "no actions" redirect
- L58 — `bool IsActive()`
- L65 — `void ReannounceCurrent()` — re-speaks current category after a stacked overlay (combat queue) closes back onto this menu
- L72 — `bool IsSuspended()` — true while armed but under a blocking engine panel; keeps state + world pause, stops owning input
- L78 — `void SetForegroundBlocked(bool blocked)` — per-tick panel-stack edge notification; suspends/resumes
- L84 — `bool HandleInputEvent(int code, int value)` — routed nav/enter/esc/home/end codes; value 0 = release (ignored)
- L86 — `void ForceDisarm(const char* reason)`
- L91-92 — `int PersonalSelection(int col)` / `int TargetSelection(int row)` — bare-key announce support, mirrors engine's per-slot selection
