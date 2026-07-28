# unified_action_menu.cpp (1102 lines)

Implements the unified action menu. Builds a flat category list each open/
re-anchor from live engine reads (target rows via engine_radial, personal
columns via engine_actionbar), keeps per-row/per-column selected-entry
"shadows" in lock-step with the engine's own selection (SelectActionInRow /
SelectVariant) so bare-key announces and Shift+description reads stay
accurate across PopulateMenus rebuilds. Handles three arm paths (radial,
target-row, personal-column) plus mid-session "follow-cycling" re-anchor
(narrated target changes while the menu is open — rebuilds target rows and
relocates selection BY ACTION-ID identity, gating Enter into an orientation
announce when the previous selection isn't carried onto the new target) and
a suspend/resume path for foreground-blocking engine panels. Enter dispatch
forces the engine's append-queue mode (bypassing its native Shift-capture),
restamps the target row to the armed handle right before firing (the
engine re-bakes lists against its OWN current target while the menu sits
open), and either stays armed (in combat, or out-of-combat while paused —
"stack mode") or fire-and-closes (out of combat, world running — mouse-
radial parity). Talks to: engine_radial, engine_actionbar, engine_picker
(ReanchorRadial), engine_subscreen (BeginOverlayPause/EndOverlayPause),
combat/combat_queue/combat_diag, narrated_target, menu_speak, prism.

## Declarations (in source order)

- L32-41 — `kRowCount`/`kColumnCount`/`kMaxCats`, `enum class CatKind`, `struct Cat{kind,slot}`
- L48-49 — `g_targetSel[3]` / `g_personalSel[6]` — persistent per-slot selection shadows
- L51-93 — `struct State` — active/suspended, cats[]/catCount/curCat, targetHandle,
  creature flag, hasTargetBlock, reqSlot (captured pre-engine-call to dodge a
  stack-corruption bug), targetName, unfoldDeclined, pausedOnOpen
- L111 — `bool ForegroundPanelBlocks()` — IsForegroundUiBlocking wrapper
- L117-155 — per-kind dispatch helpers: `CountForCat`, `ShadowFor`, `ApplySelection`,
  `ReadLabel`, `ReadActionId`
- L162 — `void AppendItemQuantity(...)` — appends charge/stack-count suffix to item labels
- L185 — `bool Dispatch(tam, mi, c)` — DispatchRowAction / FireSelectedVariant
- L194 — `const char* CategoryName(c)` — localized name, nullptr for unnamed (non-creature target rows)
- L216 — `void BuildCategoryList(tam, mi)` — populates g.cats[] skipping empty categories
- L241 — `int FirstPopulatedTargetRow(tam)`
- L260 — `bool TargetRowsLookHostileCreature(tam)`
  note: robust signal is tagged action_id ranges (force/feat/item bits set), NOT the vtable downcast — a far extended-cycled creature fails that downcast
- L273 — `bool DetectCreature(tam, handle)` — vtable downcast OR action-content fallback
- L282 — `void FormatCategory(...)` — "Name: label, N Optionen" / plain-label composition
- L310 — `void SpeakCategory(tam, mi, prefix)` — full category announce
- L335 — `void SpeakEntry(tam, mi)` — entry-only announce (post Up/Down/Home/End)
- L350 — `uint32_t ResolveNarratedServerHandle()`
- L367 — `bool ActionMenuAutoPauseEnabled()` — CClientOptions bit 0x8000, decompile-confirmed
- L372 — `void Arm()` — sets active + conditionally BeginOverlayPause
- L389/394 — `int PersonalSelection(col)` / `int TargetSelection(row)`
- L399/400 — `bool IsActive()` / `bool IsSuspended()`
- L408 — `void ReannounceCurrent()`
- L437 — `void SetForegroundBlocked(bool blocked)` — suspend on rising edge, rebuild+re-speak on falling edge
- L482 — `void ForceDisarm(reason)` — EndOverlayPause if owned; shadows intentionally persist
- L502 — `bool ArmFromRadial(name, targetHandle)`
- L542 — `bool OpenTarget(int row)`
  note: reads the LIVE menu, does NOT re-populate — input_pipeline's bare-key path already repopulated this same press; re-populating here caused a phantom-confirm bug (2026-06-07)
- L611 — `bool OpenPersonal(int col)` — folds in target block only if a hostile target is focused and populated
- L695 — `bool HandleInputEvent(code, value)` — Esc close (with world-pause-aware close cue),
  follow-cycle re-anchor by action-id, lazy re-anchor only when rows drained,
  Shift+arrow description read, Left/Right/Home/End/Up/Down navigation,
  Enter dispatch (queue-mode force, retarget, fire-and-close vs stack-mode logic)
