# diag_chargen_feats.h (22 lines)

One-shot diagnostic dump of `CSWGuiFeatsCharGen` panel structure: the four `CExoUShortList` feat lists (existing/granted/available/chosen) and the SkillFlowChart grid (row×col, each cell's featId+status+strref cross-reference). Vtable-gates internally so the caller needs no kind check; dedups per panel pointer so re-focusing doesn't re-dump; every field read is SEH-guarded, safe on partially-initialised panels.

## Declarations (in source order)

- L16 — `namespace acc::diag::chargen_feats`
- L20 — `void DumpStructureIfNeeded(void* panel)`
  note: called from menus.cpp::AnnouncePanelTitle on every panel-title announce
