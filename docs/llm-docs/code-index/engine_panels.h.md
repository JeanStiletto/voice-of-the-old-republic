# engine_panels.h (298 lines)

In-game panel identity registry. Classifies CSWGuiPanel pointers against
named CGuiInGame slots (plus heap-allocated kinds identified structurally)
so menu code can branch on semantic kind instead of layout. Chain:
`*(CAppManager**)0x7A39FC → +0x4 CClientExoApp → +0x4 Internal → +0x40
CGuiInGame*`, re-resolved every call. Also owns the render-independent
dialog-reply-text reader, the foreground/UI-blocking model, and input-class
plumbing used by engine_levelup and the sub-screen close path.

## Declarations (in source order)

- L11 — `namespace acc::engine`
- L15 — `enum class PanelKind`
  note: persistent HUD, modal HUD screens, dialogue surfaces, popups/overlays, dialogue auxiliary routing panels (DialogMessagesAux/DialogMessages), heap-allocated no-slot kinds (SaveLoad, InGameLevelUp, CharGen, Workbench*, PowersLevelUp, MainMenu(Options), Pazaak*, InGameQuestItems, ScriptSelect), and the nine title-screen Options sub-screens (Sound/Graphics/AutoPause/Feedback/Game/Mouse/Keyboard + Advanced variants).
- L162 — `const char* PanelKindName(PanelKind k)`
- L165 — `void* ResolveGuiInGame()`
  note: AppManager → ClientExoApp → Internal → CGuiInGame; null on any null link.
- L173 — `bool ReadDialogReplyText(int replyIndex, char* outBuf, size_t bufSize)`
  note: reads CGuiInGame's render-independent reply-text CExoString[] (field70_0x118), NOT the reply listbox row labels — those go empty for off-page rows (the DialogCinematicCopy silent-dropped-entries bug).
- L179 — `int ReadDialogReplyCount()`
- L183 — `PanelKind IdentifyPanel(void* panel)`
  note: Unknown on no match; first (panel,kind) sighting logged, subsequent calls hit a cache.
- L185 — `bool IsPanelKindInGameMenu(void* panel)`
- L194 — `bool IsModalPopupPanel(PanelKind k)`
  note: engine-pushed standalone modals whose Esc needs our close-route (engine's own Esc opens quit-confirm instead of backing out); distinct question from IsModalTextPanel.
- L201 — `bool IsMainMenuOptionsSubScreen(PanelKind k)`
  note: groups the nine title-screen Options sub-screen kinds for shared chain-side behaviour (wider cycle-flanker squash for spinner rows).
- L205 — `bool HasActiveDialogPanel()`
  note: scans panels[], not foreground — reply turns swap fg to a Fade overlay while the CSWGuiDialog* panel stays in panels[].
- L212 — `bool HasActiveBarkBubble()`
- L216 — `bool HasActiveSubScreen()`
- L220 — `bool HasActiveMapPanel(void** outPanel = nullptr)`
- L226 — `bool HasActiveLevelUpPanel()`
  note: debounces Shift+L — ShowLevelUpGUI doesn't dedupe and key-repeat would stack duplicate un-closeable modals.
- L236 — `bool IsInGameOptionsSubScreen(void* panel)`
  note: Options sub-screen has no CGuiInGame slot; Esc gate routes through QueueActivate(Schliess.) since vanilla Esc here trip a stack-cookie smash matching the LevelUp Annehmen signature.
- L242 — `bool CallPrevSWInGameGui()`
  note: CGuiInGame::PrevSWInGameGui @0x0062cdf0 — "back to strip" primitive.
- L249 — `bool CallHideSWInGameGui(int param_1)`
  note: CGuiInGame::HideSWInGameGui @0x0062cba0 — full unpause + audio resync, unlike the MessageBoxModal close path.
- L254 — `bool SetGlobalDialogState(int state)`
  note: force-clears the engine's conversation-active bit after cancelling a blocked walk-to-talk approach.
- L262 — `bool CloseInGameMenuToWorld()`
  note: HideSWInGameGui(0) + SetInputClass(0,1), replicating a menu tab's own Escape exactly — bare CallHideSWInGameGui(0) alone leaves input_class stuck.
- L265 — `int GetInputClass()`
- L270 — `bool SetGuiInputClass(int klass)`
  note: used by engine_levelup to put the wizard into GUI input mode without the full ShowSWInGameGui.
- L281 — `enum class UiBlockReason { NotBlocked, DialogInStack, ForegroundModal, ForegroundBlockingKind }`
- L288 — `struct UiBlockState`
- L296 — `bool IsForegroundUiBlocking(UiBlockState* outState = nullptr)`
  note: blacklist not whitelist (panels[] keeps stale closed entries at the top for seconds); triggers on dialog-in-stack, modal_stack top, or fg kind in {Container, Store, Examine, Dialog*, TutorialBox, MessageBoxModal, StatusSummary, AreaTransition, InGameMenu}.
