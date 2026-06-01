// In-game panel identity registry. Classifies CSWGuiPanel pointers
// against named CGuiInGame slots so menu code can branch on semantic
// kind instead of layout.
//
// Chain: *(CAppManager**)0x7A39FC → CAppManager → +0x4 CClientExoApp →
//   +0x4 Internal → +0x40 CGuiInGame*. Re-resolved on every call so
// module transitions don't strand stale pointers.

#pragma once

namespace acc::engine {

// Mirrors CGuiInGame's named slots. Add a value here AND a row in
// kPanelKindOffsets[] (engine_panels.cpp) to register a new kind.
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
    // "MessageBoxModal" not "MessageBox" — Win32 winuser.h defines
    // MessageBox as a macro, which would rename the enum value.
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

    // Heap-allocated panels with no CGuiInGame slot — identified
    // structurally (vtable / .gui-id signature) after slot-table miss.
    SaveLoad,            // saveload.gui
    InGameLevelUp,       // CSWGuiLevelUpPanel vtable 0x00759568.
                         // Step buttons gate on `is_active != 0` and
                         // silently drop disabled clicks — the extract
                         // path emits a disabled-state suffix.

    // Workbench (upgrade.gui / upgradeitems.gui / upgradesel.gui).
    //   Select   — category picker, 11 controls. Generic chain nav
    //              handles it; identified only for log tagging.
    //   Items    — per-category item picker, 5 controls. LB_ITEMS at 0,
    //              LB_DESCRIPTION at 2. Empty-state speech goes here.
    //   Upgrade  — slot detail, 29 controls including 7 slot buttons at
    //              IDs 12..18. ID-11 type disambiguates from SaveLoad
    //              (Button = SaveLoad's BTN_DELETE, Label = workbench).
    WorkbenchSelect,
    WorkbenchItems,
    WorkbenchUpgrade,

    // Force-power picker (pwrlvlup.gui). Same class hosts level-up and
    // chargen flows. Controls: labels 0/1/3/4/5, powers_listbox 6,
    // description_listbox 7, power_label 8, RECOMMENDED/SELECT/ACCEPT/
    // BACK at 9/10/11/12.
    PowersLevelUp,

    // Title-screen CSWGuiOptions (vtable 0x00758838). Not under
    // CGuiInGame — title is pre-game so CGuiInGame isn't resolvable
    // yet. Controls: 0 body listbox, 1..5 tab buttons, 6 title label,
    // 8 Schliess. button. Identified by vtable equality.
    MainMenuOptions,

    // Title-screen CSWGuiMainMenu (vtable 0x00752f70). Pre-game, no
    // CGuiInGame slot. Identified by vtable equality. The generic
    // title-walk lands on the optional "New downloadable content is
    // available…" notice label (Steam-side, unactionable on this
    // build); AnnouncePanelTitle substitutes Id::PanelTitleMainMenu
    // for this kind so the user hears "Main menu" / "Hauptmenü"
    // instead of the DLC notice.
    MainMenu,

    // Pre-game Pazaak side-deck builder (CSWGuiPazaakStart, vtable
    // 0x007532e8). Heap-allocated modal with no CGuiInGame slot; identified
    // by vtable. Card widgets are labelled + the chain filtered by
    // menus_pazaakdeck.
    PazaakStart,

    // Pazaak wager popup (CSWGuiWagerPopup, vtable 0x007534c8). Heap-allocated
    // modal pushed over the side-deck builder; identified by vtable. Its two
    // BTN_LESS / BTN_MORE CSWGuiSpeedButtons carry no text — labelled by
    // menus_extract; the live wager amount is announced by pazaak.cpp.
    PazaakWager,
};

const char* PanelKindName(PanelKind k);

// AppManager → ClientExoApp → Internal → CGuiInGame. Null on any null link.
void* ResolveGuiInGame();

// PanelKind::Unknown on no match. First (panel,kind) sighting is logged;
// subsequent calls hit a cache.
PanelKind IdentifyPanel(void* panel);

bool IsPanelKindInGameMenu(void* panel);

// Engine-pushed standalone modal popups whose dismissal requires our Esc-routes-
// to-close handler (the engine's own Esc handling on these is to open the
// quit-confirm sibling, leaving the user stacked deeper instead of backing out).
// Distinct from IsModalTextPanel (which identifies popups whose body text the
// engine wraps in a single-row listbox so the chain can promote it to a
// text-only entry); each is asked a different question, though MessageBoxModal /
// TutorialBox / AreaTransition appear in both.
bool IsModalPopupPanel(PanelKind k);

// During reply turns the engine swaps fg to a Fade overlay while the
// real dialog panel stays in panels[]. Scan, don't trust foreground.
bool HasActiveDialogPanel();

// Same reason as HasActiveDialogPanel — drilled sub-screens hide under
// stale Fade overlays. Sub-screens are popped on close so no stale-block.
bool HasActiveSubScreen();

// InGameMap hides under the InGameMenu strip when drilled.
// outPanel optional — populated for callers that reach into the panel.
bool HasActiveMapPanel(void** outPanel = nullptr);

// True iff a CSWGuiLevelUpPanel (InGameLevelUp) sits anywhere in
// panels[]. Used to debounce Shift+L: ShowLevelUpGUI doesn't dedupe and
// every dispatch allocates a fresh panel, so key-repeat stacks duplicate
// modals the user can't unwind (see patch-20260530-112606.log).
bool HasActiveLevelUpPanel();

// Heap-allocated Options sub-screen (Spieleinstellungen / Grafik /
// Sound / Auto-Pause / Feedback / Tastenbelegung / Mauseinstellungen).
// No CGuiInGame slot → IdentifyPanel returns Unknown; positive
// classification needs the panels[]-stack context.
//
// Used by the Esc gate to route through QueueActivate(Schliess.) —
// the engine's vanilla Esc path for these triggers a stack-cookie smash
// (0xc0000409) matching the LevelUp Annehmen signature.
bool IsInGameOptionsSubScreen(void* panel);

// CGuiInGame::PrevSWInGameGui @0x0062cdf0 — engine's "back to strip"
// primitive. Removes the sub-screen from panels[] cleanly across all
// kinds; per-screen Schliess onClick silent-fails on InGameOptions
// (reorders panels[] without popping).
bool CallPrevSWInGameGui();

// CGuiInGame::HideSWInGameGui @0x0062cba0. The engine's "close current
// sub-screen" primitive, used internally by Esc on the save/load menu.
// That path produces a full unpause + audio resync; the MessageBoxModal
// close path (Alt+F4 quit-confirm) skips it and leaves the world
// half-paused. SEH-wrapped.
bool CallHideSWInGameGui(int param_1);

// True iff foreground UI is capturing input. Blacklist (not whitelist):
// panels[] keeps stale entries (closed Fade overlays, dismissed Options
// menus) at the top for seconds, so a whitelist of in-world overlays
// underblocks.
//
// Triggers: a CSWGuiDialog* in panels[]; foreground = modal_stack[top];
// foreground kind ∈ {Container, Store, Examine, Dialog*, TutorialBox,
// MessageBoxModal, StatusSummary, AreaTransition, InGameMenu (the strip
// stays fg while any sub-screen is drilled)}.
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

// outState (optional) carries diagnostic detail for the log line.
bool IsForegroundUiBlocking(UiBlockState* outState = nullptr);

}  // namespace acc::engine
