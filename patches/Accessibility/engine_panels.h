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

    // Heap-allocated panels with no fixed CGuiInGame slot. Identified
    // structurally (vtable/.gui-id signature) inside IdentifyPanel after
    // the slot table misses, so menu-side code can branch on kind instead
    // of reaching for a separate IsXxxPanel predicate.
    SaveLoad,            // CSWGuiSaveLoad: load/save dialog (saveload.gui)
    InGameLevelUp,       // CSWGuiLevelUpPanel: vtable 0x00759568 — opened
                         //   from Charakterblatt's "Levelaufst." button.
                         //   Hosts the 5 step navigation buttons (Kräfte,
                         //   Attribute, Fähigkeiten, Talente, Annehmen) +
                         //   Zurück. Each step's click handler gates on
                         //   `button->is_active != 0` (decompiled at
                         //   0x006ee350..0x006ee500), so disabled steps
                         //   silently drop the click — the disabled-state
                         //   suffix in the extract path tells the user.

    // Workbench panels (upgrade.gui / upgradeitems.gui / upgradesel.gui).
    // All three are heap-allocated by the engine when the user picks
    // "Werkbank benutzen" from the placeable conversation; none has a
    // CGuiInGame slot, so they're identified structurally by control-ID
    // signature.
    //
    // WorkbenchSelect: category picker (upgradesel.gui). 11 controls with
    //   tagged buttons BTN_RANGED / BTN_LIGHTSABER / BTN_MELEE / BTN_ARMOR
    //   at IDs 0/2/4/6 (overlap with ProtoItem icon labels at 1/3/5/7).
    //   Currently identified only for log tagging — generic chain nav
    //   handles it correctly already.
    //
    // WorkbenchItems: per-category item picker (upgradeitems.gui). 5 controls,
    //   LB_ITEMS at ID 0 + LB_DESCRIPTION at ID 2. Empty-state speech goes
    //   here when the user has no upgradable weapons of the chosen category.
    //
    // WorkbenchUpgrade: slot detail (upgrade.gui). 29 controls including the
    //   7 BTN_UPGRADE3X/4X slot buttons at IDs 12..18. Collided with SaveLoad
    //   in patch-20260521-175339.log because the 4-ID signature (0/11/12/14)
    //   matched coincidentally — fixed by tightening SaveLoad to require
    //   ID 11 to be a Button (saveload.gui's BTN_DELETE) vs Workbench's
    //   ID 11 = LBL_UPGRADE44 (Label).
    WorkbenchSelect,
    WorkbenchItems,
    WorkbenchUpgrade,
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

// True iff any panel in the manager's panels[] identifies as one of the
// CSWGuiDialog* surfaces (DialogCinematic, DialogCinematicCopy,
// DialogComputer, DialogComputerCamera). Used by input gates that can't
// rely on GetForegroundPanel alone — during reply turns the engine swaps
// fg to a transient Fade overlay while the actual dialog panel stays in
// panels[]. Mirrors the panels[] scan in MonitorDialogReplies (menus.cpp);
// dialog panels are removed from the stack when conversations end (proven
// by that monitor's disarm path), so this does not stale-block.
bool HasActiveDialogPanel();

// True iff any panel in the manager's panels[] identifies as a drilled
// in-game sub-screen (InGameInventory, InGameMap, InGameEquip,
// InGameCharacter, InGameAbilities, InGameJournal, InGameOptions,
// InGameMessages). Used for the same reason as HasActiveDialogPanel:
// when a sub-screen is drilled, the engine reports a stale Fade overlay
// as foreground while the actual sub-screen panel stays in panels[],
// so a fg.kind check alone misses it. Sub-screens are removed from the
// stack on close (the SwitchToSWInGameGui drill chain pops the previous
// one before adding the next), so this does not stale-block.
bool HasActiveSubScreen();

// True iff PanelKind::InGameMap sits somewhere in the manager's panels[]
// stack. Mirrors HasActiveSubScreen but specialised to the area-map sub-
// screen; the InGameMap panel hides UNDER the InGameMenu strip when
// drilled in, so a foreground check alone misses it (same reason
// HasActiveSubScreen / map_ui_cursor::IsMapPanelActive scan the stack).
//
// `outPanel`, if non-null, receives the matching panel pointer (or
// nullptr when no map panel is active). Useful for callers that need to
// reach into the panel's CSWGuiInGameMap layout afterwards (e.g. the
// map-note list).
bool HasActiveMapPanel(void** outPanel = nullptr);

