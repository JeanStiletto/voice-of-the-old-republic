// Diagnostic: one-shot structural dump of CSWGuiFeatsCharGen panels.
//
// Logs the four feat lists (existing / granted / available / chosen — the
// CExoUShortList fields the engine cycles through during chargen) plus the
// SkillFlowChart grid (row × col, each cell's featId + status + strref
// cross-reference). Dedups per panel pointer so re-focusing the same panel
// doesn't re-dump.
//
// Purpose: planning data for extending the listbox spec table to cover the
// chargen-feats main panel (not just the SkillInfoBox overlay). Once that
// picker spec lands we can retire this TU.
//
// No state outside the per-pointer dedup. Silently no-ops on any panel
// whose vtable isn't CSWGuiFeatsCharGen, so the caller doesn't need a kind
// check. All field reads are SEH-guarded — safe to call on partially
// initialised panels.
//
// Lifted out of menus_listbox.cpp to keep that TU focused on the listbox
// spec dispatcher and follow the diag_* convention (see diag_input_pipeline).
// Removal: when the picker spec gets extended, delete this TU + its header
// + the call site in menus.cpp::AnnouncePanelTitle.

#pragma once

namespace acc::diag::chargen_feats {

// Called from menus.cpp::AnnouncePanelTitle on every panel-title announce.
// Vtable-gates on kVtableCSWGuiFeatsCharGen; dedups per panel pointer.
void DumpStructureIfNeeded(void* panel);

}  // namespace acc::diag::chargen_feats
