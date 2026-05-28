# engine_panels.h (170 lines)

In-game panel identity registry. Classifies CSWGuiPanel pointers against named CGuiInGame slots so menu code can branch on semantic kind instead of layout. Chain: *(CAppManager**)0x7A39FC → +0x4 CClientExoApp → +0x4 Internal → +0x40 CGuiInGame*.

## Declarations (in source order)

- L11 — `namespace acc::engine`
- L15 — `enum class PanelKind`
  note: covers all CGuiInGame named slots plus heap-allocated kinds (SaveLoad, InGameLevelUp, Workbench*, PowersLevelUp, MainMenuOptions) identified structurally
- L98 — `const char* PanelKindName(PanelKind k)`
- L101 — `void* ResolveGuiInGame()`
  note: AppManager → ClientExoApp → Internal → CGuiInGame; null on any null link
- L105 — `PanelKind IdentifyPanel(void* panel)`
  note: Unknown on no match; first (panel,kind) sighting logged; subsequent calls hit a cache
- L107 — `bool IsPanelKindInGameMenu(void* panel)`
- L111 — `bool HasActiveDialogPanel()`
  note: scans panels[] not foreground — dialog panel stays in panels[] during reply-turn Fade overlay
- L115 — `bool HasActiveSubScreen()`
  note: same scan-not-foreground rationale; drilled sub-screens hide under stale Fade overlays
- L119 — `bool HasActiveMapPanel(void** outPanel = nullptr)`
- L129 — `bool IsInGameOptionsSubScreen(void* panel)`
  note: Options sub-screen has no CGuiInGame slot; classifies by presence of InGameOptions in panels[] alongside the given panel
- L135 — `bool CallPrevSWInGameGui()`
  note: CGuiInGame::PrevSWInGameGui @0x0062cdf0 — engine's "back to strip" primitive
- L142 — `bool CallHideSWInGameGui(int param_1)`
  note: CGuiInGame::HideSWInGameGui @0x0062cba0; used for full unpause + audio resync on sub-screen close
- L153 — `enum class UiBlockReason`
- L160 — `struct UiBlockState`
- L168 — `bool IsForegroundUiBlocking(UiBlockState* outState = nullptr)`
  note: blacklist not whitelist; triggers: dialog in panels[], modal_stack top is fg, or fg kind is blocking (Container/Store/Dialog*/InGameMenu/etc.)
