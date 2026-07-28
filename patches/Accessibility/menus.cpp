// menu-side hook handlers (chain navigation, focus
// events, input dispatch, per-tick monitors).
//
// Layering:
//   log.{h,cpp}             file/debug logging primitives
//   prism.{h,cpp}            screen-reader bridge (LoadLibrary'd lazily)
//   core_dllmain.cpp        DllMain + OnRulesInit + EnsurePrismInitialized
//   engine_input.{h,cpp}    InputIndices name table + manager translate
//   engine_offsets.h        engine struct/vtable offset constants + engine structs
//   engine_reads.{h,cpp}    SEH-guarded readers (CallDowncast, ReadGuiString, ...)
//   engine_panels.{h,cpp}   PanelKind enum + CGuiInGame slot classification
//   engine_manager.{h,cpp}  CSWGuiManager surface + cursor / click-sim PFNs
//   this file               the menu-accessibility hook handlers
//
// Phase 0 of the long-term nav plan extracted the foundation (core_dllmain
// + engine_*) out of the original monolithic Accessibility.cpp; the
// remaining menu-accessibility code is what lives here under the new name.
// Per plan, the menu-side logic is NOT decomposed further in Phase 0
// (incremental refactor discipline) — see docs/navsystem-longterm-plan.md.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "prism.h"
#include "menus.h"           // public surface — Step 1 mod-wide tick split
#include "menus_charsheet.h" // Step 2A — character-sheet opener lifted out
#include "menus_chargen_attr.h" // Chargen "Attribute" panel label + selected_ability sync
#include "menus_chargen_skills.h" // Chargen "Fähigkeiten" panel — same shape as Attribute
#include "menus_chargen_feats.h"  // Chargen "Talente" panel — 2D feat-tree chart
#include "menus_powers_levelup.h" // Level-up "Kr�fte" — feat-tree-shaped power picker
#include "menus_extract.h"   // Step 2B — text extraction lifted out
#include "menus_internal.h"  // Step 2B — shared seam with menus_extract
#include "menus_focus.h"     // first-sight / focus-capture (Phase-1 split)
#include "menus_pending.h"   // Step 3 — deferred-op queue lifted out
#include "menus_abilities.h"  // dedicated Fähigkeiten-screen input handler
#include "menus_listbox.h"   // Step 4 — listbox-driven panel dispatcher
#include "menus_editbox.h"   // Editbox (chargen Name) dispatcher + monitor
#include "menus_chain.h"     // Step 5 — chain navigation lifted out
#include "menus_modsettings.h" // Virtual mod-settings submenu (Optionen panels)
#include "menus_monitors.h"  // Post-Step-5 — general per-tick monitors
#include "tutorial_hints.h"  // mapped tutorial-popup gate (mouse-announce suppression)
#include "tutorial_popup.h"  // synthetic Trask-line popup (suppress its listbox row)
#include "menus_store.h"     // Store / trading panel — price+stock suffix + mode announce
#include "menus_pazaakdeck.h" // Pazaak side-deck builder — 3-row navigator
#include "menus_galaxymap.h"  // Galaxy / star-map travel screen — planet cycle
#include "menus_keymap.h"     // dedicated Tastaturbelegung two-level handler
#include "minigame_pazaak.h"           // Pazaak board game — IsBoardForeground
#include "menus_journal.h"   // Journal (Aufträge) — Enter on quest row → description
#include "help.h"             // Help list overlay — suppress engine keys while open
#include "engine_area.h"     // IsLoadingSaveGame — gate the save-load GUI burst
#include "engine_input.h"
#include "engine_keymap.h"   // VksForCode — modifier-shadow consume (manager side)
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_player.h"   // Phase 1 lay-off 4 (test fixture only)
#include "engine_reads.h"
#include "hotkeys.h"
#include "audio_bus.h"       // Phase 1 lay-off 4 (test fixture only)
#include "announce_degrees.h" // Phase 4 sub-feature D
#include "probe_mouselook.h"  // Phase 4 lay-off 2 — view-mode probe
#include "view_mode.h"        // Phase 4 lay-off 3 — view-mode skeleton
#include "cycle_input.h"     // Phase 2 lay-off 3
#include "guidance_autowalk.h"  // Phase 2 lay-off 5 (progress watchdog)
#include "camera_announce.h"    // Phase 2 ad-hoc — camera-direction on A/D
#include "input_pipeline.h"  // Cross-stream seq counter for input diag
#include "diag_chargen_feats.h"   // One-shot CSWGuiFeatsCharGen structure dump
#include "interact_hotkey.h"    // Phase 2 lay-off 9b
#include "passive_narrate.h"    // Phase 2 lay-off 9a
#include "peek_description.h"   // Shift+arrow description peek
#include "spatial_change_detector.h"  // Phase 3 lay-off 3 — Pillar 1 Trigger 1
#include "audio_footstep_suppress.h"  // Phase 3 lay-off 5 — stuck-detection
#include "strings.h"            // Container loot panel announces
#include "update_checker.h"     // Deferred background version check + bringup mark
#include "focus_guard.h"         // WindowProc subclass for WM_ACTIVATE logging
#include "bringup_announce.h"   // Loading-phase nag when user presses arrows too early
#include "transitions.h"        // Phase 2 lay-off 7 — Pillar 2 area+room announce
#include "engine_rebase.h"

// Engine readers + offset constants moved to engine_reads.{h,cpp} +
// engine_offsets.h in Phase 0 lay-off 2. Pull the readers' names into the
// menu-side TU so callsites stay as they were.
using namespace acc::engine;

// Step 2B seam: the four detail-namespace helpers + GetControlCenter live
// in this TU (chain-side has many more callers than extract-side). The
// using-declarations bring their unqualified names back into scope so
// existing call sites don't need to be touched.
using acc::menus::detail::IsChainNavigable;
using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::ClassLabelCacheLookup;
using acc::menus::detail::ClassLabelCacheStore;
using acc::menus::detail::GetControlCenter;

