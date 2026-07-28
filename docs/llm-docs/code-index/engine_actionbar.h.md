# engine_actionbar.h (96 lines)

Engine bindings for the player action bar (Aktionsmenü) — CSWGuiMainInterface's 6 personal-action columns, each with an action_button/label/up/down/is_action widget set plus a backing CExoArrayList of variants. Pure read + primitive layer mirroring engine_radial.

## Declarations (in source order)

- L30 — `constexpr int kColumnCount = 6`
- L33 — `void* ResolveMainInterface()`
  note: borrowed pointer — re-resolve each tick, don't cache
- L39 — `int VariantCount(void* mainInterface, int slot)`
  note: descriptor list is the source of truth; the is_action field (+0x718) reads pointer garbage and field45 widgets populate lazily on render
- L43 — `bool ReadVariantLabel(void* mainInterface, int slot, int index, char* outBuf, size_t bufSize)`
- L47 — `uint32_t ReadVariantActionId(void* mainInterface, int slot, int index)`
- L52 — `void* GetColumnActionButton(void* mainInterface, int slot)`
- L62 — `bool SelectVariant(void* mainInterface, int slot, int index)`
  note: bypasses the engine's labelled arrow handlers — gate on uninitialised is_active, plus OnActionDownArrow is mislabelled (calls CSWGuiTargetActionMenu::SelectNextAction on `this`)
- L65 — `bool FireSelectedVariant(void* mainInterface, int slot)` — must call SelectVariant first or fires variant 0
- L91 — `bool PrepareBareDispatch(uint32_t targetClientHandle)`
  note: targetClientHandle is CLIENT-side (0x80000000 high bit); pass kInvalidObjectId when no narrated target so PopulateMenus leaves hostile-action creature_ids unresolved (dispatch silently no-ops) while self-buff items still fire
- L93 — `void LogState(void* mainInterface, const char* tag)`
