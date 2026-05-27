# engine_actionbar.h (95 lines)

Engine bindings for the player action bar (Aktionsmenü). Pure read + primitive layer. Mirrors engine_radial: resolve through the standard chain, per-column reads, drive engine widgets via vtable[15] activate path. actionbar_menu wires this into a navigable Shift+4..Shift+7 submenu.

Documents CSWGuiMainInterface.field45_0x771c[6] column layout, field5_0x74[6] descriptor lists, and DoPersonalAction/@0x0068ad60.

## Declarations (in source order)

- L28 — `namespace acc::engine_actionbar`
- L30 — `constexpr int kColumnCount = 6`
- L33 — `void* ResolveMainInterface()`
  note: borrowed pointer — re-resolve each tick, do not cache across frames
- L39 — `int VariantCount(void* mainInterface, int slot)`
  note: reads field5_0x74[slot].size — the descriptor list, not the lazily-populated field45 widgets
- L43 — `bool ReadVariantLabel(void* mainInterface, int slot, int index, char* outBuf, size_t bufSize)`
- L47 — `uint32_t ReadVariantActionId(void* mainInterface, int slot, int index)`
- L52 — `void* GetColumnActionButton(void* mainInterface, int slot)`
  note: returns action_button ptr for the column; safe to pass to ReadControlTooltip/ReadGuiString
- L62 — `bool SelectVariant(void* mainInterface, int slot, int index)`
  note: stamps *(mi + 0x1bac + slot*4) = descriptor[index].action_id; DoPersonalAction reads that field
- L65 — `bool FireSelectedVariant(void* mainInterface, int slot)`
  note: same entry point as bare 4..7; call SelectVariant first or it fires variant 0
- L91 — `bool PrepareBareDispatch(uint32_t targetClientHandle)`
  note: sets main-interface target + calls RePopulateMenus so bare 1..3/4..7 dispatch fires against the right creature
- L93 — `void LogState(void* mainInterface, const char* tag)`
