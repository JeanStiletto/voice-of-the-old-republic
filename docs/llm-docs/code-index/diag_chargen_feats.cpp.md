# diag_chargen_feats.cpp (177 lines)

Implements the chargen-feats panel dump. `FeatNameStrref` resolves a feat's `name_strref` from the global rules table for log cross-reference only. `DumpUshortListSEH` prints up to 96 entries of a `CExoUShortList` with each value's resolved strref; `DumpChartCells` walks the `SkillFlowChart` rows (up to 256), each row having a fixed number of columns (`kSkillFlowColumnsPerRow`), printing featId/status/strref per cell or "empty" when the cell equals the empty-feat-id sentinel. Every accessor is individually `__try`/`__except`-wrapped so a partial dump still emits on a mid-walk fault.

## Declarations (in source order)

- L12 — `namespace acc::diag::chargen_feats`
- L16 — `void* s_loggedFeatsPanel`
- L21 — `int FeatNameStrref(unsigned short featId)`
- L39 — `void DumpUshortListSEH(unsigned char* base, size_t dataOff, size_t sizeOff, const char* tag)`
- L75 — `void DumpChartCells(void* fcp)`
  note: reads chart.rows (data+size), selectedCol/selectedRow, then per-row per-column featId+status via kSkillFlowFirstColumnOffset + c*kSkillFlowColumnStride
- L142 — `void DumpStructureIfNeeded(void* panel)`
  note: vtable-gates on kVtableCSWGuiFeatsCharGen; dedups via s_loggedFeatsPanel
