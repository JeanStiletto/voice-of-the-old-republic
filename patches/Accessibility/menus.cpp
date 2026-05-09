// KOTOR Accessibility — menu-side hook handlers (chain navigation, focus
// events, input dispatch, per-tick monitors).
//
// Layering:
//   log.{h,cpp}             file/debug logging primitives
//   tolk.{h,cpp}            screen-reader bridge (LoadLibrary'd lazily)
//   core_dllmain.cpp        DllMain + OnRulesInit + EnsureTolkInitialized
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
#include "tolk.h"
#include "menus.h"           // public surface — Step 1 mod-wide tick split
#include "menus_charsheet.h" // Step 2A — character-sheet opener lifted out
#include "menus_chargen_attr.h" // Chargen "Attribute" panel label + selected_ability sync
#include "menus_extract.h"   // Step 2B — text extraction lifted out
#include "menus_internal.h"  // Step 2B — shared seam with menus_extract
#include "menus_pending.h"   // Step 3 — deferred-op queue lifted out
#include "menus_listbox.h"   // Step 4 — listbox-driven panel dispatcher
#include "menus_chain.h"     // Step 5 — chain navigation lifted out
#include "menus_monitors.h"  // Post-Step-5 — general per-tick monitors
#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_player.h"   // Phase 1 lay-off 4 (test fixture only)
#include "engine_reads.h"
#include "audio_bus.h"       // Phase 1 lay-off 4 (test fixture only)
#include "announce_degrees.h" // Phase 4 sub-feature D
#include "probe_mouselook.h"  // Phase 4 lay-off 2 — view-mode probe
#include "view_mode.h"        // Phase 4 lay-off 3 — view-mode skeleton
#include "cycle_input.h"     // Phase 2 lay-off 3
#include "guidance_autowalk.h"  // Phase 2 lay-off 5 (progress watchdog)
#include "camera_announce.h"    // Phase 2 ad-hoc — camera-direction on A/D
#include "diag_engine_select.h" // Phase 2 diagnostic — Q/E/Tab observation
#include "interact_hotkey.h"    // Phase 2 lay-off 9b
#include "passive_narrate.h"    // Phase 2 lay-off 9a
#include "peek_description.h"   // Shift+arrow description peek
#include "probe_world_hover.h"  // Phase 2 lay-off 9-probe (diagnostic)
#include "radial_menu.h"        // CSWGuiTargetActionMenu input gate
#include "spatial_change_detector.h"  // Phase 3 lay-off 3 — Pillar 1 Trigger 1
#include "audio_footstep_suppress.h"  // Phase 3 lay-off 5 — stuck-detection
#include "strings.h"            // Container loot panel announces
#include "transitions.h"        // Phase 2 lay-off 7 — Pillar 2 area+room announce
#include "turn_announce.h"      // Phase 2 ad-hoc — Pillar 2 sub-feature C

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
// externs (g_chainIndex on chain-step advance, virtual-line state on
// panel change) work the same way through using-declarations.
using acc::menus::chain::ChainEntry;
using acc::menus::chain::kMaxChainEntries;
using acc::menus::chain::g_chain;
using acc::menus::chain::g_chainPanel;
using acc::menus::chain::g_chainIndex;
using acc::menus::chain::g_chainCount;
using acc::menus::chain::g_tabbedPanel;
using acc::menus::chain::g_tabsStart;
using acc::menus::chain::g_tabsCount;
using acc::menus::chain::g_tabClickOffsetY;
using acc::menus::chain::g_equipSlotClickOffsetY;
using acc::menus::chain::g_classIconClickOffsetX;
using acc::menus::chain::g_virtualLines;
using acc::menus::chain::g_virtualLineCount;
using acc::menus::chain::g_virtualLineIdx;
using acc::menus::chain::kMaxVirtualLines;
using acc::menus::chain::kMaxVirtualLineLen;
using acc::menus::chain::RebindChain;
using acc::menus::chain::ResetTabbedState;
using acc::menus::chain::ValidateTabbedPanel;
using acc::menus::chain::DetectTabsCluster;
using acc::menus::chain::IsTabButton;
using acc::menus::chain::FindAdjacentArrow;
using acc::menus::chain::FindCloseButton;
using acc::menus::chain::FindCancelButton;
using acc::menus::chain::FindChainEntry;
using acc::menus::chain::ReadPanelActiveControl;
using acc::menus::chain::ParseVirtualLines;

// Post-Step-5 cleanup: general-monitor TU and listbox-monitor extension.
// AnnounceControl (writes monitor state) lives in menus_monitors; chain
// handlers in OnHandleInputEvent below call it through this using-decl.
using acc::menus::monitors::AnnounceControl;

// Forward decl from core_dllmain.cpp. The first hook to fire calls this so
// Tolk is loaded the moment any focus / input event reaches us.
void EnsureTolkInitialized();

// Forward declarations + the shared kEquipBtn* / kEquipLb* constants moved
// to menus_internal.h in Step 2B. g_currentPanel is declared there as
// extern; defined later in this TU. The four detail-namespace helpers
// (IsChainNavigable, IsClassSelectionIcon, ClassLabelCache*) and
// GetControlCenter are defined further down with the chain machinery and
// brought back into unqualified scope by the using-declarations above.

// CSWGuiSaveLoad control IDs from saveload.gui (verified against chain logs:
// patch-20260505-160124.log lines 45-65 et al). Stable across save and load
// contexts — both render through the same .gui file.
//
//   id=0   games_listbox       (CSWGuiListBox; rows are CSWGuiSaveLoadEntry)
//   id=11  delete_button       ("L\xF6schen" / "Delete")
//   id=12  back_button         ("Abbrechen" / "Cancel")
//   id=14  saveload_button     ("Laden" / "Save" / etc.)
constexpr int kSaveLoadLbGamesId    =  0;
constexpr int kSaveLoadBtnDeleteId  = 11;
constexpr int kSaveLoadBtnBackId    = 12;
constexpr int kSaveLoadBtnSaveLoadId = 14;


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
//   0 = panel SetActiveControl
//   1 = listbox row SetActiveControl
static void SpeakIfChanged(int channel, const char* text) {
    static char s_last[2][256] = {{0}, {0}};
    if (channel < 0 || channel >= 2 || !text) return;
    if (strncmp(s_last[channel], text, sizeof(s_last[channel])) == 0) return;
    strncpy_s(s_last[channel], text, _TRUNCATE);
    tolk::Speak(text, /*interrupt=*/false);
}

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
constexpr uintptr_t kAddrPanelSetActiveControl = 0x0040a630;
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
static bool g_drilledIntoSubScreen = false;

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

// Speech-suppression budget for OnSetActiveControl. After a voluntary nav
// action (chain step / Enter activate), set to a small N. Each subsequent
// OnSetActiveControl call decrements and suppresses speech regardless of
// which control the event targets. Covers two distinct echoes per nav:
//
//   1. The engine's own focus handler firing on the keypress (lands on a
//      DIFFERENT control than our chain target on Options-style sub-dialogs
//      and InGameEquip — engine's nav order ≠ visual layout).
//   2. The cursor-warp echo, which lands on our actual target. The
//      pending::CursorMoveTarget() self-dedup in OnSetActiveControl already
//      catches this one cleanly.
//
// (1) was the source of the "afterthought" double-speak: chain-step speaks
// the right thing, then engine SetActiveControl fires for a sibling and
// speaks it as a second utterance. Match-only dedup couldn't catch (1)
// because newControl wasn't the pending target. Budget=2 catches both echoes
// without over-suppressing legitimate later focus changes (mouse hover,
// next user action), since by the time the next user input arrives the
// budget has decremented to 0.
int g_navSpeechSuppressBudget = 0;

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// Per-frame focus monitor + its last-seen state moved to
// menus_monitors.cpp. AnnounceControl (which writes that state to keep
// the monitor in sync with voluntary speak events) moved with it; chain
// handlers below call it via the using-declaration at the top of this
// file.

// Tabbed-panel state (g_tabbedPanel, g_tabsStart, g_tabsCount), the three
// click-offset compensations (g_tabClickOffsetY, g_equipSlotClickOffsetY,
// g_classIconClickOffsetX), and the virtual-line cursor state moved to
// menus_chain.cpp in Step 5. Brought back into unqualified scope via the
// using-declarations at the top of this file. See menus_chain.h for the
// rationale.
// Center pixel of a control's hit area. Returns false on null control or
// degenerate extent (zero/negative width/height — sometimes seen on hidden
// panels and templated control prototypes).
bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy) {
    if (!control) return false;
    auto* ext = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlExtentOffset);
    int width  = ext[2];
    int height = ext[3];
    if (width <= 0 || height <= 0) return false;
    outCx = ext[0] + width  / 2;
    outCy = ext[1] + height / 2;
    return true;
}