// True iff `panel` is a heap-allocated sub-screen sitting on top of the
// in-game InGameOptions strip (Spieleinstellungen / Grafik / Sound /
// Auto-Pause / Feedback / Tastenbelegung / Mauseinstellungen). Detection:
// the InGameOptions parent panel is present in manager's panels[], and
// `panel` is non-null and is not that parent. These sub-screens have no
// CGuiInGame slot and IdentifyPanel returns Unknown for them, so
// classifying them positively needs the panels[]-stack context.
//
// Used by the Esc gate in menus.cpp to route Esc through
// QueueActivate(Schliess.) — the engine's vanilla Esc-close path for
// these sub-screens triggers a stack-cookie smash (0xc0000409) that
// matches the LevelUp Annehmen signature.
bool IsInGameOptionsSubScreen(void* panel);

// Pop the current in-game sub-screen via CGuiInGame::PrevSWInGameGui
// (0x0062cdf0 — the engine-internal counterpart to SwitchToSWInGameGui).
// This is the engine's own "back to strip" primitive: it removes the
// sub-screen from panels[] cleanly across all kinds (Options, Map, Equip,
// Abilities, Journal, Character, Messages), not relying on the per-screen
// Schliess button onClick (which silent-fails on InGameOptions — the
// Schliess handler there reorders panels[] without actually popping).
//
// Returns true on dispatch (CGuiInGame resolvable + no-op gate cleared),
// false if CGuiInGame can't be resolved yet (DLL attach, between modules).
// The caller is responsible for clearing local drill state.
bool CallPrevSWInGameGui();

// Invoke CGuiInGame::SwitchToSWInGameGui (0x0062cf10) directly with the
// requested GUI_id. The engine indexes sub-screens 0..7 in CGuiInGame slot
// order (offsets 0x0c..0x28):
//
//   0 = InGameEquip       (0x0c)
//   1 = InGameInventory   (0x10)
//   2 = InGameCharacter   (0x14)
//   3 = InGameAbilities   (0x18)
//   4 = InGameMessages    (0x1c)
//   5 = InGameJournal     (0x20)
//   6 = InGameMap         (0x24)
//   7 = InGameOptions     (0x28)
//
// Our OnSwitchToSWInGameGui detour (mid-function at 0x62cf2d) pops any
// active sub-screen first, so the new one lands on a clean panels[]. The
// caller does not need to invoke PrevSWInGameGui itself.
//
// Returns true on dispatch, false if CGuiInGame can't be resolved.
bool CallSwitchToSWInGameGui(int guiId);

// Invoke CGuiInGame::HideSWInGameGui (0x0062cba0) directly with the given
// param_1. This is the engine's "close current sub-screen" primitive —
// CSWGuiInGameOptions::HandleInputEvent calls it with param_1=0 when the
// user presses Esc on the in-game save/load menu. That path produces a
// full unpause + audio resync; the equivalent action on a MessageBoxModal
// close (Alt+F4 quit-confirm) goes through a different code path that
// skips HideSWInGameGui entirely and leaves the world half-paused.
// Calling this on modal-pop is the experiment to make MessageBoxModal
// close behave like Esc-menu close.
//
// Returns true on dispatch, false if CGuiInGame can't be resolved.
// SEH-wrapped; faults log a line and return false instead of crashing.
bool CallHideSWInGameGui(int param_1);

// "Is the foreground UI capturing input" predicate — shared between every
// in-world hotkey gate (Enter via interact_hotkey, Tab leader-announce via
// party_leader_announce, etc.). Returns true if any of:
//   - a CSWGuiDialog* panel is alive anywhere in panels[]
//   - the foreground panel is modal_stack[top]
//   - the foreground panel is one of: Container, Store, Examine, Dialog*,
//     TutorialBox, MessageBoxModal, StatusSummary, AreaTransition,
//     InGameMenu (strip stays fg while any sub-screen is drilled)
//
// Uses a blacklist rather than whitelist: panels[] keeps stale entries
// (closed Fade overlays, dismissed Options menus) at the top of the
// stack for seconds, so a whitelist of "in-world overlay kinds"
// underblocks because those stale entries get reported as fg by
// GetForegroundPanel.
enum class UiBlockReason {
    NotBlocked,
    DialogInStack,
    ForegroundModal,
    ForegroundBlockingKind,
};

struct UiBlockState {
    UiBlockReason reason   = UiBlockReason::NotBlocked;
    void*         fgPanel  = nullptr;
    PanelKind     fgKind   = PanelKind::Unknown;
    int           modalStackTop = -1;  // index in modal_stack when reason == ForegroundModal
};

// Optional `outState` is filled with diagnostic detail for the caller's
// log line (fg panel + kind, modal index, why it's blocked). Pass nullptr
// when only the bool answer is needed.
bool IsForegroundUiBlocking(UiBlockState* outState = nullptr);

}  // namespace acc::engine