// Step 4 seam: the listbox-driven panel handlers live in menus_listbox.cpp,
// but their helpers (FindControlById, FindListBoxChild, IsSaveLoadPanel,
// ReadSaveLoadEntryString, DriveListBoxSelection, QueueButtonByIdActivate)
// stay defined here because they're called from menus.cpp's monitors and
// chain code too. Same using-declaration pattern as Step 2B.
using acc::menus::detail::FindControlById;
using acc::menus::detail::FindListBoxChild;
using acc::menus::detail::IsSaveLoadPanel;
using acc::menus::detail::ReadSaveLoadEntryString;
using acc::menus::detail::DriveListBoxSelection;
using acc::menus::detail::ListBoxNavResult;
using acc::menus::detail::QueueButtonByIdActivate;

// Step 5 seam: chain state + helpers live in menus_chain.cpp. Bring all
// the names into unqualified scope so the dense reads in OnHandleInputEvent
// / OnSetActiveControl / monitors stay as they were. Writes to the
// externs (g_chainIndex on chain-step advance) work the same way through
// using-declarations.
using acc::menus::chain::ChainEntry;
using acc::menus::chain::kMaxChainEntries;
using acc::menus::chain::kVirtualMod_SettingsRoot;
using acc::menus::chain::g_chain;
using acc::menus::chain::g_chainPanel;
using acc::menus::chain::g_chainIndex;
using acc::menus::chain::g_chainCount;
using acc::menus::chain::g_tabbedPanel;
using acc::menus::chain::g_tabsStart;
using acc::menus::chain::g_tabsCount;
using acc::menus::chain::g_equipSlotClickOffsetY;
using acc::menus::chain::g_classIconClickOffsetX;
using acc::menus::chain::RebindChain;
using acc::menus::chain::ResetTabbedState;
using acc::menus::chain::ValidateTabbedPanel;
using acc::menus::chain::ValidateChainPanel;
using acc::menus::chain::DetectTabsCluster;
using acc::menus::chain::IsTabButton;
using acc::menus::chain::FindAdjacentArrow;
using acc::menus::chain::FindCloseButton;
using acc::menus::chain::FindCancelButton;
using acc::menus::chain::FindChainEntry;
using acc::menus::chain::ReadPanelActiveControl;
using acc::menus::chain::WalkChildren;

// Post-Step-5 cleanup: general-monitor TU and listbox-monitor extension.
// AnnounceControl (writes monitor state) lives in menus_monitors; chain
// handlers in OnHandleInputEvent below call it through this using-decl.
using acc::menus::monitors::AnnounceControl;

// Phase-1 structure split: the first-sight / focus-capture step functions
// moved to menus_focus.cpp. OnSetActiveControl below is their only caller
// and reads exactly as it did before via these using-declarations.
using acc::menus::focus::PrefillClassIconCacheOnTransition;
using acc::menus::focus::UpdateFocusedPanelState;
using acc::menus::focus::WalkAndCaptureOnFirstSight;
using acc::menus::focus::SpeakPanelTitleOnFirstSight;
using acc::menus::focus::AnnounceNewFocusedControl;

// Forward decl from core_dllmain.cpp. The first hook to fire calls this so
// Prism is loaded the moment any focus / input event reaches us.
void EnsurePrismInitialized();

// Defined in menus_dispatch.cpp (Phase-1 split). PollHomeEndKeys below
// re-enters the GUI input pipeline through it to deliver the Home/End
// edges the engine keymap drops.
extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2);

// Forward declarations + the shared kEquipBtn* / kEquipLb* constants moved
// to menus_internal.h in Step 2B. g_currentPanel is declared there as
// extern; defined later in this TU. The four detail-namespace helpers
// (IsChainNavigable, IsClassSelectionIcon, ClassLabelCache*) and
// GetControlCenter are defined further down with the chain machinery and
// brought back into unqualified scope by the using-declarations above.



// Multi-line "blob" listbox readout. The Options-Gameplay settings list is
// the canonical case: CSWGuiListBox.controls.size == 1, the single child is a
// CSWGuiLabel whose CExoString contains all visible setting names joined by
// Speak `text` only if it differs from what we last spoke on this channel.
// Dedup is the only filter: in the first session we used interrupt=true and
// NVDA went fully silent in chargen (every utterance got cut off mid-word
// because focus events fire ~10/sec while panels initialize). Switching to
// interrupt=false (queued) lets NVDA finish each line at its own pace; the
// user can still skip forward with NVDA's own ctrl-key shortcut.
//
// Channels keep dedup state independent so a listbox row update doesn't
// silence the parent panel's announcement and vice-versa:
//   0 = panel SetActiveControl  (slot drain + voluntary AnnounceControl)
//   1 = listbox row SetActiveControl
//
// Exposed publicly via menus.h so the focus-monitor's AnnounceControl can
// MarkSpoken(0, text) after voluntary speech, which lets the slot drain
// suppress the engine's echo of the same nav.
namespace acc::menus {

namespace {
char s_lastSpoken[2][256] = {{0}, {0}};
}

void MarkSpoken(int channel, const char* text) {
    if (channel < 0 || channel >= 2 || !text) return;
    strncpy_s(s_lastSpoken[channel], text, _TRUNCATE);
}

void SpeakIfChanged(int channel, const char* text) {
    if (channel < 0 || channel >= 2 || !text) return;
    if (strncmp(s_lastSpoken[channel], text,
                sizeof(s_lastSpoken[channel])) == 0) return;
    strncpy_s(s_lastSpoken[channel], text, _TRUNCATE);
    prism::Speak(text, /*interrupt=*/false);
}

}  // namespace acc::menus

using acc::menus::SpeakIfChanged;
using acc::menus::MarkSpoken;

// ============================================================================
// Unified-cursor menu navigation (Phase 1+2 — see docs/menu-nav-design.md).
// ============================================================================

