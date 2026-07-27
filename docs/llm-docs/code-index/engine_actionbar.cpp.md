# engine_actionbar.cpp (313 lines)

Read + drive primitives for the player action bar (CSWGuiMainInterface personal-action columns), mirroring engine_radial/engine_picker's chain-resolution pattern. Backs actionbar_menu's Shift+4..7 navigable submenu. Talks to engine_player (AppManager chain), engine_rebase (address rebasing), log.

## Declarations (in source order)

- L16/19 — `kGuiInGameMainInterfaceOffset = 0x90`, `kInternalGuiInGameOffset = 0x040`
- L27 — `kSelectedActionIdArrayOffset = 0x1bac` — 6 int32s, one per column, "currently-selected variant action_id"
- L32-35 — `kPersonalListsOffset=0x74`, stride `0x0C`, data/size sub-offsets — CExoArrayList<CSWGuiInterfaceAction>[6]
- L38-40 — `kIfActionLabelOffset=0x00`, `kIfActionIdOffset=0x08`, `kIfActionStride=0x38`
- L45 — `const uintptr_t kAddrDoPersonalAction = R(0x0068ad60)`
- L51 — `const uintptr_t kAddrSetMainInterfaceTarget = R(0x0062b000)` — thin forwarder to CSWGuiMainInterface::SetTarget
- L58 — `const uintptr_t kAddrRePopulateMainInterface = R(0x0062b050)` — forwards to PopulateMenus@0x00689d80
- L60-69 — `PFN_DoPersonalAction`, `PFN_SetMainInterfaceTarget`, `PFN_RePopulateMainInterface` typedefs
  note: SetMainInterfaceTarget callee purges 8 bytes though only param_1 is used — single-arg typedef under-pushes and corrupts caller frame; requires a matching padded second dword
- L72/84/95/106 — `GetClientExoApp`, `GetClientExoAppInternal`, `GetGuiInGame`, `GetMainInterface` — local chain helpers
- L117/128 — `ReadInt32`, `ReadPtr` — SEH-guarded raw field readers
- L140 — `void* DescriptorAddr(void* mi, int slot, int index)`
- L157 — `bool ReadCExoStringLocal(void* base, size_t offset, char* outBuf, size_t bufSize)`
- L183 — `void* ResolveMainInterface()` (public)
- L190 — `int VariantCount(void* mi, int slot)` (public) — reads field5_0x74[slot].size
- L200 — `bool ReadVariantLabel(...)` (public)
- L209 — `uint32_t ReadVariantActionId(...)` (public)
- L217 — `void* GetColumnActionButton(void* mi, int slot)` (public) — field45_0x771c[6], stride 0x71C
- L228 — `bool SelectVariant(void* mi, int slot, int index)` (public)
  note: stamps kSelectedActionIdArrayOffset with the descriptor's action_id — bypasses the labelled OnActionUp/DownArrowPressed handlers (gate on uninitialised is_active; OnActionDownArrow is mislabelled)
- L244 — `bool FireSelectedVariant(void* mi, int slot)` (public) — calls DoPersonalAction; param_2 unused
- L260 — `void LogState(void* mi, const char* tag)` (public)
- L277 — `bool PrepareBareDispatch(uint32_t targetClientHandle)` (public)
  note: runs SetMainInterfaceTarget + RePopulateMainInterface synchronously so bare 1..7 dispatch fires against a freshly-Q/E-selected target (Q/E alone only writes field1_0x64, doesn't repopulate — stale action_lists creature_ids otherwise silently bail dispatch)
