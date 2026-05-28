// Shared coordination for the patch's in-DLL submenus
// (actionbar_menu, target_action_menu, radial_menu, examine_view,
// combat_queue).
//
// Each submenu owns its own state and `IsActive()` predicate. This header
// only documents the cross-submenu mutex contracts and exposes the helpers
// that enforce them.

#pragma once

namespace acc::menus::submenu {

// actionbar_menu (Shift+4..7) and target_action_menu (Shift+1..3) are
// mutually exclusive: only one variant-cycling submenu may be armed at a
// time. Either submenu's Open() calls this helper just before flipping its
// own g_state.active so the other one tears itself down cleanly (instead of
// staying armed while the new one takes over input routing).
//
// `opening` is logged as the disarm reason ("actionbar-open" /
// "target-menu-open") so the patch log shows which side initiated the
// handover.
//
// Safe to call when neither is active — both ForceDisarm bodies are
// idempotent on already-disarmed state. Cheap predicate-then-disarm; the
// helper does not introduce any new locking.
void EnforceCombatHotkeyMutex(const char* opening);

}  // namespace acc::menus::submenu