// CSWGuiManager / cursor / click-sim surfaces moved to engine_manager.{h,cpp}
// in Phase 0 lay-off 4: kAddrGuiManagerPtr, kMgr*Offset, MoveMouseToPosition
// + click-sim PFN typedefs and addresses, FindOwningPanel, GetForegroundPanel,
// LogManagerStack.

// CSWGuiPanel::SetActiveControl @ 0x40a630 — committing selection to a panel.
// MoveMouseToPosition only updates hover state; panel.activeControl lags
// behind the cursor unless we explicitly set it. Enter / F1 activates
// panel.activeControl, so without this call the engine activates the
// previously-clicked button instead of the cursor target.
const uintptr_t kAddrPanelSetActiveControl = acc::addr::R(0x0040a630);
typedef void (__thiscall* PFN_PanelSetActiveControl)(void* panel, void* control);

// The "fire activate" primitive (vtable[15].HandleInputEvent(0x27, 1)) used
// to live here as `FireActivate(control)`. It moved with the deferred-op
// queue in Step 3 of the refactor — the only caller was the Activate-op
// drain, which now lives inline in menus_pending.cpp.

// Logical input codes (kInputNav*, kInputEnter1/2, kInputEsc1/2,
// kInputActivate) are defined in engine_input.h. They're the codes
// CSWGuiManager::HandleInputEvent receives pre-translation; see
// ManagerTranslateCode for what each maps to post-translation.
//
// Up/Down (0xb6/0xb7) and Left/Right (0xb8/0xb9) are the engine's nav-prev/
// nav-next and horizontal-axis equivalents — consuming Up/Down prevents the
// engine's broken `.gui` focus-cycle from running. Left/Right are consumed
// selectively (slider passes through, otherwise dispatched to a cycle-arrow
// neighbour). Enter (0xb5/0xbb → KEYBOARD_F1) and Esc (0xb4/0xdf → KEYBOARD_F2)
// route to our chain-target activation and Schliess-button fallback paths.

// Chain state (g_chain, g_chainPanel/Index/Count, ChainEntry struct,
// kMaxChainEntries) moved to menus_chain.cpp in Step 5; brought back into
// unqualified scope at the top via using-declarations.
//
// g_currentPanel stays here — it's set by OnSetActiveControl (focus
// tracking) and read across chain code, monitors, and extract::FromControl.
// Default-linkage so menus_extract.cpp + menus_chain.cpp see it via the
// extern decl in menus_internal.h.
void* g_currentPanel = nullptr;

// Sub-screen drill state. The InGameMenu icon strip is kept in foreground by
// the engine: each icon's onClick (OnInvButtonPressed @0x624d10 etc.) jumps
// into CGuiInGame::SwitchToSWInGameGui @0x62cf10, which calls AddPanel for the
// new sub-screen and then SendPanelToBack on it — the strip stays on top
// (verified via SARIF xref trace). Without intervention our chain therefore
// keeps targeting the strip's 8 icons and the user can never reach the
// sub-screen's content (item rows, quest rows, settings buttons).
//
// Drill model: Enter on a strip icon arms this flag. The chain-target router
// in OnHandleInputEvent then prefers FindActiveSubScreenPanel() over the
// engine's foreground when fg is the strip — so arrows step through the
// sub-screen instead. Esc clears the flag (returns to strip nav). The flag
// also self-clears when the sub-screen leaves panels[].
//
// Override is gated on fg-is-the-strip: while a tutorial modal or an
// Options sub-tab is on top, fg is something else and we route to that
// directly (no double-override). Once the modal/sub-tab closes and fg
// returns to the strip, the override re-engages.
// Non-static: the Esc / drill-routing gates in menus_dispatch.cpp read and
// clear it. Declared in menus_internal.h.
bool g_drilledIntoSubScreen = false;

// Equipment picker zone arming state moved to menus_listbox.cpp in Step 4
// of the refactor (state ownership follows the spec entry that primarily
// uses it). Two outside touch sites in this file — the slot-Enter arming
// site below and MonitorEquipPickerSelection's "panel gone, disarm"
// cleanup — go through the acc::menus::listbox accessors.

// Sub-screen tracking (InGameSubScreenSpec, FindActiveSubScreenPanel,
// AnnounceNewSubScreens) moved to menus_monitors.cpp. The drill router
// in OnHandleInputEvent calls acc::menus::monitors::FindActiveSubScreenPanel
// and acc::menus::monitors::IsInGameSubScreenKind through the public
// surface; no using-decl needed since both call sites are explicit and few.

// The deferred-op queue (cursor-warp / click-at-point / activate / equip-
// slot / equip-commit / slider-input) lives in menus_pending.{h,cpp} as of
// Step 3. Input handlers below call `pending::Queue*`; the queue drains
// once per tick from `TickPendingOps`.

// Pending-announce slot for the panel-focus path. OnSetActiveControl writes
// the slot on every event; DrainPendingAnnounce reads + clears it once per
// tick from TickMonitors. Multiple intra-tick events overwrite the slot —
// natural last-write-wins coalesce.
//
// Two reasons this beats the old "decrement a budget on every event" knob:
//
//   1. Triple-burst panel-open events (NULL → first child → engine's actual
//      default focus) used to produce two utterances ("OK, Abbrechen" on
//      MessageBox open). With the slot, the first two writes get overwritten
//      by the third before the next tick reads.
//   2. Voluntary-nav echoes (chain step + cursor warp → engine echoes a
//      SetActive on the same control) used to need a separate suppress
//      counter. Now AnnounceControl calls MarkSpoken(0, text) after speaking,
//      which primes the channel-0 dedup; the slot drain sees the same text
//      and stays silent.
// Not in an anonymous namespace: the slot is written by
// AnnounceNewFocusedControl (menus_focus.cpp) and drained here, so it has
// to be visible across the two TUs. Declared in menus_internal.h; this TU
// owns the storage because DrainPendingAnnounce / ClearPendingAnnounce
// are the lifecycle.
namespace acc::menus {
void* s_pendingAnnouncePanel   = nullptr;
void* s_pendingAnnounceControl = nullptr;

// Set while PollHomeEndKeys is synthesising a call to OnHandleInputEvent.
// The engine drops KEYBOARD_HOME(32) / KEYBOARD_END(33) before our manager
// hook (no [Keymapping] action targets them), so we Win32-poll them and
// re-enter the input pipeline ourselves. The synthesised call has no
// matching engine-sent release, so the press-release pair tracker inside
// OnHandleInputEvent skips its update when this flag is set — otherwise
// a synthesised non-consumed press would clobber the tracker for a
// previous real consumed press (Enter activations, etc.).
//
// Set here (PollHomeEndKeys), read in menus_dispatch.cpp
// (OnHandleInputEvent) — declared in menus_internal.h for that reason.
bool s_synthesizedNav = false;
}


