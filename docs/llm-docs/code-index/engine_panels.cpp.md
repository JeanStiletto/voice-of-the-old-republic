# engine_panels.cpp (1182 lines)

Implementation of engine_panels.h. Holds the CGuiInGame slot-offset table,
every structural (vtable / .gui-id) panel detector for heap-allocated
panels with no fixed slot, the unknown-panel diagnostic dumper, and the
foreground/UI-blocking + input-class primitives.

## Declarations (in source order)

- L14 — `namespace acc::engine`
- L26 — `namespace { ... }` (anonymous, structural detectors)
- L28 — `void* FindControlByGuiId(void* panel, int id)`
  note: scans panel.controls[] up to 64 entries for a child with .gui-id == id.
- L49 — `bool ControlHasVtable(void* control, uintptr_t expected)`
- L59 — `bool IsSaveLoadStructural(void* panel)`
  note: requires listbox@ID0 + Button vtable at IDs 11/12/14 — disambiguates from Workbench upgrade, whose ID 11 is a LabelHilight (LBL_UPGRADE44), not a Button.
- L101 — `bool IsWorkbenchUpgradeStructural(void* panel)`
  note: listbox@ID0 + Button vtable at IDs 24 (BTN_ASSEMBLE) and 15 (slot).
- L128 — `bool IsWorkbenchItemsStructural(void* panel)`
- L166 — `const uintptr_t kVtableCSWGuiUpgradeSelection = R(0x007571b0)`
- L168 — `bool IsWorkbenchSelectStructural(void* panel)`
  note: vtable-equality primary check, ID-based structural fallback (Button@0/9/10) kept in case a build relocates the vtable.
- L192 — `const uintptr_t kVtableCSWGuiLevelUpPanel = R(0x00759568)`
- L194 — `bool IsLevelUpStructural(void* panel)`
- L212 — `const uintptr_t kVtableCSWGuiCustomPanel = R(0x007595e0)`, `kVtableCSWGuiQuickPanel = R(0x00759668)`
- L215 — `bool IsCharGenStructural(void* panel)`
- L231 — `const uintptr_t kVtableCSWGuiOptions = R(0x00758838)`
- L233 — `bool IsMainMenuOptionsStructural(void* panel)`
- L249 — `const uintptr_t kVtableCSWGuiMainMenu = R(0x00752f70)`
- L251 — `bool IsMainMenuStructural(void* panel)`
- L265 — `const uintptr_t kVtableCSWGuiPazaakStart = R(0x007532e8)`
- L267 — `bool IsPazaakStartStructural(void* panel)`
- L281 — `const uintptr_t kVtableCSWGuiWagerPopup = R(0x007534c8)`
- L283 — `bool IsPazaakWagerStructural(void* panel)`
- L298 — `const uintptr_t kVtableCSWGuiQuestItem = R(0x00757c20)`
- L300 — `bool IsQuestItemStructural(void* panel)`
- L314 — `const uintptr_t kVtableCSWGuiScriptSelect = R(0x007590a8)`
- L316 — `bool IsScriptSelectStructural(void* panel)`
- L336 — `bool IsPowersLevelUpStructural(void* panel)`
  note: vtable-equality primary (kVtableCSWGuiPowersLevelUp), ID fallback (ListBox@6/7 + Button@11/12) — must probe before WorkbenchSelect's loose fallback or the powers screen gets stolen.
- L372 — `struct OptionsSubScreenVtable { uintptr_t vtable; PanelKind kind; }`
- L376 — `static const OptionsSubScreenVtable kOptionsSubScreenVtables[]`
  note: nine title-screen Options sub-screen vtables captured 2026-06-13 from PanelProbe first-sight dumps.
- L392 — `PanelKind IdentifyOptionsSubScreen(void* panel)`
- L406 — `static constexpr uintptr_t kAddrAppManagerPtr = 0x007A39FC` + CGuiInGame chain offsets
- L421 — `constexpr size_t kNoSlotOffset = -1`
- L423 — `struct PanelKindOffset { size_t offset; PanelKind kind; const char* name; }`
- L429 — `static const PanelKindOffset kPanelKindOffsets[]`
  note: ~55-row table; slotted CGuiInGame kinds plus kNoSlotOffset rows for every heap-allocated kind (name resolution only, no slot scan).
- L504 — `const char* PanelKindName(PanelKind k)`
- L512 — `void* ResolveGuiInGame()`
- L525 — `bool ReadDialogReplyText(int replyIndex, char* outBuf, size_t bufSize)`
- L550 — `int ReadDialogReplyCount()`
- L563 — `struct PanelKindCacheEntry` + `g_panelKindCache[32]` (dedup first-sight logging)
- L583 — `namespace { ... }` (unknown-panel probe, dedup by vtable not pointer)
- L589 — `bool IsVtableAlreadyDumped(uintptr_t vt)`
- L596 — `void RememberDumpedVtable(uintptr_t vt)`
- L606 — `void LogUnknownPanelDiagnostics(void* panel)`
  note: dumps panel vtable + controls.size + per-control {vtable, .gui-id, rendered text}; capped 32 controls, dedup by vtable so re-opening a known screen doesn't re-log.
- L669 — `PanelKind IdentifyPanel(void* panel)`
  note: slot-table scan first; on miss runs structural detectors in tightest-first order (WorkbenchUpgrade/Items → PowersLevelUp → WorkbenchSelect → SaveLoad → LevelUp → CharGen → MainMenuOptions → MainMenu → Pazaak* → QuestItem → ScriptSelect → Options sub-screens); last resort logs diagnostics and returns Unknown.
- L779 — `bool IsPanelKindInGameMenu(void* panel)`
- L783 — `bool IsMainMenuOptionsSubScreen(PanelKind k)`
- L800 — `bool IsModalPopupPanel(PanelKind k)`
- L815 — `bool HasActiveDialogPanel()`
- L839 — `bool HasActiveBarkBubble()`
- L859 — `static constexpr uintptr_t kAddrPrevSWInGameGui = 0x0062cdf0`
- L862 — `bool CallPrevSWInGameGui()`
- L884 — `static constexpr uintptr_t kAddrHideSWInGameGui = 0x0062cba0`
- L887 — `bool CallHideSWInGameGui(int param_1)`
- L914 — `static constexpr uintptr_t kAddrSetGlobalDialogState = 0x0062ec60`
- L917 — `bool SetGlobalDialogState(int state)`
- L935 — `static constexpr uintptr_t kAddrSetInputClass = 0x005eda60`
- L941 — `bool CloseInGameMenuToWorld()`
  note: replicates every in-game menu tab's own Esc: HideSWInGameGui(0) then SetInputClass(client,0,1) — HideSWInGameGui alone leaves input_class stuck (patch-20260609-115959.log).
- L977 — `int GetInputClass()`
- L1001 — `bool SetGuiInputClass(int klass)`
- L1020 — `bool HasActiveMapPanel(void** outPanel)`
- L1046 — `bool HasActiveLevelUpPanel()`
- L1068 — `bool IsInGameOptionsSubScreen(void* panel)`
- L1093 — `bool HasActiveSubScreen()`
- L1121 — `bool IsForegroundUiBlocking(UiBlockState* outState)`
  note: three checks in order — HasActiveDialogPanel(), modal_stack[top]==fgPanel, then a fg-kind blacklist switch.
