# menus_chargen_feats.cpp (530 lines)

Chargen Feats panel (CSWGuiFeatsCharGen SkillFlow tree) accessibility implementation. Manages 2D row/column navigation, engine-selection sync, and announcement.

## Declarations (in source order)

- L32 — `constexpr int kBtnRecommendedId = 9`, `kBtnAcceptId = 11`, `kBtnBackId = 12` — .gui IDs for the three action buttons (anonymous ns)
- L45 — `struct ChartRow` — holds a pointer to a SkillFlow row and its cell descriptors (anonymous ns)
- L51 — `struct ButtonRow` — maps a row index to its .gui button ID (anonymous ns)
- L56 — `constexpr ButtonRow kButtonRows[]`, `kButtonRowCount` — Empfohlen / Annehmen / Zurück (anonymous ns)
- L64 — `ChartRow s_chartRows[kMaxChartRows]`, `s_chartRowCount`, `s_curRow`, `s_curCol` — 2D navigation state (anonymous ns)
- L77 — `void* s_boundPanel`, `void* s_boundRowsPtr`, `int s_boundRowsCount` (anonymous ns)
- L81 — `int TotalRowCount()`, `bool IsButtonRow(int r)`, `bool ColFilled(int r, int c)` — inline helpers (anonymous ns)
- L91 — `int FirstFilledCol(int r)` (anonymous ns)
- L101 — `int NearestFilledCol(int r, int want)` (anonymous ns)
- L112 — `bool ReadChartBinding(void* panel, void*& outRows, int& outCount)` — reads CSWGuiFeatsCharGen.chart rows pointer and count via SEH (anonymous ns)
- L135 — `void WalkChartRows(void* rows, int nRows)` — populates s_chartRows from the engine's SkillFlow row array (anonymous ns)
- L179 — `void EnsureBound(void* panel)` — calls ReadChartBinding + WalkChartRows on panel change or cache miss (anonymous ns)
- L221 — `unsigned char ReadCellStatus(int r, int c)` — reads the engine's status byte for a chart cell (anonymous ns)
- L237 — `const char* StatusWord(unsigned char status)` — maps status byte to localised string ("verfügbar", "gewählt", etc.) (anonymous ns)
- L250 — `bool ReadLabelText(void* lab, char* out, size_t outN)` (anonymous ns)
- L269 — `bool ReadButtonText(void* btn, char* out, size_t outN)` (anonymous ns)
- L288 — `bool ReadNameLabel(void* panel, char* out, size_t outN)` — reads the feat name from the panel's name label (anonymous ns)
- L296 — `bool ReadDescription(void* panel, char* out, size_t outN)` — reads the feat description from the panel's description label (anonymous ns)
- L318 — `void DriveEngineSelection(void* panel, unsigned short featId)` — calls engine's chart-select thiscall to move the engine's cursor to the given feat (anonymous ns)
- L345 — `void AnnounceFocused(void* panel)` — speaks name, status, and description for the currently focused chart cell or button row (anonymous ns)
- L396 — `void NavVertical(void* panel, bool down)` — moves s_curRow ±1 with clamping; calls EnsureBound, DriveEngineSelection, AnnounceFocused (anonymous ns)
- L420 — `void NavHorizontal(void* panel, bool right)` — moves s_curCol within the row; wraps to nearest filled column (anonymous ns)
- L448 — `bool acc::menus::chargen_feats::IsChargenFeatsPanel(void* panel)`
- L459 — `bool acc::menus::chargen_feats::HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)`