// Screen-absolute center of a CSWGuiListBox row. Listbox children's extents
// are listbox-local (origin at the listbox's top-left, not the screen) so
// click-sim at row.extent alone lands on dead space. Add the listbox's own
// extent origin to translate. Listboxes themselves are panel-direct children
// whose extents are already screen-absolute (panels render at fixed
// positions), so one accumulation step is sufficient for the InGameEquip
// LB_ITEMS case. If we ever need to click rows in a deeper-nested listbox,
// generalise this into a parent-chain walk.
static bool GetListBoxRowScreenCenter(void* lb, void* row, int& outCx, int& outCy) {
    if (!lb || !row) return false;
    auto* lbExt  = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(lb)  + kControlExtentOffset);
    auto* rowExt = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(row) + kControlExtentOffset);
    int rowW = rowExt[2];
    int rowH = rowExt[3];
    if (rowW <= 0 || rowH <= 0) return false;
    outCx = lbExt[0] + rowExt[0] + rowW / 2;
    outCy = lbExt[1] + rowExt[1] + rowH / 2;
    return true;
}

// True if the control is button-like (CSWGuiButton or its subclasses
// CharButton / ActivatedButton / ButtonToggle) OR a CSWGuiSlider.
// MoveMouseToPosition's hover→active promotion path is safe for buttons but
// crashes when the active control is a label (verified: navigating onto the
// main-menu "Neue Inhalte verfügbar…" label froze the game). Sliders are
// included because Sound's Music/Voice/SFX/Movie controls are real sliders
// and we want chain navigation to land on them so we can announce their
// numeric value.
//
// Long-term: replace with a proper CSWGuiControl::GetIsSelectable call
// (vtable lookup at 0x4189d0) to also include editbox / listbox / etc.
bool acc::menus::detail::IsChainNavigable(void* control) {
    if (!control) return false;
    if (CallDowncast(control, kVtableAsButton)        != nullptr) return true;
    if (CallDowncast(control, kVtableAsButtonToggle)  != nullptr) return true;
    if (IsSlider(control))                                        return true;
    return false;
}

// AnnounceControl moved to menus_monitors.cpp (writes the focus monitor's
// last-seen state to keep voluntary speech in sync). Brought back into
// unqualified scope via the using-declaration at the top of this file.

// First focus into a panel speaks the panel's "title" — the first label-like
// child we can find — so the user knows which menu they're in. Subsequent
// per-control announcements still fire from OnSetActiveControl as the user
// navigates, so this is just the entry banner, not a layout dump.
//
// Heuristic: walk panel.controls in order, return the text of the first
// CSWGuiLabel / CSWGuiLabelHilight child with announceable text. Buttons and
// other interactive controls are skipped — they get announced through the
// regular focus path. If no label exists we stay silent and rely on the
// SetActiveControl announcement of the focused child to orient the user.
static void AnnouncePanelTitle(void* panel) {
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* child = list->data[i];
        if (!child) continue;
        if (CallDowncast(child, kVtableAsLabel) == nullptr &&
            CallDowncast(child, kVtableAsLabelHilight) == nullptr) {
            continue;
        }
        char text[256];
        if (acc::menus::extract::FromControl(child, text, sizeof(text), panel)) {
            acclog::Write("Menus.PanelWalk", "title parent=%p label=%p text=\"%s\"",
                          panel, child, text);
            tolk::Speak(text, /*interrupt=*/false);
            return;
        }
    }
}

// DetectTabsCluster, ResetTabbedState, ValidateTabbedPanel, and
// ParseVirtualLines moved to menus_chain.cpp in Step 5. Brought back into
// unqualified scope via the using-declarations at the top of this file.

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

// Locate a child control on `panel` by its +0x50 ID field. The .gui-time IDs
// are stable per panel kind, so this is the canonical way to address a known
// control in a known panel without text-matching (which breaks across
// localizations) or relying on panel.controls index (which can shift).
//
// Step 4 of the refactor lifted the listbox-driven panel handlers
// (Container / SaveLoad / EquipPicker) into menus_listbox.cpp; this helper
// now spans both TUs via menus_internal.h.
void* acc::menus::detail::FindControlById(void* panel, int id) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 64 ? 64 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(c) + 0x50);
        if (cid == id) return c;
    }
    return nullptr;
}

// FindCloseButton / FindCancelButton moved to menus_chain.cpp in Step 5.
// They're heuristic finders for the back-out / cancel buttons used by the
// Esc handler in OnHandleInputEvent. Brought back into unqualified scope
// via the using-declarations at the top of this file.

// Detect the CSWGuiSaveLoad panel (the "Spiel laden" / "Spiel speichern"
// dialog). The panel is allocated dynamically when the user activates the
// load/save action, has no slot in CGuiInGame, and so doesn't show up via
// IdentifyPanel.
//
// We classify by a *structural* signature — the four .gui-time control IDs
// the saveload.gui resource declares:
//
//   - id=0  games_listbox       (CSWGuiListBox)
//   - id=11 delete_button       (CSWGuiButton)
//   - id=12 back_button         (CSWGuiButton)
//   - id=14 saveload_button     (CSWGuiButton)
//
// .gui-time IDs are baked into the resource at build time and identical
// between the save and load contexts (both render through the same
// CSWGuiSaveLoad layout). They're language-independent — only the rendered
// label text is localised, not the IDs — so this matches every locale
// without enumerating titles. The combined four-ID tuple is specific
// enough that no other panel we've observed in the chain logs collides.
//
// We deliberately do NOT generalise this to "any panel that has a listbox":
// listbox row semantics vary. Options sub-dialogs render settings as
// listbox-row buttons whose onClick toggles state directly, dialog replies
// have engine-bound arrow keys that mutate selection_index already, and
// description listboxes are read-only. The select-then-confirm-via-button
// pattern is shared by Container, Equip-picker, and SaveLoad — all three
// kinds detected per-panel today.
bool acc::menus::detail::IsSaveLoadPanel(void* panel) {
    if (!panel) return false;

    void* lb = FindControlById(panel, kSaveLoadLbGamesId);
    if (!lb) return false;
    void** lbVtable = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(lbVtable) != kVtableListBox) return false;

    return FindControlById(panel, kSaveLoadBtnSaveLoadId) != nullptr &&
           FindControlById(panel, kSaveLoadBtnBackId)     != nullptr &&
           FindControlById(panel, kSaveLoadBtnDeleteId)   != nullptr;
}

// Read the user-visible text of a CExoString-style field on a control. Returns
// nullptr if the field is empty or the c_string pointer is null. The two
// fields we care about on CSWGuiSaveLoadEntry (areaname, lastmodule) are plain
// CExoStrings populated from the save GFF — no TLK indirection, no engine
// rendering callback needed. Output is borrowed from the engine; valid until
// the entry is freed (we use it inline within a single input event).
const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset) {
    if (!entry) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(entry);
    auto* str  = reinterpret_cast<CExoString*>(base + fieldOffset);
    if (!str || !str->c_string || str->length == 0) return nullptr;
    return str->c_string;
}

