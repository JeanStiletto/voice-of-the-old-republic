# menus_chargen_feats.cpp (484 lines)

Dedicated input handler for the chargen "Talente" panel (CSWGuiFeatsCharGen):
a 2D feat-tree chart (row = progression chain, up to 3 columns = root/
successor/master) that doesn't fit the listbox-driven dispatcher. Builds a
flat row-major cursor over non-empty chart cells plus 3 trailing virtual
button rows (Empfohlen/OK/Abbrechen). On each cell focus calls the engine's
`SetSelectedSkill` + `OnEnterFeat` directly (refreshes name/description/
highlight in one round-trip); Enter on a cell calls `OnFeatPicked`. Re-walks
the engine's chart rows array on every input hit (`EnsureBound`) so a panel
reused for the next auto-level-up character never indexes stale row
pointers.

## Declarations (in source order)

- L26 — `namespace acc::menus::chargen_feats`
- L32-34 — `constexpr int kBtnRecommendedId=9, kBtnAcceptId=11, kBtnBackId=12` (anonymous ns)
- L46 — `struct ChartRow { void* row; int rowIdx; unsigned short featId[3]; }` (anonymous ns)
- L52 — `struct ButtonRow { int buttonId; const char* logTag; }` / `kButtonRows[]` (anonymous ns)
- L65-80 — `ChartRow s_chartRows[256]` / `s_chartRowCount` / `s_curRow` / `s_curCol` / binding-signature statics (anonymous ns)
- L82-83 — `int TotalRowCount()` / `bool IsButtonRow(int r)` (anonymous ns)
- L87 — `bool ColFilled(int r, int c)` (anonymous ns)
  note: empty sentinel is featId == 0xffff
- L93-98 — `int FirstFilledCol(int r)` / `int NearestFilledCol(int r, int want)` (anonymous ns)
  note: delegate to shared acc::menus::skillflow_nav helpers
- L103 — `bool ReadChartBinding(void* panel, void*& outRows, int& outCount)` (anonymous ns)
- L126 — `void WalkChartRows(void* rows, int nRows)` (anonymous ns)
  note: skips rows with no filled cells entirely
- L170 — `void EnsureBound(void* panel)` (anonymous ns)
  note: cursor resets to first filled cell ONLY on a genuinely new (panel, rows-ptr, rows-count) binding; otherwise preserves + clamps position
- L212 — `unsigned char ReadCellStatus(int r, int c)` (anonymous ns)
  note: low byte of FlowSkillStruct+0x120 — 0 avail/1 existing/2 granted/3 locked/4 chosen
- L228 — `const char* StatusWord(unsigned char status)` (anonymous ns)
- L241 — `bool ReadNameLabel(void* panel, char* out, size_t outN)` (anonymous ns)
- L249 — `bool ReadDescription(void* panel, char* out, size_t outN)` (anonymous ns)
- L271 — `void DriveEngineSelection(void* panel, unsigned short featId)` (anonymous ns)
  note: SetSelectedSkill (highlight) + OnEnterFeat (repaint) — both SEH-guarded __thiscall
- L298 — `void AnnounceFocused(void* panel)` (anonymous ns)
- L349 — `void NavVertical(void* panel, bool down)` (anonymous ns)
- L373 — `void NavHorizontal(void* panel, bool right)` (anonymous ns)
  note: buttons are single-cell — re-announces rather than going silent on Left/Right
- L401 — `bool IsChargenFeatsPanel(void* panel)`
- L412 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)`
  note: Enter on a locked/granted/chosen cell still calls OnFeatPicked — the engine pops its own "can't change this" message box in that case
