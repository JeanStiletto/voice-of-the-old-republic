// KOTOR 2 focus announce — see menus_focus_k2.cpp for why this is a separate,
// deliberately minimal path rather than the KOTOR 1 handler.

#pragma once

namespace acc::menus::k2 {

// Read the newly focused control's inline caption and speak it. Uses only
// offsets verified against the KOTOR 2 binary; performs no engine calls and no
// panel classification, so it cannot reach an unresolved address.
void AnnounceFocus(void* panel, void* control, void* caller);

}  // namespace acc::menus::k2