// ListBoxNavResult struct + DriveListBoxSelection signature now live in
// menus_internal.h (Step 4 — listbox-driven panels lifted to
// menus_listbox.cpp). The function is still defined here because the
// dialog/container monitors call it too.
//
// `minSel` is the lowest selectable row index. Pass 0 for normal listboxes;
// pass 1 for the equip-picker LB_ITEMS where row 0 is the .gui-time
// PROTOITEM template (verified empirically — see
// docs/equip-flow-investigation.md). Existing selection_index < minSel
// (typically the engine's initial -1) lands on `minSel` regardless of
// direction, closer to user expectation than wrapping or staying silent.
//
// Returns false iff `listbox` is null or has rowCount==0; caller logs +
// ignores. On true, `out` is fully populated.
bool acc::menus::detail::DriveListBoxSelection(void* listbox, bool navDown,
                                               short minSel,
                                               ListBoxNavResult& out)
{
    out = {};
    if (!listbox) return false;

    auto* lbBase = reinterpret_cast<unsigned char*>(listbox);
    auto* lbList = reinterpret_cast<CExoArrayList*>(
        lbBase + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;
    if (rowCount <= 0) {
        out.rowCount = 0;
        return false;
    }

    short* selPtr = reinterpret_cast<short*>(
        lbBase + kListBoxSelectionIndexOffset);
    short* topPtr = reinterpret_cast<short*>(
        lbBase + kListBoxTopVisibleIndexOffset);
    short* ippPtr = reinterpret_cast<short*>(
        lbBase + kListBoxItemsPerPageOffset);

    short oldSel = *selPtr;
    short newSel;
    if (oldSel < minSel) {
        newSel = minSel;
    } else if (navDown) {
        newSel = (short)(oldSel + 1);
        if (newSel >= rowCount) newSel = (short)(rowCount - 1);
    } else {
        newSel = (short)(oldSel - 1);
        if (newSel < minSel) newSel = minSel;
    }

    if (newSel != oldSel) {
        *selPtr = newSel;
        short ipp = *ippPtr;
        short top = *topPtr;
        if (ipp <= 0) ipp = 1;
        if (newSel < top) {
            *topPtr = newSel;
        } else if (newSel >= top + ipp) {
            *topPtr = (short)(newSel - ipp + 1);
        }
    }

    out.oldSel   = oldSel;
    out.newSel   = newSel;
    out.rowCount = rowCount;
    out.row      = (newSel >= 0 && newSel < rowCount) ? lbList->data[newSel]
                                                       : nullptr;
    return true;
}

// Queue activation of the chain-navigable button child of `panel` whose
// .gui-time id matches `buttonId`. Mirrors the activate path used by
// chain-Enter elsewhere: queues an Activate op via menus_pending and sets
// the speech-suppress budget so the post-activation focus echo doesn't
// double-speak. The actual vtable[15].HandleInputEvent runs one tick later
// in TickPendingOps.
//
// Returns false on debounce (any op already pending) or if the button id
// isn't found on the panel; caller still consumes the keypress in those
// cases so the engine's stale activeControl can't take over.
//
// `logPrefix` is used in the diagnostic log line — pass something like
// "Container: Enter -> BTN_OK".
bool acc::menus::detail::QueueButtonByIdActivate(void* panel, int buttonId,
                                                 const char* logPrefix)
{
    if (acc::menus::pending::IsPending()) {
        acclog::Write(logPrefix, "-- op already pending; ignoring");
        return false;
    }
    void* tgt = FindControlById(panel, buttonId);
    if (!tgt) {
        acclog::Write(logPrefix, "-- target id=%d not resolved on panel=%p",
                      buttonId, panel);
        return false;
    }
    acc::menus::pending::QueueActivate(tgt);
    g_navSpeechSuppressBudget = 2;
    acclog::Write(logPrefix, "panel=%p target=%p", panel, tgt);
    return true;
}

// Positional detector for chargen class-icon buttons. The 6 class icons
// are CSWGuiClassSelChar[6] starting at panel+0x6c with stride 0x25c, and
// each CSWGuiClassSelChar embeds a CSWGuiButton at offset 0 — so a focused
// class-icon control pointer lands exactly on `panel + 0x6c + i * 0x25c`
// for some 0 ≤ i < 6.
bool acc::menus::detail::IsClassSelectionIcon(void* panel, void* control) {
    if (!panel || !control) return false;
    void** vt = *reinterpret_cast<void***>(panel);
    if (reinterpret_cast<uintptr_t>(vt) != kVtableCSWGuiClassSelection) {
        return false;
    }
    auto* panelBase = reinterpret_cast<unsigned char*>(panel);
    auto* ctrlBase  = reinterpret_cast<unsigned char*>(control);
    ptrdiff_t off = ctrlBase - panelBase;
    ptrdiff_t arrayEnd = (ptrdiff_t)(kClassSelectionsArrayOffset +
                                     kClassSelectionsCount * kClassSelCharSize);
    if (off < (ptrdiff_t)kClassSelectionsArrayOffset || off >= arrayEnd) {
        return false;
    }
    return ((off - (ptrdiff_t)kClassSelectionsArrayOffset) %
            (ptrdiff_t)kClassSelCharSize) == 0;
}

// Per-icon class-name cache for CSWGuiClassSelection. See the long
// comment in ExtractAnnounceableText step 9c for why this exists. Sized
// to hold all 6 icons of a single panel; key is (panel, icon). Keyed by
// panel as well as icon so a chargen restart on a new panel instance
// doesn't surface stale entries from the previous run. First-write
// wins — once an entry is locked, subsequent updates are ignored so the
// engine's transient class_label revert can't corrupt a settled value.
struct ClassLabelCacheEntry {
    void* panel;
    void* icon;
    char  text[64];
};
static constexpr int kClassLabelCacheSize = 8;
static ClassLabelCacheEntry g_classLabelCache[kClassLabelCacheSize];

const char* acc::menus::detail::ClassLabelCacheLookup(void* panel, void* icon) {
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        const auto& e = g_classLabelCache[i];
        if (e.panel == panel && e.icon == icon && e.text[0] != '\0') {
            return e.text;
        }
    }
    return nullptr;
}

void acc::menus::detail::ClassLabelCacheStore(void* panel, void* icon, const char* text) {
    if (!panel || !icon || !text || text[0] == '\0') return;
    // First-write wins for a (panel, icon) pair: bail out if already cached.
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        const auto& e = g_classLabelCache[i];
        if (e.panel == panel && e.icon == icon) return;
    }
    // Find a free slot, evicting any entry from a different panel if full.
    int slot = -1;
    for (int i = 0; i < kClassLabelCacheSize; ++i) {
        if (g_classLabelCache[i].panel == nullptr) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < kClassLabelCacheSize; ++i) {
            if (g_classLabelCache[i].panel != panel) { slot = i; break; }
        }
    }
    if (slot < 0) return;
    g_classLabelCache[slot].panel = panel;
    g_classLabelCache[slot].icon  = icon;
    strncpy_s(g_classLabelCache[slot].text, text, _TRUNCATE);
}

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
// Distinct from IsModalTextPanel: that one identifies popups whose body
// text the engine wraps in a single-row listbox (so RebindChain can promote
// it to a text-only chain entry); this one identifies popups that need a
// keyboard dismiss path. Overlap exists (MessageBoxModal/TutorialBox/
// AreaTransition are in both) but each is asked a different question.
static bool IsModalPopupPanel(PanelKind k) {
    switch (k) {
    case PanelKind::MessageBoxModal:
    case PanelKind::TutorialBox:
    case PanelKind::AreaTransition:
    case PanelKind::StatusSummary:
    case PanelKind::ControllerLossBox:
    case PanelKind::SkillInfoBox:
    case PanelKind::SoloModeQuery:
        return true;
    default:
        return false;
    }
}

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
static void WalkChildren(const char* label, void* parent, size_t offset) {
    if (!parent) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(parent) + offset);
    if (!list->data || list->size <= 0) {
        acclog::Write(label, "walk parent=%p children=0", parent);
        return;
    }
    int count = list->size;
    if (count > 256) {
        acclog::Write(label, "walk parent=%p size_oob=%d (capped)", parent, count);
        count = 256;
    }
    acclog::Write(label, "walk parent=%p children=%d", parent, list->size);
    for (int i = 0; i < count; ++i) {
        void* child = list->data[i];
        if (!child) {
            acclog::Write(label, "  [%d]=NULL", i);
            continue;
        }
        int id = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(child) + 0x50);
        char text[256];
        // Pass `parent` so the perkind fallback resolves correctly when
        // walking InGameMenu's children — the icon labels/buttons have empty
        // CExoString/strref/text_object/gui_string and only resolve via the
        // panel-keyed perkind table.
        const char* source = acc::menus::extract::FromControl(child, text, sizeof(text),
                                                     parent);
        if (source) {
            acclog::Write(label, "  [%d] %p id=%d src=%s text=\"%s\"",
                          i, child, id, source, text);
        } else {
            char vtbl[160];
            DumpControlVtable(child, vtbl, sizeof(vtbl));
            acclog::Write(label, "  [%d] %p id=%d src=none %s",
                          i, child, id, vtbl);
        }
    }
}

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
// Chargen class-icon cache pump. The hook fires at the very entry of
// CSWGuiPanel::SetActiveControl — before the function writes the new
// active_control and before any OnEnterButton runs for the new icon. At
// this moment panel.active_control still points at the OUTGOING icon
// (the one the user is leaving) and class_label still carries that
// icon's class string. Caching (outgoing -> label) on every transition
// closes the gap that the per-frame monitor leaves under rapid arrow
// input: the monitor only catches icons the user dwells on, but every
// SetActiveControl event fires regardless of duration.
static void PrefillClassIconCacheOnTransition(void* panel, void* newControl) {
    if (!panel) return;
    void** pVt = *reinterpret_cast<void***>(panel);
    if (reinterpret_cast<uintptr_t>(pVt) != kVtableCSWGuiClassSelection) return;
    void* outgoing = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(panel) + kPanelActiveControlOffset);
    if (!outgoing || outgoing == newControl) return;
    if (!IsClassSelectionIcon(panel, outgoing)) return;
    if (ClassLabelCacheLookup(panel, outgoing) != nullptr) return;
    void* classLabel = reinterpret_cast<unsigned char*>(panel) +
                       kClassSelectionClassLabelOffset;
    char text[256];
    if (ExtractTextOrStrRefIndirect(classLabel,
                                    kLabelTextOffset,
                                    kLabelStrRefOffset,
                                    kLabelTextObjectOffset,
                                    text, sizeof(text)) &&
        text[0] != '\0') {
        ClassLabelCacheStore(panel, outgoing, text);
        acclog::Write("Menus.PerKind",
                      "ClassSelection prefill outgoing=%p -> \"%s\"",
                      outgoing, text);
    }
}

// Update g_currentPanel and clear the virtual-line cursor on panel
// transitions. Tabbed-mode tab-cluster state DELIBERATELY persists across
// transitions into sub-dialogs of the tabbed panel — the tab strip lives
// on the parent panel (Options) and is still the right thing for Tab/
// Shift+Tab while the user is inside one of its sub-dialogs. ValidateTabbed-
// Panel drops the cluster state when the engine frees the underlying panel.
static void UpdateFocusedPanelState(void* panel) {
    void* prevPanel = g_currentPanel;
    g_currentPanel = panel;
    if (panel != prevPanel) {
        g_virtualLineCount = 0;
        g_virtualLineIdx   = -1;
    }
}