// Per-frame focus monitor + its last-seen state moved to
// menus_monitors.cpp. AnnounceControl (which writes that state to keep
// the monitor in sync with voluntary speak events) moved with it; chain
// handlers below call it via the using-declaration at the top of this
// file.

// Tabbed-panel state (g_tabbedPanel, g_tabsStart, g_tabsCount) and the
// three click-offset compensations (g_tabClickOffsetY,
// g_equipSlotClickOffsetY, g_classIconClickOffsetX) moved to menus_chain.cpp
// in Step 5. Brought back into unqualified scope via the using-declarations
// at the top of this file. See menus_chain.h for the rationale.


// AnnounceControl moved to menus_monitors.cpp (writes the focus monitor's
// last-seen state to keep voluntary speech in sync). Brought back into
// unqualified scope via the using-declaration at the top of this file.


// DetectTabsCluster, ResetTabbedState, and ValidateTabbedPanel moved to
// menus_chain.cpp in Step 5. Brought back into unqualified scope via the
// using-declarations at the top of this file.

// Container loot panel control IDs from container.gui (extracted via
// xoreos-tools from data/gui.bif). Stable per panel kind across patch versions.
// Used by the Container input handler in OnHandleInputEvent and the per-row
// monitor MonitorContainerSelection further down. (Equipment IDs live near
// the top of the file because ExtractAnnounceableText needs them; container
// IDs aren't referenced until the input handler ~800 lines below so they
// stay co-located here with the Container helpers.)
constexpr int kContainerLbItemsId   = 2;
constexpr int kContainerBtnOkId     = 3;
constexpr int kContainerBtnGiveId   = 4;
constexpr int kContainerBtnCancelId = 5;

// Forward declaration — body lives next to MonitorDialogReplies (which is
// the long-standing first-and-only caller). Container input handler in
// OnHandleInputEvent now also uses it for arrow-key selection_index drive.
//
// Step 4: now in acc::menus::detail (cross-TU seam — menus_listbox.cpp's
// Container spec entry calls it via FindListBoxChild forwarded by
// menus_internal.h). Definition is further down in this TU.


// FindCloseButton / FindCancelButton moved to menus_chain.cpp in Step 5.
// They're heuristic finders for the back-out / cancel buttons used by the
// Esc handler in OnHandleInputEvent. Brought back into unqualified scope
// via the using-declarations at the top of this file.





// FindAdjacentArrow, IsTabButton, AppendChainEntry, and IsModalTextPanel
// moved to menus_chain.cpp in Step 5. Brought back into unqualified scope
// via the using-declarations at the top of this file. IsModalPopupPanel
// (next function below) stays here because its only caller is the Esc
// handler in OnHandleInputEvent.

// True for engine-pushed modal popup panels that the user dismisses via a
// close button (Schliess / OK / Weiter / Continue). Used by the Esc handler
// to know when to fall back to FindCloseButton on standalone modals — the
// older "tabbed sub-dialog" gate only covers Esc inside Options sub-tabs,
// missing the post-action info popups (StatusSummary after a skill check,
// the engine's quit-confirm MessageBox, AreaTransition prompts, …) which
// the engine never auto-dismisses on Esc.
//
// Implementation in engine_panels.cpp; pulled into unqualified scope here
// because the Esc handler calls it inline below.
using acc::engine::IsModalPopupPanel;

// AppendChainTextOnly + RebindChain moved to menus_chain.cpp in Step 5.
// RebindChain is the heart of chain navigation: walks panel.controls,
// recurses into sub-dialog listboxes, sorts by visual y, squashes
// cycle-arrow flankers, computes click-offset compensations, anchors the
// cursor on the engine's current activeControl. Brought back into
// unqualified scope via the using-declarations at the top of this file.

// Walk a CExoArrayList<CSWGuiControl*> embedded at parent+offset and log every
// child. Used as a diagnostic when the focused panel/listbox changes — gives us
// the full set of widgets on the screen, not just whatever arrow keys reach.
//
// `label` is a short tag that prefixes every line (e.g. "Panel", "ListBox").
// Iteration is capped at 256 entries to limit damage from a corrupt size field
// (defensive: the SARIF datatypes are authoritative but a struct-layout
// regression on a future engine version would otherwise spin forever).
// WalkChildren moved to menus_chain.cpp (called from chain dispatch
// HandleNavStep's empty-chain probe + the 3 menus.cpp sites here).

// CSWGuiPanel::SetActiveControl — hooked mid-function at 0x0040a638.
// At hook entry: EDI = this (the panel), ESI = param_1 (the new active
// control, possibly null when the panel is deactivating selection).
//
// This is the canonical focus-change signal: fires once per actual change,
// covers arrow-key nav + mouse + programmatic. Speaks the new control's
// tooltip text or, as a placeholder, "control <id>" while we work out how
// to extract subclass-specific labels.
//
// Logging policy:
//   * Resolved events (text extracted) are throttled — they're noisy when
//     the user is just navigating.
//   * Unresolved events (src=none) are ALWAYS logged with the control's
//     vtable pointer, because that's the data we need to identify which
//     subclasses fall through (Slider, Editbox, ListBox row, etc.).
//   * NULL newControl events are also throttled.





extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl) {
    EnsurePrismInitialized();
    static int n = 0;
    ++n;

    // Stay silent while an engine movie owns the foreground. During a
    // cutscene-load (e.g. 03.bik at the Leviathan capture) the engine
    // constructs and activates a storm of GUI panels — in-game HUD,
    // store, options strip, equip screens — firing SetActiveControl on
    // each. Announcing them talks over the movie AND would mark those
    // panels first-seen, so they'd stay silent on the player's first
    // real visit. Skip the whole handler: the engine re-fires
    // SetActiveControl on genuine interaction once the movie ends, so
    // first-sight + focus speech resume cleanly then. Symmetric with
    // transitions::Tick's movie gate (patch-20260601-220641.log line
    // ~2110: panel-label burst one second into the cutscene movie).
    if (acc::bringup_announce::IsMovieWindowForeground()) {
        acclog::Write("Menus.SetActive",
                      "#%d panel=%p — suppressed (movie foreground)", n, panel);
        return;
    }

    // Same storm, no movie: a save-game load (or any module load) makes the
    // engine reconstruct and activate every in-game GUI panel — HUD, store,
    // abilities, options strip, message log — firing SetActiveControl on
    // each. Narrating that burst spams build strings / panel titles /
    // "control N" fallbacks over the loading screen, ahead of the area-name
    // announce the player actually wants. Skip the whole handler while the
    // load is in progress (same rationale + same first-sight-preservation
    // benefit as the movie gate above): the engine re-fires SetActiveControl
    // on genuine interaction once the world is live, so first-sight + focus
    // speech resume cleanly then.
    //
    // Two signals: IsModuleLoadPending covers module/door transitions (set by
    // the SetMoveToModuleString detour); IsLoadingSaveGame is the engine's own
    // load_from_savegame flag, true across any save-game restore — crucially
    // the IN-GAME load, where the old world is still live so the module latch
    // never armed.
    if (acc::transitions::IsModuleLoadPending() ||
        acc::engine::IsLoadingSaveGame()) {
        acclog::Write("Menus.SetActive",
                      "#%d panel=%p — suppressed (save-load / module load)", n,
                      panel);
        return;
    }

    // Bringup handoff (secondary signal — mouse path only). The engine
    // fires one SetActive immediately after panel construction for its
    // auto-focused button ("New Game"); a genuine 2nd SetActive on the
    // MainMenu comes from the engine's own mouse-over/keyboard focus path.
    // This clears the bring-up nag for mouse users, but NOT for keyboard-
    // only players: our chain navigates via synthetic mouse-over, which
    // fires no SetActiveControl. The primary, keyboard-reachable handoff is
    // the first consumed key in the manager hook (see OnHandleInputEvent).
    // Kept as a belt-and-suspenders signal; also still useful as the
    // "menu appeared ready but wasn't responsive" bring-up diagnostic.
    if (IdentifyPanel(panel) == PanelKind::MainMenu) {
        static int mainMenuSetActiveCount = 0;
        if (++mainMenuSetActiveCount == 2) {
            acclog::BringupMark("main_menu_input_pump_live");
            acc::bringup_announce::NotifyInputPumpLive();
        }
    }

    PrefillClassIconCacheOnTransition(panel, newControl);
    UpdateFocusedPanelState(panel);
    WalkAndCaptureOnFirstSight(panel);

    // The InGameMenu strip is architecturally invisible: we never surface
    // it as a navigable menu — hotkeys + Tab/Shift+Tab drill the user
    // directly into the sub-screens it would otherwise route to. Engine
    // still fires SetActiveControl on it (panel-open and panelIdx=7
    // "Nachrichten" once per first open) which previously produced spurious
    // "Ausrüstung" + "Nachrichten" utterances over the actual sub-screen's
    // title/focus speech. Diagnostic walk above stays; only speech-side
    // paths get the gate.
    if (IdentifyPanel(panel) == PanelKind::InGameMenu) {
        if (newControl) {
            int sid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(newControl) + kControlIdOffset);
            acclog::Write("Menus.SetActive",
                          "#%d panel=%p new=%p id=%d (InGameMenu strip — "
                          "speech suppressed)",
                          n, panel, newControl, sid);
        } else {
            acclog::Write("Menus.SetActive",
                          "#%d panel=%p newControl=NULL (InGameMenu strip)",
                          n, panel);
        }
        return;
    }

    SpeakPanelTitleOnFirstSight(panel);

    if (!newControl) {
        acclog::Write("Menus.SetActive", "#%d panel=%p newControl=NULL", n, panel);
        return;
    }

    AnnounceNewFocusedControl(n, panel, newControl);
}

