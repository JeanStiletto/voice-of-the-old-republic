# menus_abilities.h (46 lines)

Public surface for the in-game Fähigkeiten (Abilities) screen handler — a
view-only tabbed character screen (Powers/Skills/Talents) driving a shared
LB_ABILITY listbox + detail repaint, distinct from the chargen/level-up
button-grid shape. Rides `ListBoxPanelSpec` (menus_listbox.cpp) for Up/Down
and the description-peek registry (peek_description.cpp) for Shift+Up/Down.

## Declarations (in source order)

- L21 — `namespace acc::menus::abilities`
- L24 — `bool IsAbilitiesPanel(void* panel)`
- L29 — `void RefreshDetail(void* panel)`
  note: used as the peek-refresh hook so Shift+Up/Down matches the focused entry
- L42 — `bool HandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)`
  note: mirrors the TryHandleInput contract — true + outRv set means consumed