// First focus event into a previously-unseen panel: dump every child
// control + capture cycle-button category text. Cycle widgets carry their
// localized category in their CExoString at panel construction time; the
// engine replaces it with the persisted value (e.g. "Difficulty" -> "Normal")
// shortly after, and our FireActivate calls overwrite it again on each
// cycle. SetActiveControl's first fire on a new panel is the earliest
// reachable capture point.
static void WalkAndCaptureOnFirstSight(void* panel) {
    static void* s_lastPanel = nullptr;
    if (!panel || panel == s_lastPanel) return;
    s_lastPanel = panel;

    LogManagerStack(*reinterpret_cast<void**>(kAddrGuiManagerPtr),
                    "panel-walk");
    PanelKind kind = IdentifyPanel(panel);
    acclog::Write("Menus.PanelWalk", "panel=%p kind=%s",
                  panel, PanelKindName(kind));
    WalkChildren("Menus.PanelWalk", panel, kPanelControlsOffset);

    // Capture cycle-button categories before any activation rewrites the
    // value-display button's CExoString. The cache lives in
    // menus_extract.cpp; we write through its public setter.
    acc::menus::extract::ResetCycleCategoryCache();
    auto* plist = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!plist || !plist->data || plist->size <= 0) return;

    int pn = plist->size > 256 ? 256 : plist->size;
    for (int i = 0; i < pn; ++i) {
        void* c = plist->data[i];
        if (!c) continue;
        if (CallDowncast(c, kVtableAsButton) == nullptr) continue;
        if (IsToggle(c)) continue;
        void* leftN  = FindAdjacentArrow(panel, c, /*toRight=*/false);
        void* rightN = FindAdjacentArrow(panel, c, /*toRight=*/true);
        if (!leftN && !rightN) continue;

        char text[128];
        bool gotText = false;
        uint32_t strref = ReadU32(c, kButtonStrRefOffset);
        if (LookupTlk(strref, text, sizeof(text))) {
            gotText = true;
        } else if (ReadCExoString(c, kButtonTextOffset, text, sizeof(text))) {
            gotText = true;
        }
        if (gotText) {
            acc::menus::extract::CaptureCycleCategory(c, text);
            acclog::Write("Menus.CycleCategory", "control=%p text=\"%s\" strref=%u",
                          c, text, strref);
        }
    }

    // Chargen "Attribute" panel: each value button has an inline text "8"
    // (the current value), so the generic capture above resolves the
    // category to the value itself (useless). Override here by reading
    // ability_labels[i] for each ability_buttons[i] and binding the pair
    // into the cycle-category cache so FromControl produces "Stärke, 8".
    acc::menus::chargen_attr::CaptureLabelsIfApplicable(panel);
}

// First focus into a new panel: speak its title once. The focused
// control's announcement still fires after, so the user hears
// "<panel title>, <focused control>" on entry.
static void SpeakPanelTitleOnFirstSight(void* panel) {
    if (!panel || panel == g_lastTitledPanel) return;
    g_lastTitledPanel = panel;
    AnnouncePanelTitle(panel);
}

// Self-dedup: if this SetActiveControl was caused by our deferred
// MoveMouseToPosition, the input hook already announced the target.
// Skip Tolk and clear the pending marker. Returns true if dedup fired.
static bool ConsumeCursorWarpDedup(int n, void* panel, void* newControl) {
    if (!newControl || newControl != acc::menus::pending::CursorMoveTarget()) {
        return false;
    }
    acclog::Write("Menus.SetActive", "#%d panel=%p new=%p (self-dedup; cursor sync)",
                  n, panel, newControl);
    acc::menus::pending::ClearCursorMoveTarget();
    // Cursor-warp echo arrived: voluntary nav has fully settled.
    g_navSpeechSuppressBudget = 0;
    return true;
}

// Voluntary-nav speech-suppression. Chain-step / Enter-activate handlers
// set the budget to a small N; decrement on any focus event and skip
// speech while > 0. Covers engine-side focus echoes that don't match the
// cursor-warp target (e.g. engine's UP handler picking a sibling on
// AutoPause / equip panels). Returns true if suppressed.
static bool ConsumeNavSpeechBudget(int n, void* panel, void* newControl) {
    if (g_navSpeechSuppressBudget <= 0) return false;
    int wasBudget = g_navSpeechSuppressBudget;
    --g_navSpeechSuppressBudget;
    int sid = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(newControl) + 0x50);
    acclog::Write("Menus.SetActive", "#%d panel=%p new=%p id=%d "
                  "(nav-suppress; budget %d->%d)",
                  n, panel, newControl, sid, wasBudget,
                  g_navSpeechSuppressBudget);
    return true;
}

// Speak the focused control's text (or "control N" placeholder), with
// Container-listbox suppression to keep the panel-open announce clean.
static void AnnounceNewFocusedControl(int n, void* panel, void* newControl) {
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(newControl) + 0x50);

    char text[256];
    const char* source = acc::menus::extract::FromControl(
        newControl, text, sizeof(text), panel);

    // Container loot panel: the engine's listbox text concatenates every
    // row into one giant utterance, which would drown the count + per-row
    // navigation announces from the container monitor. Log only.
    bool suppressForContainer =
        IsListBox(newControl) &&
        IdentifyPanel(panel) == PanelKind::Container;

    if (source) {
        acclog::Write("Menus.SetActive", "#%d panel=%p new=%p id=%d src=%s text=\"%s\"",
                      n, panel, newControl, id, source, text);
        if (!suppressForContainer) {
            SpeakIfChanged(/*channel=*/0, text);
        }
        return;
    }

    char vtbl[160];
    DumpControlVtable(newControl, vtbl, sizeof(vtbl));
    acclog::Write("Menus.SetActive", "#%d panel=%p new=%p id=%d src=none %s",
                  n, panel, newControl, id, vtbl);
    if (!suppressForContainer) {
        // Bypass SpeakIfChanged dedup deliberately: a non-readable focus
        // change deserves *some* announcement every time. Better to hear
        // "control 11" repeated than to silently skip a focus event.
        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder), "control %d", id);
        tolk::Speak(placeholder, /*interrupt=*/false);
    }
}

extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;

    PrefillClassIconCacheOnTransition(panel, newControl);
    UpdateFocusedPanelState(panel);
    WalkAndCaptureOnFirstSight(panel);
    SpeakPanelTitleOnFirstSight(panel);

    if (!newControl) {
        acclog::Write("Menus.SetActive", "#%d panel=%p newControl=NULL", n, panel);
        return;
    }

    if (ConsumeCursorWarpDedup(n, panel, newControl)) return;
    if (ConsumeNavSpeechBudget(n, panel, newControl)) return;

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
    EnsureTolkInitialized();

    static int n = 0;
    ++n;

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

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newRow) + 0x50);

    char text[256];
    const char* source = acc::menus::extract::FromControl(newRow, text, sizeof(text));

    if (source) {
        acclog::Write("Menus.ListBox", "SetActive #%d list=%p row=%p id=%d "
                      "p2=%d src=%s text=\"%s\"",
                      n, listBox, newRow, id, param2, source, text);

        // Lazy tabbed-mode detection: first listbox event after a panel
        // change probes whether the focused panel has the Options-style
        // listbox-at-[0] + button-cluster layout. If so we arm the tab/
        // virtual-line nav path and silence blob speech (the user navigates
        // lines explicitly with arrow keys instead).
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

        if (strchr(text, '\n')) {
            // Multi-line listbox blob (Options-style: all settings concatenated
            // by '\n' into a single CSWGuiLabel row). In tabbed mode we parse
            // the lines into a virtual cursor and speak them one-at-a-time on
            // arrow keys. In non-tabbed mode we silence them too — bulk
            // enumeration is too noisy. If a non-tabbed multi-line listbox
            // ever needs per-line nav, that's a future feature.
            if (g_tabbedPanel == g_currentPanel) {
                ParseVirtualLines(text);
                acclog::Write("Menus.ListBox", "blob silenced (tabbed mode); %d virtual lines parsed",
                              g_virtualLineCount);
            } else {
                int lines = 1;
                for (const char* p = text; *p; ++p) if (*p == '\n') ++lines;
                acclog::Write("Menus.ListBox", "blob silenced (non-tabbed); lines=%d",
                              lines);
            }
        } else {
            SpeakIfChanged(/*channel=*/1, text);
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
            tolk::Speak(placeholder, /*interrupt=*/false);
        }
    }
}

// CSWGuiControl::HandleFocusChange — hooked mid-function at 0x41896b.
// Demoted to log-only. The panel-level SetActiveControl hook above is the
// real announcement signal; HandleFocusChange fires twice per navigation
// (old loses focus + new gains focus) so speaking from here would echo.
extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    const char* tip; uint32_t tipLen; int id;
    ReadControlNameFields(thisPtr, tip, tipLen, id);
    acclog::Write("Menus.FocusChange", "#%d this=%p p1=%d id=%d tip[%u]=\"%s\"",
                  n, thisPtr, param_1, id, tipLen,
                  (tip && tipLen > 0) ? tip : "");
}