// CSWGuiListBox::SetActiveControl — hooked mid-function at 0x0041c16b.
// Function entry per Lane's SARIF:
//   void __thiscall CSWGuiListBox::SetActiveControl(CSWGuiControl* param_1, int param_2)
//
// Bytes from 0x0041c160 (DumpBytes.java):
//   8b 44 24 08          MOV EAX, [ESP+8]   ; param_2 (int) before push
//   56                   PUSH ESI
//   8b f1                MOV ESI, ECX       ; this → ESI
//   8b 4c 24 08          MOV ECX, [ESP+8]   ; param_1 (post-push, was [ESP+4])
//   50 51 8d 8e 9c 02 00 00     ← hook here, all three args in registers
//   50                   PUSH EAX           ; param_2
//   51                   PUSH ECX           ; param_1
//   8d 8e 9c 02 00 00    LEA  ECX, [ESI+0x29c]  ; embedded sub-object
//
// Cut covers PUSH EAX (1) + PUSH ECX (1) + complete LEA (6) = 8 bytes. All
// three instructions are position-independent → safe to relocate.
//
// Listbox row navigation does NOT bubble up to CSWGuiPanel::SetActiveControl,
// so without this hook we miss every per-row focus event inside listboxes
// (race / class / portrait pickers in chargen, save-game list, etc.).
extern "C" void __cdecl OnListBoxSetActiveControl(void* listBox, void* newRow,
                                                  int param2) {
    EnsurePrismInitialized();

    static int n = 0;
    ++n;

    // Suppress the in-game-GUI reconstruction burst on a module / save-game
    // load — same two-signal gate as OnSetActiveControl above. The engine
    // fires per-row SetActiveControl on every listbox it rebuilds (abilities
    // "Add Power", party stash, options rows); narrating them spams over the
    // loading screen.
    if (acc::transitions::IsModuleLoadPending() ||
        acc::engine::IsLoadingSaveGame()) {
        acclog::Write("Menus.ListBox",
                      "SetActive #%d list=%p — suppressed (save-load / module "
                      "load)", n, listBox);
        return;
    }

    // First event for a previously-unseen listbox: dump every row control.
    // Tells us whether the listbox holds N separate child widgets (one per
    // visible line) or aggregates everything into a single multi-line label
    // — the central question for the Options Gameplay panel.
    static void* s_lastListBox = nullptr;
    if (listBox && listBox != s_lastListBox) {
        s_lastListBox = listBox;
        WalkChildren("Menus.ListBox", listBox, kListBoxControlsOffset);
    }

    // Always log the listbox's internal cursor + flags state. selection_index
    // distinguishes scroll-mode (-1, set when bit_flags & 0x200) from
    // selection-mode (>=0). controls_size tells us how many real rows exist:
    // for the multi-line-blob settings listbox this is 1 even though the
    // user sees 8 visual lines.
    if (listBox) {
        auto* base = reinterpret_cast<unsigned char*>(listBox);
        short itemsPerPage = *reinterpret_cast<short*>(
            base + kListBoxItemsPerPageOffset);
        short selIdx       = *reinterpret_cast<short*>(
            base + kListBoxSelectionIndexOffset);
        short topVisible   = *reinterpret_cast<short*>(
            base + kListBoxTopVisibleIndexOffset);
        uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(
            base + kListBoxBitFlagsOffset);
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            base + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : -1;
        acclog::Write("Menus.ListBox", "cursor list=%p sel=%d top=%d perPage=%d "
                      "size=%d flags=0x%x",
                      listBox, selIdx, topVisible, itemsPerPage,
                      ctrlsSize, bitFlags);
    }

    if (!newRow) {
        acclog::Write("Menus.ListBox", "SetActive #%d list=%p newRow=NULL p2=%d",
                      n, listBox, param2);
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newRow) + kControlIdOffset);

    char text[256];
    const char* source = acc::menus::extract::FromControl(newRow, text, sizeof(text));

    if (source) {
        acclog::Write("Menus.ListBox", "SetActive #%d list=%p row=%p id=%d "
                      "p2=%d src=%s text=\"%s\"",
                      n, listBox, newRow, id, param2, source, text);

        // Lazy tabbed-mode detection: first listbox event after a panel
        // change probes whether the focused panel has the Options-style
        // listbox-at-[0] + button-cluster layout. Used by the chain-step
        // Y-offset compensation for tab buttons; no longer drives a
        // virtual-line cursor.
        if (g_currentPanel && g_tabbedPanel != g_currentPanel) {
            int tabsStart = -1, tabsCount = 0;
            if (DetectTabsCluster(g_currentPanel, tabsStart, tabsCount)) {
                g_tabbedPanel = g_currentPanel;
                g_tabsStart   = tabsStart;
                g_tabsCount   = tabsCount;
                acclog::Write("Menus.Tabs", "detected panel=%p tabsStart=%d tabsCount=%d",
                              g_currentPanel, tabsStart, tabsCount);
            }
        }

        // Chargen Fähigkeiten description_list_box: silence everything
        // the engine pushes here. The hover-driven population is
        // off-by-one on this panel (the cursor warp's hit-test
        // resolves to skill_labels[i-1] regardless of Y compensation
        // — labels overlap the cursor's row in a way Attribute labels
        // don't), so any text the engine writes corresponds to the
        // wrong row. We speak the focused row's description from the
        // chain-step handler via skill_descriptions[i] direct read.
        if (acc::menus::chargen_skills::IsChargenSkillsDescriptionListbox(
                listBox)) {
            acclog::Write("Menus.ListBox",
                          "chargen-skills description silenced "
                          "(handled by chain-step direct read)");
        } else if (acc::menus::chargen_attr::IsChargenAttributesDescriptionListbox(
                       listBox)) {
            // Same treatment as chargen-skills: the engine's hover-driven
            // population is resolution-dependent (cursor-warp hit-test can
            // resolve to the neighbouring ability), so the text here may be
            // the wrong row. We speak the focused ability's description from
            // the chain-step handler via OnEnterPointsButton direct call.
            acclog::Write("Menus.ListBox",
                          "chargen-attr description silenced "
                          "(handled by chain-step direct read)");
        } else if (strchr(text, '\n')) {
            // Multi-line listbox blob (Options-style: all settings concatenated
            // by '\n' into a single CSWGuiLabel row). Always silenced — bulk
            // enumeration is too noisy. Per-line nav was never wired up after
            // the Options listbox turned out to be decorative; if a future
            // panel ever needs it, that's a new feature.
            int lines = 1;
            for (const char* p = text; *p; ++p) if (*p == '\n') ++lines;
            acclog::Write("Menus.ListBox", "blob silenced; lines=%d", lines);
        } else {
            // In-game sub-screens (InGameOptions, InGameInventory, …)
            // pair their button chain with a single-row description
            // listbox whose text updates each time the user changes
            // focus. The engine fires SetActiveControl on the row on
            // panel-open + every focus change → would announce the
            // description on every nav. Silence here; if a future
            // sub-screen wants the description, expose it via the
            // chain-step extract path (like chargen-skills does).
            auto* ctrls = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(listBox) +
                kListBoxControlsOffset);
            int ctrlsSize = ctrls ? ctrls->size : 0;
            if (ctrlsSize == 1 && g_currentPanel &&
                acc::menus::monitors::IsInGameSubScreenKind(
                    IdentifyPanel(g_currentPanel))) {
                acclog::Write("Menus.ListBox",
                              "sub-screen description listbox silenced "
                              "(panel kind=%s)",
                              PanelKindName(IdentifyPanel(g_currentPanel)));
            } else if (acc::tutorial_hints::IsSuppressedTutorialText(text) ||
                       acc::tutorial_popup::SyntheticActive()) {
                // The tutorial popup's single-row message listbox: the keyboard
                // hint is spoken by the content-fingerprint monitor. Suppress
                // the raw (mouse-worded) row text here so it isn't also read.
                // Matched on text (not panel identity) because this fires before
                // the popup registers in panels[]. See tutorial_hints.cpp.
                // The synthetic Trask popup (SyntheticActive) shows the original
                // mouse strref as its visible text — suppress that row too.
                acclog::Write("Menus.ListBox",
                              "tutorial mouse message suppressed "
                              "(keyboard hint owns the announce)");
            } else {
                SpeakIfChanged(/*channel=*/1, text);
            }
        }
    } else {
        char vtbl[160];
        DumpControlVtable(newRow, vtbl, sizeof(vtbl));
        acclog::Write("Menus.ListBox", "SetActive #%d list=%p row=%p id=%d "
                      "p2=%d src=none %s",
                      n, listBox, newRow, id, param2, vtbl);
        // Suppress placeholder for single-row listboxes (description blobs
        // adjacent to a chain panel — the engine fires SetActiveControl on
        // them as the user navigates the chain, alternating between
        // src=label with text and src=none when the description is
        // momentarily empty). The user isn't navigating these listboxes;
        // "row 0" repeated 5+ times per chain step is just noise.
        //
        // Real multi-row listboxes (save-game list, chargen pickers) keep
        // the fallback so an extraction failure on one row still announces.
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : 0;
        if (ctrlsSize > 1) {
            char placeholder[64];
            snprintf(placeholder, sizeof(placeholder), "row %d", id);
            prism::Speak(placeholder, /*interrupt=*/false);
        }
    }
}


