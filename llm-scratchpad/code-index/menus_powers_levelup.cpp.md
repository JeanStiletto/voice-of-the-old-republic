# menus_powers_levelup.cpp (511 lines)

PowersLevelUp panel (pwrlvlup.gui CSWGuiSkillFlow tree) accessibility implementation. Mirrors the chargen_feats shape for the level-up powers tree.

## Declarations (in source order)

- L32 — `constexpr int kBtnAcceptId = 11`, `kBtnBackId = 12`, and other .gui IDs (anonymous ns)
- L40 — `struct ChartRow` (anonymous ns)
- L46 — `struct ButtonRow` (anonymous ns)
- L56 — `constexpr ButtonRow kButtonRows[]` (anonymous ns)
- L64 — `ChartRow s_chartRows[]`, `s_chartRowCount`, `s_curRow`, `s_curCol`, `s_boundPanel`, `s_boundRowsPtr`, `s_boundRowsCount` — 2D nav state (anonymous ns)
- L81 — `int TotalRowCount()`, `bool IsButtonRow(int r)`, `bool ColFilled(int r, int c)` (anonymous ns)
- L91 — `int FirstFilledCol(int r)` (anonymous ns)
- L101 — `int NearestFilledCol(int r, int want)` (anonymous ns)
- L112 — `bool ReadChartBinding(void* panel, void*& outRows, int& outCount)` (anonymous ns)
- L135 — `void WalkChartRows(void* rows, int nRows)` (anonymous ns)
- L179 — `void EnsureBound(void* panel)` (anonymous ns)
- L221 — `unsigned char ReadCellStatus(int r, int c)` (anonymous ns)
- L237 — `const char* StatusWord(unsigned char status)` (anonymous ns)
- L250 — `bool ReadLabelText(void* lab, char* out, size_t outN)` (anonymous ns)
- L269 — `bool ReadButtonText(void* btn, char* out, size_t outN)` (anonymous ns)
- L288 — `bool ReadPowerName(void* panel, char* out, size_t outN)` — reads power name from panel's name label (anonymous ns)
- L296 — `bool ReadDescription(void* panel, char* out, size_t outN)` (anonymous ns)
- L318 — `void DriveEngineSelection(void* panel, unsigned short featId)` (anonymous ns)
- L345 — `void AnnounceFocused(void* panel)` (anonymous ns)
- L396 — `void NavVertical(void* panel, bool down)` (anonymous ns)
- L420 — `void NavHorizontal(void* panel, bool right)` (anonymous ns)
- L431 — `bool acc::menus::powers_levelup::IsPowersLevelUpPanel(void* panel)`
- L435 — `const char* acc::menus::powers_levelup::GetTitleOverride(void* panel)` — returns thread_local buffer with formatted "N Kraftpunkte übrig" title
- L445 — `bool acc::menus::powers_levelup::HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)`