// CSWGuiManager::HandleInputEvent — hooked mid-function at 0x0040c907.
// This is the GUI manager's central input dispatcher: every key / mouse event
// the engine routes to any GUI surface passes through here before being
// virtual-dispatched to the active panel's per-class override. One hook
// covers every screen (title, Options, chargen, in-game menus, dialog,
// save/load) — replaces the old CSWGuiMainMenu-only hook at 0x67b395.
//
// We hook BEFORE the param_2 == 0 early-out, so we see press AND release
// edges. param_2 is logged as `val=` (0 = release, non-zero = press).
//
// At hook entry: ECX = this, EBX = param_1 (InputIndices key/button code),
// EAX = param_2 (state).
extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;

    // Resolve the foreground panel via the manager's modal_stack / panels[].
    // g_currentPanel tracks "last panel that received SetActiveControl" — fine
    // for per-instance state (sibling-label lookup, cycle-category capture)
    // but UNRELIABLE for routing, because flows that pre-instantiate multiple
    // panels in one frame (character creation: modal + 2 wizards) leave
    // g_currentPanel pointing at the last-walked panel, which is NOT the
    // visible foreground. Verified from patch-20260502-164320.log: in that
    // flow modal_stack.size goes 0→4 with the user-visible Standardcharakter
    // modal correctly at modal[top], while g_currentPanel had latched onto
    // a backgrounded wizard. See ManagerStack diagnostic and report.
    //
    // Fallback to g_currentPanel only when the manager pointer or the
    // foreground resolves to null (early-init frames before any panel
    // exists, or screens we don't yet understand).
    void* activePanel = nullptr;
    {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        void* fg = GetForegroundPanel(mgr);
        activePanel = fg ? fg : g_currentPanel;
        // First-fire-per-pair divergence log: when fg != g_currentPanel we
        // want to see it in the log, but only once per (fg, g_currentPanel)
        // tuple to avoid spamming during steady-state (every keypress in a
        // multi-panel flow would otherwise emit a line).
        if (fg && fg != g_currentPanel) {
            static void* s_lastFg = nullptr;
            static void* s_lastCp = nullptr;
            if (fg != s_lastFg || g_currentPanel != s_lastCp) {
                acclog::Write("Routing", "fg=%p current=%p (using fg)",
                              fg, g_currentPanel);
                s_lastFg = fg;
                s_lastCp = g_currentPanel;
            }
        }

        // Drill override: when the user has Entered into a sub-screen, retarget
        // the chain from the strip (kept in fg by SendPanelToBack) to the
        // sub-screen panel. Only fires when fg actually IS the strip — leaves
        // tutorial modals and Options sub-tabs (which become fg in their own
        // right) routing through fg directly.
        if (g_drilledIntoSubScreen) {
            if (IdentifyPanel(activePanel) == PanelKind::InGameMenu) {
                void* sub = acc::menus::monitors::FindActiveSubScreenPanel();
                if (sub) {
                    activePanel = sub;
                } else {
                    g_drilledIntoSubScreen = false;
                    acclog::Write("Drill", "sub-screen gone from panels[]; "
                                  "returning to strip");
                }
            }
        }
    }

    // Chain navigation: consume nav-up / nav-down on key-down. We only handle
    // press edges (param_2 != 0) so key-up events still pass through cleanly.
    // Other keys (Tab, Enter, mouse, F-keys) always pass through; activation
    // comes free from the engine via the normal click pipeline once the
    // cursor is over the chain target.
    bool consumed = false;

    // Radial action menu — embedded inside CSWGuiMainInterface, NOT a
    // separate panel. Run its gate before the panel-kind switch: when the
    // gate is armed (acc::picker::Drive opened a radial via PopulateMenus
    // and it has at least one populated row) Up/Down switches rows,
    // Left/Right cycles within a row via the engine's SelectNext/Prev,
    // Enter dispatches via DoTargetAction, Esc disarms. See radial_menu.h
    // for the full contract. Returns early because no other handler down
    // this function knows about the radial.
    if (acc::radial_menu::IsActive()) {
        bool radialConsumed =
            acc::radial_menu::HandleInputEvent(param_1, param_2);
        // Always log the event so we can correlate keyboard pressure
        // against the radial's state transitions in the patch log.
        int translated = acc::engine::ManagerTranslateCode(param_1);
        const char* tag = radialConsumed ? " RADIAL-CONSUMED" : " RADIAL-PASS";
        if (translated != param_1) {
            acclog::Write("Menus.Input", "#%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                          n, thisPtr, param_1,
                          acc::engine::InputIndexName(translated), translated,
                          param_2, tag);
        } else {
            acclog::Write("Menus.Input", "#%d this=%p key=%s(%d) val=%d%s",
                          n, thisPtr, acc::engine::InputIndexName(param_1),
                          param_1, param_2, tag);
        }
        if (radialConsumed) return 1;
        // Not consumed (e.g. release edge, or unhandled key like Tab):
        // fall through to the existing handlers below so unrelated input
        // still works while the radial is mounted (rare in practice; the
        // user's hands are on Up/Down/Enter while navigating the radial).
    }

    // Shift+Up / Shift+Down description peek. Runs before any panel
    // handler that consumes Up/Down (Container, equip picker, generic
    // chain) so a held Shift is read as "peek the focused item's
    // description" rather than navigating rows. Panels not in the
    // peek registry pass through unchanged. See peek_description.h.
    //
    // Pass the chain's current focus pointer when valid — peek's
    // panel-specific refresh functions need it to re-stage the
    // description for the focused row (panel.activeControl tracks a
    // different helper control during chain nav, not the item entry).
    {
        void* peekFocus = nullptr;
        if (g_chainPanel == activePanel &&
            g_chainIndex >= 0 &&
            g_chainIndex < g_chainCount) {
            peekFocus = g_chain[g_chainIndex].control;
        }
        if (acc::peek::HandleShiftArrow(param_1, param_2, activePanel,
                                        peekFocus)) {
            return 1;
        }
    }

    // Listbox-driven panels (Container loot, SaveLoad, EquipPicker)
    // dispatch through menus_listbox::TryHandleInput. Step 4 of the
    // refactor: three structurally similar handlers (~340 lines inline)
    // collapsed into a spec-table-driven dispatcher with one entry per
    // panel. See menus_listbox.h for the contract; the spec entries in
    // menus_listbox.cpp are where each panel's quirks (announce format,
    // Enter/Esc dispatch, fall-through behaviour) live.
    {
        int rv = 0;
        if (acc::menus::listbox::TryHandleInput(n, thisPtr, activePanel,
                                                 param_1, param_2, rv)) {
            return rv;
        }
    }


    // Pillar 4 cycle keys (`,` `.` `Shift+,` `Shift+.` `-` `Shift+-`) — Phase 2
    // lay-off 3. Routed first because cycle is in-game-only and the handler
    // self-gates on GetPlayerPosition; in menus / chargen / dialog it returns
    // false and the key falls through to the normal menu logic below.
    if (acc::cycle_input::TryHandleEvent(param_1, param_2)) {
        consumed = true;
    }

    // Enter-press activation. Two activation primitives, picked per-target:
    //
    //   * **Tab buttons** in the tabbed parent panel (Options:
    //     Gameplay/Auto-Pause/Grafik/Sound/Feedback) require the full
    //     click pipeline because their handler (CSWGuiInGameOptions::OnGraphics
    //     @0x006aad90, etc.) gates on `param_1->is_active != 0`. That flag is
    //     set by HandleLMouseDown but NOT by direct vtable[15] dispatch — so
    //     FireActivate on a tab silently no-ops. Verified via Ghidra
    //     decompilation. Click-sim (cursor warp + Down + Up) is the only way
    //     in.
    //
    //   * **Everything else** (sub-dialog setting buttons, OK/Cancel popups,
    //     main menu) uses direct vtable[15] activate. It bypasses hit-test, so
    //     buttons covered by overlapping listbox extents (chain targets in
    //     sub-dialogs) still fire — click-sim there resolves to the listbox
    //     instead of the button (Up=0, no dispatch).
    //
    // Debounce: refuse to queue another op if one is already pending.
    // Stops Enter typematic from queuing back-to-back activations on adjacent
    // OnUpdate frames — the only re-entry path left after deleting the
    // Tab-cycle two-step. Single user-paced presses always go through.
    //
    // Consume Enter either way so the engine doesn't ALSO fire F1 against
    // panel.activeControl (which can be stale or wrong).
    //
    // Lazy rebind: the Up/Down handler below rebinds the chain when the
    // active panel changes, but Enter previously required a prior arrow
    // press. That stranded engine-pushed modals that the user reasonably
    // tries to dismiss with Enter alone (StatusSummary after a skill
    // check, the post-quit-confirm MessageBox, an AreaTransition prompt):
    // chain stayed bound to the previous panel, the gate below failed,
    // and the engine's own F1 dispatch silently no-ops on those popups
    // too — total lockup. Mirroring the Up/Down rebind here lifts that
    // restriction. RebindChain anchors g_chainIndex on
    // panel.activeControl when present, so popups the engine pre-focuses
    // (quit-confirm pre-focused on Abbrechen) activate the focused
    // button on Enter without arrow-key priming.
    if (param_2 != 0 &&
        (param_1 == kInputEnter1 || param_1 == kInputEnter2) &&
        activePanel != nullptr &&
        g_chainPanel != activePanel)
    {
        RebindChain(activePanel);
    }
    if (param_2 != 0 &&
        (param_1 == kInputEnter1 || param_1 == kInputEnter2) &&
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
        g_chainCount > 0 &&
        g_chainIndex < g_chainCount)
    {
        ChainEntry& e = g_chain[g_chainIndex];

        bool isTabButton = false;
        if (g_tabbedPanel && g_tabsCount >= 2) {
            auto* tlist = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
            if (tlist && tlist->data) {
                for (int i = g_tabsStart;
                     i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
                    if (tlist->data[i] == e.control) { isTabButton = true; break; }
                }
            }
        }

        // Detect equip-screen slot buttons up front. They need the full click
        // pipeline (cursor warp + LMouseDown/Up) to fire the engine's
        // OnSelectSlot — which is what populates LB_ITEMS with items matching
        // the slot. Direct vtable[15] activate on a slot button routes to a
        // different handler (likely OnEnterSlot, the keyboard shortcut path)
        // that pops a "no items" modal instead of populating the picker. Same
        // gate-mismatch shape as Options tab buttons: the mouse path is the
        // only one that triggers the populate.
        bool isEquipSlot = false;
        int equipSlotCid = 0;
        if (IdentifyPanel(g_chainPanel) == PanelKind::InGameEquip) {
            equipSlotCid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(e.control) + 0x50);
            isEquipSlot =
                equipSlotCid == kEquipBtnHeadId    || equipSlotCid == kEquipBtnImplantId ||
                equipSlotCid == kEquipBtnBodyId    || equipSlotCid == kEquipBtnArmLId    ||
                equipSlotCid == kEquipBtnArmRId    || equipSlotCid == kEquipBtnWeapLId   ||
                equipSlotCid == kEquipBtnWeapRId   || equipSlotCid == kEquipBtnBeltId    ||
                equipSlotCid == kEquipBtnHandsId;
        }

        if (acc::menus::pending::IsPending()) {
            acclog::Write("Enter", "op already pending; ignoring (target=%p)", e.control);
            consumed = true;
        } else if (e.textOnly) {
            // Modal body text — non-activatable. Re-speak so a user who
            // missed the open-time announce can hear it again. Don't fire
            // vtable[15] (the listbox has no activate handler).
            AnnounceControl(e.control);
            acclog::Write("Menus.Enter", "re-announce panel=%p index=%d target=%p (text-only)",
                          activePanel, g_chainIndex, e.control);
            consumed = true;
        } else if (isTabButton) {
            int cursorY = e.cy;
            if (g_tabClickOffsetY > 0) cursorY += g_tabClickOffsetY;
            acc::menus::pending::QueueClickAt(e.cx, cursorY, e.control);
            g_navSpeechSuppressBudget = 2;  // see chain-step doc above
            acclog::Write("Menus.Enter", "click-sim panel=%p index=%d target=%p cursorY=%d (tab)",
                          activePanel, g_chainIndex, e.control, cursorY);
            consumed = true;
        } else if (isEquipSlot) {
            // Bypass click-sim entirely. Calling OnEnterSlot then
            // OnSelectSlot directly invokes the same engine path that
            // mouse-driven hover+click does, but without depending on
            // hit-test landing on the slot button (the labels cover the
            // buttons in z-order — see docs/equip-flow-investigation.md
            // for the hit-test data). Deferred to OnUpdate to stay clear
            // of mid-input-dispatch recursion.
            acc::menus::pending::QueueEquipSelect(g_chainPanel, e.control);
            g_navSpeechSuppressBudget  = 2;
            // Arm the picker zone now: OnSelectSlot raises field33_0x4270 |= 1
            // and the user proceeds to LB_ITEMS browsing. Self-clears on
            // panel close, picker Esc, or BTN_EQUIP dispatch.
            acc::menus::listbox::ArmEquipPicker(g_chainPanel);
            acclog::Write("EquipPicker", "armed via direct OnEnterSlot+OnSelectSlot "
                          "(Enter on slot id=%d btn=%p panel=%p)",
                          equipSlotCid, e.control, g_chainPanel);
            consumed = true;
        } else {
            acc::menus::pending::QueueActivate(e.control);
            g_navSpeechSuppressBudget = 2;  // see chain-step doc above

            // Arm the drill flag when Enter activates an icon on the InGameMenu
            // strip. The engine's activation path (FireActivate → button
            // onClick → SwitchToSWInGameGui) pushes the sub-screen to back; on
            // the next arrow press, the drill router will retarget the chain
            // to it. Set EAGERLY: if the engine no-ops the activation (e.g.
            // re-press of the same icon, GUI_id unchanged), the override
            // self-clears via FindActiveSubScreenPanel-returns-null on the
            // next route.
            if (IdentifyPanel(g_chainPanel) == PanelKind::InGameMenu) {
                g_drilledIntoSubScreen = true;
                acclog::Write("Drill", "armed (Enter on InGameMenu icon target=%p)",
                              e.control);
            }

            acclog::Write("Menus.Enter", "activate panel=%p index=%d target=%p",
                          activePanel, g_chainIndex, e.control);
            consumed = true;
        }
    }

    // Tab key falls through to the engine. We previously implemented a
    // Tab-cycle two-step here (FireActivate(Schliess) → schedule click-sim on
    // next tab), but it crashed: compressing close-old-subdialog + open-new
    // into a single ~16ms window pressured the GL driver (Grafik specifically
    // re-runs gamma-ramp via OnMoveGammaSlider in its constructor) until the
    // NVIDIA driver fast-failed mid-frame. See docs/tab-crash-investigation.md.
    //
    // Replacement UX: arrow keys walk tab buttons via the chain (they're
    // already in panel.controls of the tabbed parent), Enter opens a tab via
    // click-sim (above), Esc closes a sub-dialog via FireActivate(Schliess)
    // (handler below) — keeping every action user-paced and never collapsing
    // close+open into the same frame.

    // Arrow keys: flat chain navigation. Chain is built from panel.controls
    // + listbox children (one level) sorted by extent.top, so arrow-down
    // walks visually top-to-bottom through every navigable button — including
    // tab buttons on the parent Options panel and settings that live as
    // button children of a CSWGuiListBox in sub-dialogs.
    if (param_2 != 0 &&
        (param_1 == kInputNavUp || param_1 == kInputNavDown) &&
        activePanel != nullptr)
    {
        if (activePanel != g_chainPanel) {
            RebindChain(activePanel);
        }
        if (g_chainCount == 0) {
            // Foreground panel has no navigable controls. Log so we can see
            // which panels are routing-only (e.g. the recurring 074FE618
            // overlay and the dialog routing target 0FDEE418 observed in
            // the in-game session) and decide whether to add a fallback
            // strategy (walk down the modal stack to the next chain-eligible
            // panel, or surface the panel's content via the title/listbox
            // path). For now: log only, leave the input unconsumed so the
            // engine sees it.
            PanelKind emptyKind = IdentifyPanel(activePanel);
            acclog::Write("Menus.Chain", "empty panel=%p kind=%s has no navigable "
                          "controls; input not consumed",
                          activePanel, PanelKindName(emptyKind));

            // Walk the panel ONCE so we can see what's actually in it.
            // OnSetActiveControl's panel-walk gate (s_lastPanel) doesn't
            // fire on these panels because the engine never sets focus on
            // them. Without a walk we never learn their structure — log-only
            // diagnostics give us nothing actionable.
            static void* s_walkedEmptyPanels[16];
            static int   s_walkedEmptyCount = 0;
            bool walked = false;
            for (int i = 0; i < s_walkedEmptyCount; ++i) {
                if (s_walkedEmptyPanels[i] == activePanel) { walked = true; break; }
            }
            if (!walked && s_walkedEmptyCount < 16) {
                s_walkedEmptyPanels[s_walkedEmptyCount++] = activePanel;
                acclog::Write("Menus.EmptyChain", "walk panel=%p kind=%s",
                              activePanel, PanelKindName(emptyKind));
                WalkChildren("Menus.EmptyChain", activePanel,
                             kPanelControlsOffset);
            }
        }
        if (g_chainCount > 0) {
            int delta = (param_1 == kInputNavDown) ? +1 : -1;
            int newIndex = g_chainIndex + delta;
            if (newIndex < 0)              newIndex = 0;
            if (newIndex >= g_chainCount)  newIndex = g_chainCount - 1;
            g_chainIndex = newIndex;

            ChainEntry& e = g_chain[g_chainIndex];
            // For chargen class icons AnnounceControl bails silently when
            // the cache for this icon hasn't been populated yet (first-time
            // visit). On revisits the cache hits and speech fires
            // immediately, sidestepping the OnEnterButton race that would
            // otherwise speak the previous icon's class. See
            // ExtractAnnounceableText step 9c and the OnSetActiveControl
            // prefill path.
            AnnounceControl(e.control);
            // Mirror chain focus into the chargen Attributes panel's
            // selected_ability so the next Left/Right press routes
            // OnPlusButton / OnMinusButton to the focused ability rather
            // than the default top row (STR). No-op on every other panel.
            acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus();
            // Chargen Attribute panel: speak the per-row info suffix
            // ("Modifikator -1, Preis 1") synchronously, right after
            // the regular "Stärke, 8" announce. Computes the modifier
            // and cost from the focused button's value rather than
            // reading the engine's labels — see menus_chargen_attr.h
            // for the timing/rendering reasons. No-op on every other
            // panel.
            acc::menus::chargen_attr::AnnounceChainStepSuffix(
                g_chainPanel, e.control);
            int cursorX = e.cx;
            int cursorY = e.cy;
            if (!e.textOnly) {
                // Cursor warp + suppress-budget exist to make hover-to-focus
                // work for activatable controls. Text-only entries (modal
                // body listboxes) have no hover semantics worth chasing —
                // skipping keeps the cursor stable on whatever button the
                // user just left, and avoids spurious engine-side
                // SetActiveControl echoes from the listbox under the cursor.
                if (IsTabButton(e.control) && g_tabClickOffsetY > 0) {
                    cursorY += g_tabClickOffsetY;
                }
                if (IsClassSelectionIcon(g_chainPanel, e.control) &&
                    g_classIconClickOffsetX > 0) {
                    cursorX += g_classIconClickOffsetX;
                }
                // Chargen Attribute panel: same hit-test-shifts-up-one-row
                // problem as Options tabs. Without this the cursor lands
                // on the row above and the engine's OnEnterPointsButton
                // populates description_listbox for the wrong ability.
                {
                    int abilityPitch =
                        acc::menus::chargen_attr::RowPitchForCursorWarp(
                            g_chainPanel, e.control);
                    if (abilityPitch > 0) cursorY += abilityPitch;
                }
                acc::menus::pending::QueueMoveCursor(cursorX, cursorY, e.control);
                // Suppress the next two SetActiveControl announces — engine-
                // side focus echoes that fire after the keypress + cursor
                // warp would otherwise read out the wrong sibling control as
                // an "afterthought" after we already announced the chain
                // target above.
                g_navSpeechSuppressBudget = 2;
            }
            acclog::Write("Menus.Chain", "step panel=%p index=%d/%d target=%p center=(%d,%d) cursor=(%d,%d)%s %s",
                          g_chainPanel, g_chainIndex, g_chainCount,
                          e.control, e.cx, e.cy, cursorX, cursorY,
                          e.textOnly ? " text-only" : "",
                          param_1 == kInputNavDown ? "DOWN" : "UP");
            // Always consume nav-up/nav-down on a panel with a non-empty chain.
            consumed = true;
        }
    }

    // Left/Right dispatch. Two cases:
    //
    //   1. Focused control is a slider — queue a slider HandleInputEvent
    //      with logical inc/dec code (500 / 501). Engine's slider runs the
    //      full pipeline: SetCurValue + bounds clamp + gui_object callback
    //      (audio volume change for Music/Voice/SFX/Movie) + PlayGuiSound.
    //      Letting the keypress pass through to the engine doesn't work
    //      because panel.activeControl isn't set to the slider (chain
    //      navigation only updates mouseOverControl); the engine's natural
    //      dispatch would route Left/Right to whichever previous control was
    //      activeControl, not the slider the user navigated to.
    //
    //   2. Focused control is anything else — find an empty-text navigable
    //      neighbour at the same y-row in panel.controls and fire-activate
    //      it (cycle-arrow flanker). Engine rewrites the value-display
    //      button's CExoString in place. Per-frame monitor catches both
    //      cases on the next tick and re-announces.
    //
    // Both cases consume the keypress so we don't surface unspecified
    // native behaviour from Left/Right on widgets where it has no
    // user-meaningful effect.
    if (param_2 != 0 &&
        (param_1 == kInputNavLeft || param_1 == kInputNavRight) &&
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
        g_chainCount > 0 &&
        g_chainIndex >= 0 &&
        g_chainIndex < g_chainCount)
    {
        void* focused = g_chain[g_chainIndex].control;
        bool toRight = (param_1 == kInputNavRight);

        if (IsSlider(focused)) {
            if (acc::menus::pending::IsPending()) {
                acclog::Write("Menus.Slider", "%s: op already pending; ignoring",
                              toRight ? "right" : "left");
            } else {
                int code = toRight ? 500 : 501;
                acc::menus::pending::QueueSliderInput(focused, code);
                acclog::Write("Menus.Slider", "%s panel=%p focus=%p code=%d",
                              toRight ? "right" : "left",
                              activePanel, focused, code);
            }
        } else {
            // Panel-aware cycle override: in CSWGuiPortraitCharGen the
            // chain holds left_arrow as the lone anchor (right_arrow is
            // filtered out in RebindChain). FindAdjacentArrow can pick up
            // the right_arrow as a same-row neighbour when going right,
            // but going left there's nothing to the left of x=272 — so we
            // resolve the targets directly from the panel offsets:
            //   Left  → activate left_arrow (cycles -1)
            //   Right → activate right_arrow (cycles +1)
            // Engine's UpdatePortraitButton writes the new resref to
            // creature.portrait, the per-frame focus monitor re-reads, and
            // the diff fires the new "Porträt: …" announcement.
            void* portraitTarget = nullptr;
            {
                void** pVt = *reinterpret_cast<void***>(activePanel);
                if (reinterpret_cast<uintptr_t>(pVt) ==
                        kVtableCSWGuiPortraitCharGen) {
                    auto* base = reinterpret_cast<unsigned char*>(activePanel);
                    void* leftArrow  = base + kPortraitLeftArrowOffset;
                    if (focused == leftArrow) {
                        portraitTarget = toRight
                            ? (void*)(base + kPortraitRightArrowOffset)
                            : leftArrow;
                    }
                }
            }
            void* neighbor = portraitTarget
                ? portraitTarget
                : FindAdjacentArrow(activePanel, focused, toRight);
            if (neighbor) {
                if (acc::menus::pending::IsPending()) {
                    acclog::Write("Menus.Cycle", "%s: op already pending; ignoring",
                                  toRight ? "right" : "left");
                } else {
                    acc::menus::pending::QueueActivate(neighbor);
                    acclog::Write("Menus.Cycle", "%s panel=%p focus=%p neighbor=%p%s",
                                  toRight ? "right" : "left",
                                  activePanel, focused, neighbor,
                                  portraitTarget ? " (portrait-anchor)" : "");
                }
            } else {
                acclog::Write("Menus.Cycle", "%s: no adjacent arrow for focus=%p",
                              toRight ? "right" : "left", focused);
            }
        }
        consumed = true;
    }

    // Esc in drill mode: return chain to the strip without closing the
    // sub-screen. User flow:
    //   1. On strip, Enter on Inventory → engine pushes InGameInventory,
    //      strip stays fg, drill arms, chain retargets to inventory listbox.
    //   2. User navigates inventory items.
    //   3. Esc → drill clears, next arrow press rebinds chain to strip.
    //   4. From strip, Right-arrow + Enter to switch to a different sub-screen.
    //
    // We deliberately don't fire the sub-screen's exit_button here — leaving
    // the sub-screen alive in panels[] means re-pressing Enter on the same
    // icon is a cheap re-drill (no tutorial replay, no engine-side teardown).
    // Closing the screen is what the sub-screen's own exit_button is for; the
    // user can navigate to it explicitly while drilled.
    //
    // Routes BEFORE the tabbed-panel Esc handler below: drilled mode is the
    // outer state, sub-tab close is the inner. If both could match (drilled
    // into Options with a sub-tab open), close the sub-tab first via the
    // existing handler — drill stays armed because activePanel is still
    // a sub-tab modal at that point, not the strip.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        g_drilledIntoSubScreen &&
        activePanel != nullptr &&
        IdentifyPanel(activePanel) != PanelKind::InGameMenu)
    {
        // Only fire when activePanel is the sub-screen itself, not a sub-tab
        // or modal sitting on top of it. Tabbed-Esc handler below will close
        // sub-tabs first; once activePanel resolves back to the sub-screen,
        // the next Esc lands here and clears the drill.
        PanelKind apk = IdentifyPanel(activePanel);
        if (acc::menus::monitors::IsInGameSubScreenKind(apk)) {
            g_drilledIntoSubScreen = false;
            acclog::Write("Drill", "Esc -> back to strip (sub-screen panel=%p "
                          "kind=%s left in panels[])",
                          activePanel, PanelKindName(apk));
            consumed = true;
        }
    }

    // Esc / Backspace (when bound to "back/cancel" via the in-game Key Mapping
    // screen): close the current sub-dialog by FireActivate-ing its Schliess
    // button. The engine's natural Esc → CSWGuiOptionsXxx::HandleInputEvent(0x28)
    // → PopModalPanel path is silently failing in our environment (Esc reaches
    // the manager and translates correctly, but no close fires — verified in
    // patch-20260502-102803.log lines 311-312). FireActivate(Schliess) is the
    // same primitive that already works when the user manually navigates to
    // Schliess and presses Enter, so routing Esc through it gives deterministic
    // close behavior.
    //
    // Gate covers two kinds of activePanel:
    //   1. A sub-dialog of a tabbed parent (e.g. Options' Auto-Pause /
    //      Grafik / Sound / … sub-screens). Original use case;
    //      `activePanel != g_tabbedPanel` keeps Esc on the parent Options
    //      panel itself passing through to the engine, which then opens
    //      the "Möchtest du wirklich aufhören?" quit confirmation —
    //      desired existing behavior.
    //   2. A standalone modal popup pushed by the engine — IsModalPopupPanel
    //      lists the kinds. Without this branch, Esc on a StatusSummary or
    //      a MessageBoxModal does nothing (engine's own Esc handling on
    //      these is to open the quit-confirm sibling, which is also a
    //      MessageBoxModal — leaving the user stacked deeper instead of
    //      backing out). FindCloseButton resolves to Schliess/OK/Weiter
    //      reliably for these panels (verified for StatusSummary in
    //      patch-20260506-073143.log); same primitive that already works
    //      for sub-dialog Esc.
    //
    // We use activePanel (resolved from the manager's modal_stack/panels[]
    // at the top of this function) rather than g_currentPanel. The latter is
    // set by SetActiveControl and never cleared on panel pop, so once a
    // sub-dialog closes, g_currentPanel keeps pointing at the dead panel
    // until a new one takes focus — and Esc would keep firing FireActivate
    // against the popped panel. activePanel always reflects the current
    // foreground per the manager.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        activePanel != nullptr &&
        ((g_tabbedPanel != nullptr && activePanel != g_tabbedPanel) ||
         IsModalPopupPanel(IdentifyPanel(activePanel))))
    {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Esc", "op already pending; ignoring");
            consumed = true;
        } else {
            // Probe order matters: confirm-style popups (OK + Abbrechen,
            // Yes + No, …) carry BOTH a cancel-intent button AND the
            // affirmative that FindCloseButton matches as "OK". Esc is a
            // back-out gesture, never a confirm — try Abbrechen/Cancel
            // first so the quit-confirm and save-overwrite-style dialogs
            // route Esc to the safe choice. Single-button info popups
            // (StatusSummary's lone Schliess, AreaTransition's Weiter)
            // have no cancel button, so the FindCloseButton fallback
            // handles them.
            void* cancelBtn = FindCancelButton(activePanel);
            void* tgt       = cancelBtn ? cancelBtn : FindCloseButton(activePanel);
            if (tgt) {
                acc::menus::pending::QueueActivate(tgt);
                acclog::Write("Menus.Esc", "%s panel=%p kind=%s target=%p",
                              cancelBtn ? "cancel" : "close",
                              activePanel,
                              PanelKindName(IdentifyPanel(activePanel)),
                              tgt);
                consumed = true;
            } else {
                acclog::Write("Menus.Esc", "sub-dialog panel=%p kind=%s but no "
                              "cancel/close button found; passing through",
                              activePanel, PanelKindName(IdentifyPanel(activePanel)));
            }
        }
    }

    int translated = acc::engine::ManagerTranslateCode(param_1);
    const char* tag = consumed ? " CONSUMED" : "";
    if (translated != param_1) {
        acclog::Write("Menus.Input", "#%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                      n, thisPtr, param_1,
                      acc::engine::InputIndexName(translated), translated, param_2, tag);
    } else {
        acclog::Write("Menus.Input", "#%d this=%p key=%s(%d) val=%d%s",
                      n, thisPtr, acc::engine::InputIndexName(param_1), param_1, param_2, tag);
    }
    return consumed ? 1 : 0;
}

