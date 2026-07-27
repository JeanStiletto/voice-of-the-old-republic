# menus_powers_levelup.cpp (470 lines)

Force-power picker (CSWGuiPowersLevelUp, shared by chargen power-selection and
the in-game level-up "Kräfte" sub-screen) input handler. Models the panel as a
2D skill-tree grid (CSWGuiSkillFlow rows of up to 3 CSWGuiFlowSkillStruct
cells) plus trailing virtual button rows (Empfohlen/OK/Abbrechen), mirroring
`menus_chargen_feats`'s navigation shape and sharing column-scan primitives
with `menus_skillflow_nav` (extracted since the last index refresh — this file
no longer defines its own FirstFilledCol/NearestFilledCol/ReadLabelText/
ReadButtonText, it now calls into shared helpers). Talks to `menus_internal`
(FindControlById, QueueButtonByIdActivate), `menus_pending` (IsPending guard),
`engine_panels` (IdentifyPanel), and `strings`/`prism` for chart-cell status
speech.

## Declarations (in source order)

- L32-L39 — pwrlvlup.gui control-id constants (kIdSubTitleLabel=1, kIdPowersListbox=6, kIdDescriptionLb=7, kIdPowerLabel=8, kBtnRecommendedId=9, kBtnAcceptId=11, kBtnBackId=12)
- L41-L45 — `struct ChartRow { row, rowIdx, powerId[3] }` (0xffff = empty), `struct ButtonRow { buttonId, logTag }`
- L57-L62 — `kButtonRows[]`: Recommended/Accept/Back virtual rows
  note: BTN_SELECT (id 10) intentionally omitted — Enter on a cell dispatches OnPowerPicked directly, the same action the button would fire
- L64-L76 — statics: `s_chartRows[64]`, `s_chartRowCount`, `s_curRow`/`s_curCol`, binding signature (`s_boundPanel`/`s_boundRowsPtr`/`s_boundRowsCount`)
- L78-L79 — `TotalRowCount()`, `IsButtonRow(int r)`
- L83-L95 — `ColFilled`, `FirstFilledCol`, `NearestFilledCol` — thin wrappers over `acc::menus::skillflow_nav`
- L103-L120 — `bool ReadChartBinding(panel, outRows, outCount)`: reads powers_listbox.controls (CExoArrayList) directly
  note: the chart at +0x19fc holds (row,col) selection state only — its own rows_data is unused in the level-up flow
- L124-L161 — `void WalkChartRows(void* rows, int nRows)`: builds `s_chartRows[]` from the engine array
- L168-L202 — `void EnsureBound(void* panel)`: re-walks the chart every hit (auto-level-up reuses the panel pointer across characters); resets cursor only on a real binding change
- L204-L218 — `unsigned char ReadCellStatus(int r, int c)`
- L220-L231 — `const char* StatusWord(unsigned char status)` — Available/Existing/Granted/Locked/Chosen
- L233-L236 — `bool ReadPowerName`, L238-L255 `bool ReadDescription`
- L261-L286 — `void DriveEngineSelection(void* panel, unsigned short powerId)`: calls `CSWGuiSkillFlowChart::SetSelectedSkill` + `CSWGuiPowersLevelUp::OnEnterPower`, both SEH-guarded
- L288-L337 — `void AnnounceFocused(void* panel)` — button-row vs chart-cell speech (name/status + description)
- L339-L361 — `void NavVertical(void* panel, bool down)`
- L363-L385 — `void NavHorizontal(void* panel, bool right)`
- L389-L391 — `bool IsPowersLevelUpPanel(void* panel)`
- L393-L401 — `const char* GetTitleOverride(void* panel)` — returns the panel's sub_title_label text instead of the misleading main title (no power-point-budget formatting here — that was a prior-revision behavior, current code just echoes the label)
- L403-L467 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)`: Up/Down/Left/Right/Enter/Esc dispatcher
  note: Enter on a chart cell calls `OnPowerPicked` directly (guarded by `menus::pending::IsPending()`), Enter on a button row uses `QueueButtonByIdActivate`
