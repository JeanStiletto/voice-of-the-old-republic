// In-game panel identity registry.
//
// Layer: engine/ (pure read-side helpers; no menu-side state, no engine
// re-entry). Classifies any CSWGuiPanel pointer against the named slots in
// CGuiInGame so menu-side code can branch on semantic kind (InGameMenu,
// DialogCinematic, TutorialBox, ...) instead of guessing from layout.
//
// Resolution chain (per docs/llm-docs/re/swkotor.exe.h):
//   *(CAppManager**)0x7A39FC                    → CAppManager*
//   CAppManager.client            (+0x04)       → CClientExoApp*
//   CClientExoApp.internal        (+0x04)       → CClientExoAppInternal*
//   CClientExoAppInternal.gui_in_game (+0x40)   → CGuiInGame*
//
// Each step is a single indirect read; we re-resolve on every call so a
// module transition that destroys/recreates the in-game GUI doesn't leave
// us holding a stale pointer.
//
// `PanelKind` is file-scope (not namespaced) for callsite brevity, matching
// the convention used by `engine_input.h`'s `kInput*` codes. Functions live
// under `acc::engine` like the rest of the engine layer.

#pragma once

namespace acc::engine {

// Semantic identity of an in-game GUI panel. The list mirrors the named
// slots in CGuiInGame (swkotor.exe.h:10219); add a new value here AND a
// matching row in `kPanelKindOffsets[]` (engine_panels.cpp) to teach the
// classifier about a new panel kind.
enum class PanelKind {
    Unknown = 0,
    // Persistent always-on UI
    MainInterface,
    InGameMenu,
    // Modal screens accessible from the HUD
    InGameEquip,
    InGameInventory,
    InGameCharacter,
    InGameAbilities,
    InGameMessages,
    InGameJournal,
    InGameMap,
    InGameOptions,
    InGamePause,
    InGameGalaxyMap,
    // Dialogue surfaces
    DialogCinematic,
    DialogCinematicCopy,
    DialogComputer,
    DialogComputerCamera,
    DialogLetterbox1,
    DialogLetterbox2,
    DialogLetterbox3,
    BarkBubble,
    // Popups / overlays
    TutorialBox,
    // Named MessageBoxModal (not MessageBox, the engine's field name) to dodge
    // the Win32 winuser.h macro `#define MessageBox MessageBoxA`. With the bare
    // name, any TU that included <windows.h> before this header would silently
    // rename the enum value to `MessageBoxA`, breaking consumers that didn't.
    MessageBoxModal,
    SkillInfoBox,
    ControllerLossBox,
    StatusSummary,
    Examine,
    Container,
    CreateItemMenu,
    CreateItemSubMenu,
    Fade,
    LoadModuleDebugMenu,
    PowersFeatsSkillsDebugMenu,
    PartySelection,
    Store,
    SoloModeQuery,
    AreaTransition,
    // Dialogue auxiliary panels (the panels that route input during a
    // CSWGuiDialogCinematic conversation — separate from the rendering
    // panel that holds the message text).
    DialogMessagesAux,   // 0xf8: void* messages?
    DialogMessages,      // 0xfc: CGuiInGameDialogMessage*
};

// Return the registered name for `k`, or "Unknown" / "?" if not found.
const char* PanelKindName(PanelKind k);

// Resolve the CGuiInGame singleton via the AppManager → ClientExoApp →
// Internal indirection chain. Returns nullptr at any null link (DLL attach
// timing, between modules, title screen with no game loaded, etc.).
void* ResolveGuiInGame();

// Compare `panel` against every named slot in CGuiInGame; return its
// PanelKind on match, PanelKind::Unknown otherwise (including when
// CGuiInGame isn't resolvable yet). First sighting of a (panel, kind) pair
// is logged once via acclog; subsequent identifications are cache hits.
PanelKind IdentifyPanel(void* panel);

// Convenience predicate: true iff `panel` identifies as PanelKind::InGameMenu.
bool IsPanelKindInGameMenu(void* panel);

}  // namespace acc::engine