// MonitorFocusedControl + MonitorPanelContents + their helpers (content
// fingerprint, sub-screen tracking, segment-diff speech) moved to
// menus_monitors.cpp post-Step-5. menus.cpp's drill router calls
// acc::menus::monitors::FindActiveSubScreenPanel /
// IsInGameSubScreenKind through the public surface.


// MonitorDialogReplies, MonitorContainerSelection,
// MonitorEquipPickerSelection, PollContainerGiveModeKey, and their per-
// monitor state structs moved out post-Step-5. Dialog-reply monitor lives
// in menus_monitors.cpp; the three subsystem-paired monitors (Container,
// EquipPicker, give-mode key poll) co-locate with their listbox spec
// entries in menus_listbox.cpp.

//
//   * The OnUpdate detour entry and the per-tick fan-out across all
//     mod subsystems now live in core_tick.cpp / core_tick.h. The
//     hook contract (CSWGuiManager::Update @ 0x40ce76, function name
//     "OnUpdate") is unchanged — the symbol just resolves to a
//     different TU.
//   * The three callables exposed below are what core_tick::Dispatch
//     consumes from the menu-side. Internal helpers (ValidateTabbedPanel,
//     MonitorFocusedControl, …) stay file-static; the deferred-op queue
//     state was split out in Step 3 (menus_pending.{h,cpp}); subsequent
//     refactor steps split listbox handlers and chain navigation further.

namespace acc::menus {

void ValidatePanels() {
    // Defensive — if the engine freed the panel that DetectTabsCluster
    // last latched onto, drop the stale pointer before any input handler
    // can deref it.
    ValidateTabbedPanel();
    // Same hazard for g_chainPanel / g_chain[].control: when Esc on the
    // Main-Menu Optionen strip (or any other panel our Esc gate doesn't
    // catch) falls through to the engine's native handler, the engine
    // destroys the panel and its children. The chain still references the
    // freed buttons; next tick MonitorFocusedControl dereferences one and
    // FromControl's SEH-caught AV interacts with /GS to fastfail. Confirmed
    // by crash dump 8752 (TID 22220, ESI=0x137af76c = the Auto-Pause button
    // last navigated to before Esc closed the parent strip). Same root
    // cause as the InGameOptions sub-screen fix; that case routes through
    // a queued FireActivate which already calls InvalidateChain, but the
    // engine-handled Esc path has no such hook — guard generically here.
    ValidateChainPanel();
}

void TickMonitors() {
    // Post-Step-5 cleanup: monitors split across two TUs. General monitors
    // (focus / panel-contents / dialog-reply) live in menus_monitors.cpp;
    // listbox-paired monitors (Container, EquipPicker, give-mode key poll)
    // co-locate with their spec entries in menus_listbox.cpp.
    //
    // Store runs FIRST so its trade-outcome speech ("Verkauft" /
    // "Gekauft" / "Kann nicht …") and chain rebind land before the focus
    // monitor's per-tick re-extract of the focused chain row. Without
    // this ordering, a successful sell speaks the next item's name
    // (focus monitor saw the row's text mutate when the listbox
    // repopulated) BEFORE the outcome — sounds like the announce is
    // about a different item than the user just sold.
    acc::menus::store::TickMonitorMode();
    acc::menus::monitors::TickGeneralMonitors();
    acc::menus::listbox::TickListboxMonitors();
    acc::menus::editbox::TickEditboxMonitors();
    acc::menus::galaxymap::Tick();
    acc::menus::keymap::Tick();
}

void PollHomeEndKeys() {
    bool home = acc::hotkeys::Pressed(acc::hotkeys::Action::NavHome);
    bool end_ = acc::hotkeys::Pressed(acc::hotkeys::Action::NavEnd);
    if (!home && !end_) return;

    // Both pressed in the same tick: prefer Home (the user is unlikely to
    // hit both intentionally; defer End to the next press).
    int code = home ? kInputHome : kInputEnd;

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) {
        acclog::Write("Menus.PollHomeEnd",
                      "%s pressed but mgr=NULL; ignored",
                      home ? "Home" : "End");
        return;
    }

