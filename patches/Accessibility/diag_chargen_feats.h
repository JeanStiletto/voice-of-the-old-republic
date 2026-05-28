// Diagnostic: one-shot structural dump of CSWGuiFeatsCharGen panels.
//
// Logs the four feat lists (existing / granted / available / chosen — the
// CExoUShortList fields the engine cycles through during chargen) plus the
// SkillFlowChart grid (row × col, each cell's featId + status + strref
// cross-reference). Dedups per panel pointer so re-focusing the same panel
// doesn't re-dump.
//
// No state outside the per-pointer dedup. Silently no-ops on any panel
// whose vtable isn't CSWGuiFeatsCharGen, so the caller doesn't need a kind
// check. All field reads are SEH-guarded — safe to call on partially
// initialised panels.

#pragma once

namespace acc::diag::chargen_feats {

// Called from menus.cpp::AnnouncePanelTitle on every panel-title announce.
// Vtable-gates on kVtableCSWGuiFeatsCharGen; dedups per panel pointer.
void DumpStructureIfNeeded(void* panel);

}  // namespace acc::diag::chargen_feats
