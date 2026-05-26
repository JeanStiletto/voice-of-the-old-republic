// KOTOR Accessibility — chain-navigation state and helpers.
//
// Step 5 of the menus.cpp refactor. The chain is the menu accessibility
// mod's central abstraction: a flat, visually-sorted list of focusable
// controls on the current panel that arrow keys walk top-to-bottom. Two
// previous refactor steps (listbox panels, deferred ops) deferred this
// lift because chain code couples broadly with menus.cpp's input,
// monitor, and announce paths.
//
// What lives here:
//
//   * Chain entry struct + the chain array + cursor state.
//   * Tab-cluster detection state for Options-style panels (tabbed
//     listbox at controls[0], buttons after) plus the click-offset
//     compensations RebindChain computes for cursor-warp hit-test
//     correction (tab-y, equip-slot-y, class-icon-x).
//   * The geometry helpers that build and walk the chain: DetectTabsCluster,
//     ResetTabbedState, ValidateTabbedPanel, IsTabButton, AppendChainEntry/
//     TextOnly, RebindChain, FindAdjacentArrow, FindCloseButton/Cancel,
//     FindChainEntry, ReadPanelActiveControl.
//
// What stays in menus.cpp:
//
//   * AnnounceControl / AnnouncePanelTitle — speech utilities not tied to
//     chain state (they read panel/control, write prism + monitor state).
//   * IsModalPopupPanel — used by Esc handler in OnHandleInputEvent.
//   * g_focusMonitor*, g_currentPanel, g_lastTitledPanel — monitor / focus
//     state read by chain code via extern decl in menus_internal.h, but
//     written from OnSetActiveControl which stays.
//
// Cross-TU seam: state has `extern` decls here; menus.cpp adds `using`
// declarations to keep call-site code unqualified. Same pattern as Steps
// 2B (extract) and 4 (listbox) — the chain TU's namespace is
// `acc::menus::chain::` rather than `detail::` to signal cohesion (these
// names belong together, not just "shared between two TUs").

#pragma once