    // Synthesise the manager dispatch through our own hook. The hook
    // re-enters listbox dispatch / editbox dispatch / chain nav exactly
    // as a real engine-routed keypress would. s_synthesizedNav suppresses
    // the press-release tracker write so an unconsumed Home/End can't
    // clobber a still-pending real consumed press (e.g. an Enter that
    // hasn't received its release yet).
    s_synthesizedNav = true;
    ::OnHandleInputEvent(mgr, code, /*param_2=*/1);
    s_synthesizedNav = false;
}

// Drain the menu-side pending-op queue. Called from core_tick::Dispatch
// after all monitors have run. The queued op was set by an input handler
// (chain Enter, Esc, Left/Right) on this or a recent tick; dispatching
// here keeps deep engine re-entry off the input-hook stack.
//
// Step 3 of the refactor: queue state + drain logic moved to
// menus_pending.{h,cpp}. This wrapper just resolves the GuiManager
// singleton and forwards.
void TickPendingOps() {
    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    pending::Drain(gm);
}

// Drain the pending-announce slot. Called early in TickMonitors so any
// intra-tick burst of SetActive events (panel-open NULL/first/settled
// triple, cursor-warp echo after voluntary nav) collapses to one
// announcement of the final settled focus. Channel-0 dedup short-circuits
// when the voluntary AnnounceControl path already spoke the same text.
//
// Chain-coherence drop: if the slot points at a chain entry on the bound
// panel that is NOT g_chainIndex (i.e. the engine fired SetActive on a
// sibling control after a voluntary chain step), suppress speech. This is
// the InGameOptions cursor-warp hit-test miss pattern — chain steps to
// "Gameplay" (index 2), engine's own DOWN handler / hit-test miss fires
// SetActive on "Spiel laden" (chain[0]) instead. The chain handler's
// AnnounceControl has already spoken the intended target; drop the
// engine's wrong-sibling echo so the user doesn't hear two button names.
//
// Doesn't suppress: legitimate user-driven focus changes (mouse hover /
// click on a non-chain control or on a chain control via direct cursor) —
// those land on the chain's matching index (handled by SpeakIfChanged
// dedup matching) or on a non-chain control (gate fails open and speaks).
void DrainPendingAnnounce() {
    void* panel   = s_pendingAnnouncePanel;
    void* control = s_pendingAnnounceControl;
    s_pendingAnnouncePanel   = nullptr;
    s_pendingAnnounceControl = nullptr;
    if (!control) return;

    // Multi-row listbox guard — mirrors AnnounceControl in menus_monitors.cpp.
    // OnSetActiveControl on a multi-row listbox container is engine-driven
    // panel-open auto-focus, not user navigation. Speaking "control N"
    // here was the source of the "control 3" noise on Fähigkeiten /
    // Inventar open after the FromControl listbox-blob path was removed.
    // Row navigation inside the listbox is owned by ListBoxPanelSpec /
    // chain handlers.
    const bool isListBoxContainer = IsListBox(control);
    if (isListBoxContainer) {
        auto* lb = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(control) + kListBoxControlsOffset);
        if (lb && lb->data && lb->size > 1) return;
    }

    if (g_chainPanel == panel && g_chainCount > 0 &&
        g_chainIndex >= 0 && g_chainIndex < g_chainCount &&
        g_chain[g_chainIndex].control != control)
    {
        int slotIdx = -1;
        for (int i = 0; i < g_chainCount; ++i) {
            if (g_chain[i].control == control) { slotIdx = i; break; }
        }
        if (slotIdx >= 0) {
            acclog::Write("Announce",
                          "chain-coherence drop: slot=%p (chain[%d]) != "
                          "chain[%d]=%p; engine sibling-focus echo "
                          "after voluntary nav",
                          control, slotIdx, g_chainIndex,
                          g_chain[g_chainIndex].control);
            return;
        }
    }

    char text[256];
    const char* source = acc::menus::extract::FromControl(
        control, text, sizeof(text), panel);
    if (source) {
        SpeakIfChanged(/*channel=*/0, text);
        return;
    }

    // A listbox container with no extractable text is the engine's panel-open
    // auto-focus on an empty / not-yet-populated list (workbench LB_ITEMS at
    // controls[0], id 0). The multi-row guard above only catches populated
    // lists; an empty one leaked the "control 0" spoken on workbench open.
    // Containers carrying real text returned via the FromControl success path,
    // so this drops only contentless containers, never a navigable control.
    if (isListBoxContainer) return;

    // No extractable text: announce a "control N" placeholder. Bypasses
    // SpeakIfChanged dedup deliberately (memory:
    // feedback_never_silence_fallback_announcement) — better to hear
    // repeated "control 11" than to silently drop a focus event.
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlIdOffset);
    char placeholder[64];
    snprintf(placeholder, sizeof(placeholder), "control %d", id);
    prism::Speak(placeholder, /*interrupt=*/false);
}

void ClearPendingAnnounce() {
    s_pendingAnnouncePanel   = nullptr;
    s_pendingAnnounceControl = nullptr;
}

bool IsDrilledIntoSubScreen() { return g_drilledIntoSubScreen; }
void SetDrilledIntoSubScreen(bool drilled) {
    g_drilledIntoSubScreen = drilled;
}

}  // namespace acc::menus

// DllMain + OnRulesInit + EnsurePrismInitialized live in core_dllmain.cpp.
// OnSetMoveToModuleString moved to transitions.cpp next to
// AnnouncePreLoadDestination.