// MonitorFocusedControl + MonitorPanelContents + their helpers (content
// fingerprint, sub-screen tracking, segment-diff speech) moved to
// menus_monitors.cpp post-Step-5. menus.cpp's drill router calls
// acc::menus::monitors::FindActiveSubScreenPanel /
// IsInGameSubScreenKind through the public surface.

// Find the first CSWGuiListBox child in a panel's controls. Returns
// nullptr if none. CSWGuiDialog::replies_listbox is at child[1] in
// observed panels (preceded by the message_label at child[0]); first-
// match on IsListBox is robust enough for the dialog case.
void* acc::menus::detail::FindListBoxChild(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 32 ? 32 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (c && IsListBox(c)) return c;
    }
    return nullptr;
}

// MonitorDialogReplies, MonitorContainerSelection,
// MonitorEquipPickerSelection, PollContainerGiveModeKey, and their per-
// monitor state structs moved out post-Step-5. Dialog-reply monitor lives
// in menus_monitors.cpp; the three subsystem-paired monitors (Container,
// EquipPicker, give-mode key poll) co-locate with their listbox spec
// entries in menus_listbox.cpp.

// Step 1 of the menus.cpp refactor (mod-wide tick dispatcher split):
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
}

void TickMonitors() {
    // Post-Step-5 cleanup: monitors split across two TUs. General monitors
    // (focus / panel-contents / dialog-reply) live in menus_monitors.cpp;
    // listbox-paired monitors (Container, EquipPicker, give-mode key poll)
    // co-locate with their spec entries in menus_listbox.cpp.
    acc::menus::monitors::TickGeneralMonitors();
    acc::menus::listbox::TickListboxMonitors();
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

}  // namespace acc::menus