namespace acc::menus::chain {

// One chain entry = one navigable target on the current panel.
struct ChainEntry {
    void* control;
    int   cx;
    int   cy;
    // True for non-activatable body text — currently the body listbox of a
    // modal popup (MessageBoxModal / TutorialBox / AreaTransition). Arrow
    // keys land on it so the user can re-hear the message; Enter
    // re-announces instead of firing vtable[15] (the listbox has no
    // activate handler).
    bool  textOnly;
    // Virtual-entry kind. 0 = real engine control (default). Non-zero
    // values identify entries whose `control` field is a sentinel
    // pointer owned by a mod-side module — currently only mod-settings
    // root (kVirtualMod_SettingsRoot). Real-control behaviour is the
    // common case so the field is zero-init in every existing call site.
    int   virtualKind;
};

// Virtual-entry kind tags. 0 is reserved for "real engine control".
constexpr int kVirtualMod_SettingsRoot = 1;

constexpr int kMaxChainEntries = 64;

// The chain itself + the cursor that points at the focused entry. Reads
// dominate; the only mutator outside RebindChain is the chain-step in
// OnHandleInputEvent (advances g_chainIndex on Up/Down).
extern ChainEntry g_chain[kMaxChainEntries];
extern void*      g_chainPanel;
extern int        g_chainIndex;
extern int        g_chainCount;

// Tab-cluster state. Populated by DetectTabsCluster on first listbox
// activation in a tabbed panel; ValidateTabbedPanel drops it when the
// engine frees the underlying panel across hard transitions (death →
// main menu, etc.).
extern void* g_tabbedPanel;
extern int   g_tabsStart;       // first tab-button index in panel.controls
extern int   g_tabsCount;       // number of contiguous tab buttons

// Cursor-warp hit-test compensations. RebindChain measures these from the
// chain's cluster spacing; OnHandleInputEvent's chain-step / Enter sites
// add them when computing the click-sim coords. 0 outside the relevant
// panel kinds.
extern int g_tabClickOffsetY;          // tab-cluster row pitch (Options-style)
extern int g_equipSlotClickOffsetY;    // InGameEquip slot row pitch
extern int g_classIconClickOffsetX;    // chargen class-icon column pitch

// (Re)bind the chain to the currently focused panel. Walks panel.controls,
// recurses one level into sub-dialog listboxes, sorts by extent.top, then
// squashes cycle-arrow flankers (empty-text neighbours of value-display
// buttons). Computes the click-offset compensations + anchors g_chainIndex
// on the engine's current activeControl.
void RebindChain(void* panel);

// Same as RebindChain, but after the rebuild restore the chain cursor to
// the position it held before the call (clamped to chainCount-1). Used
// when the rebind is triggered by an in-place listbox repopulate
// (e.g. Store sell/buy removes one row without changing the user's
// logical position — the row that was at index N is gone; the next item
// shifted up to take its slot, so the user's cursor should stay at N).
//
// Defaults to clamping high; if the user was on the last row and it got
// removed, lands on the new last row (one above). If the saved index
// no longer fits (chain shrank to 0), falls back to 0.
void RebindChainPreserveIndex(void* panel);

// Drop all chain state without rebuilding. Called from the sub-screen
// status hook on `new_status == 4` (teardown begin): the engine is about
// to free the focused panel's child controls, so any pointer in g_chain
// becomes stale. Setting g_chainCount = 0 makes MonitorFocusedControl
// short-circuit on its first gate; the chain rebuilds on the next
// OnSetActiveControl when a fresh panel takes focus. Without this, the
// monitor dereferences g_chain[g_chainIndex].control on the tick between
// teardown and the next SetActiveControl, and that pointer is now in
// freed memory (crash analysed 2026-05-11, dump swkotor.exe.18312.dmp).
//
// Safe to call multiple times; idempotent when already empty.
void InvalidateChain();

// Reset tabbed-mode state (tab cluster fields). Called when the focused
// panel changes to a different one.
void ResetTabbedState();

// Per-tick guard against a dangling g_tabbedPanel. Cheap pointer-compare
// against the manager's panels[]; clears the state if not found.
void ValidateTabbedPanel();

// Post-FireActivate guard against a dangling g_chainPanel. Some commit-style
// buttons (InGameLevelUp "Annehmen", quit-confirm "OK") synchronously
// destroy their parent panel inside vtable[15] dispatch. Cached g_chain
// entries then point at freed controls; the next tick's
// MonitorFocusedControl calls extract::FromControl on one of them, walks a
// freed vtable, and trips /GS via an indirect call into garbage (crash
// analysed 2026-05-21, dump swkotor.exe(1).31052.dmp). InvalidateChain
// short-circuits the monitor until the next OnSetActiveControl rebinds.
// Same panels[]-walk shape as ValidateTabbedPanel.
void ValidateChainPanel();


// Detect Options-style layout: CSWGuiListBox at controls[0] with
// controls.size > 0, followed by a contiguous cluster of buttons. Fills
// outStart/outCount with the cluster bounds on success.
bool DetectTabsCluster(void* panel, int& outStart, int& outCount);

// True if `control` is one of the current tabbed panel's tab buttons.
// Used by chain-step / Enter sites to gate the cursor-y offset.
bool IsTabButton(void* control);

// Find the empty-text navigable neighbour at the same y-row in
// panel.controls — the cycle-arrow flanker around a value-display button.
// `toRight` selects the direction. Returns nullptr if no neighbour found.
void* FindAdjacentArrow(void* panel, void* focused, bool toRight);

// Heuristic finders for the back-out / cancel buttons used by the Esc
// handler. FindCloseButton matches "Schliess" / "Close" / "OK" / "Weiter"
// / "Continue" prefixes; FindCancelButton matches "Abbrechen" / "Cancel"
// / "Nein" / "No". Probe order matters at the call site (cancel-first to
// route Esc on confirm dialogs to the safe option).
void* FindCloseButton(void* panel);
void* FindCancelButton(void* panel);

// Linear scan of g_chain for `control`. Returns the chain index or -1.
int FindChainEntry(void* control);

// Read panel.activeControl. Defined here because RebindChain anchors on
// it; menus.cpp's OnSetActiveControl uses its own focus-tracking instead.
void* ReadPanelActiveControl(void* panel);

}  // namespace acc::menus::chain
