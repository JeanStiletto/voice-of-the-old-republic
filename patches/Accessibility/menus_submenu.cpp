#include "menus_submenu.h"

#include "actionbar_menu.h"
#include "target_action_menu.h"

namespace acc::menus::submenu {

void EnforceCombatHotkeyMutex(const char* opening) {
    if (acc::actionbar_menu::IsActive()) {
        acc::actionbar_menu::ForceDisarm(opening);
    }
    if (acc::target_action_menu::IsActive()) {
        acc::target_action_menu::ForceDisarm(opening);
    }
}

}  // namespace acc::menus::submenu