// Read a snapshot of the listbox's cursor / flags / size into a string. Shared
// between the click and key handlers so all listbox events log the same fields.
static void DumpListBoxState(void* listBox, char* out, size_t outSize) {
    if (!listBox) {
        snprintf(out, outSize, "list=NULL");
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(listBox);
    short selIdx       = *reinterpret_cast<short*>(base + kListBoxSelectionIndexOffset);
    short topVisible   = *reinterpret_cast<short*>(base + kListBoxTopVisibleIndexOffset);
    short itemsPerPage = *reinterpret_cast<short*>(base + kListBoxItemsPerPageOffset);
    uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(base + kListBoxBitFlagsOffset);
    auto* ctrls        = reinterpret_cast<CExoArrayList*>(base + kListBoxControlsOffset);
    int ctrlsSize      = ctrls ? ctrls->size : -1;
    snprintf(out, outSize,
             "list=%p sel=%d top=%d perPage=%d size=%d flags=0x%x",
             listBox, selIdx, topVisible, itemsPerPage, ctrlsSize, bitFlags);
}

// CSWGuiListBox::HandleLMouseDown — entry hook @0x0041c4a0. Click press.
extern "C" void __cdecl OnListBoxLMouseDown(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("Menus.ListBox", "LMouseDown #%d %s", n, state);
}

// CSWGuiListBox::HandleLMouseUp — entry hook @0x0041a700. Click release; this
// is where the click action commits and the row's callback fires. Pair with
// the next OnListBoxSetSelectedControl / OnListBoxSetActiveControl events to
// see the full chain.
extern "C" void __cdecl OnListBoxLMouseUp(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("Menus.ListBox", "LMouseUp #%d %s", n, state);
}

// CSWGuiListBox::HandleInputEvent — entry hook @0x0041ce20. Per-listbox key
// dispatch. Fires only when the listbox is the focused control AND the
// engine routes the key down to it. We don't extract param_1/param_2 here
// (would need stack-source path which is broken upstream) — correlate by
// timestamp with the manager-level HandleInputEvent log line that fired
// just before.
extern "C" void __cdecl OnListBoxHandleInput(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("Menus.ListBox", "HandleInputEvent #%d %s", n, state);
}

// CSWGuiListBox::SetSelectedControl — entry hook @0x0041c040. Fires whenever
// the listbox's selection index changes, regardless of source (keyboard, mouse,
// programmatic). Reads the OLD selection_index pre-update; the next
// OnListBoxSetActiveControl event will reveal the new value.
extern "C" void __cdecl OnListBoxSetSelectedControl(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("Menus.ListBox", "SetSelected #%d %s (pre-update)", n, state);
}

// CServerExoApp::SetMoveToModuleString — entry hook @0x004aecd0. Fires once
// per area transition with the destination module's resref CExoString*. Pre-
// load announce path; reads the resref via the dedup-and-speak helper in
// transitions.cpp.
//
// **LEA-vs-MOV bug workaround** (memory: project_kpatchmanager_lea_bug.md).
// The wrapper emits LEA (not MOV) for `source = "esp+4"` parameters, so
// `arg_addr` is the *address* of the original [esp+4] stack slot, not the
// CExoString* value at that slot. Dereference once to get the actual
// CExoString*. SEH-guarded — if the wrapper hands us a bogus address (e.g.
// stack frame mid-teardown), absorb the fault rather than crashing.
//
// `serverApp` (ECX = CServerExoApp* facade) is unused; the handler only
// needs the destination string. Kept in the signature so the parameter
// order matches the hooks.toml declaration.
extern "C" void __cdecl OnSetMoveToModuleString(void* /*serverApp*/,
                                                void* arg_addr) {
    EnsureTolkInitialized();

    void* exoStringPtr = nullptr;
    __try {
        exoStringPtr = *reinterpret_cast<void**>(arg_addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Transition", "pre-load arg deref faulted (arg_addr=%p)",
            arg_addr);
        return;
    }

    acc::transitions::AnnouncePreLoadDestination(exoStringPtr);
}

// DllMain + OnRulesInit + EnsureTolkInitialized live in core_dllmain.cpp.
