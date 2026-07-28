# menus.cpp (2270 lines)

The core menu-accessibility TU: the two central engine hook entry points
(`OnSetActiveControl` for panel/control focus, `OnHandleInputEvent` for the
GUI manager's input dispatch), the focus-chain speech helpers, the
pending-announce slot, and the public `acc::menus` surface (`ValidatePanels`,
`TickMonitors`, `PollHomeEndKeys`, `TickPendingOps`, `DrainPendingAnnounce`).
`OnHandleInputEvent` funnels through a long ordered gate list (mod-settings
submenu, help overlay, Shift-arrow peek, Fähigkeiten handler, listbox
dispatcher, editbox, chargen Feats/Powers, cycle keys, Pazaak, galaxy map,
keymap) before falling to generic chain nav (Enter/Nav/Left-Right/Esc in
menus_chain.cpp). Talks to nearly every other menus_* TU (chargen_attr/
skills/feats, listbox, editbox, chain, monitors, pending, store, credits,
charsheet, equipstats, modsettings, journal, keymap, galaxymap, pazaak/
pazaakdeck) since it is the dispatch hub. Refactor history (Steps 1-5) split
listbox handlers, chain state, and monitors into sibling TUs; this file kept
the hook glue.

## Declarations (in source order)

- L188 — `namespace acc::menus` (s_lastSpoken dedup block)
- L191 — `char s_lastSpoken[2][256]`
  note: channel 0 = panel focus drain, channel 1 = listbox row hook
- L194 — `void MarkSpoken(int channel, const char* text)`
- L199 — `void SpeakIfChanged(int channel, const char* text)`
  note: interrupt=false — first session used interrupt=true and NVDA went silent during rapid chargen focus bursts
- L226 — `const uintptr_t kAddrPanelSetActiveControl = acc::addr::R(0x0040a630)`
- L254 — `void* g_currentPanel = nullptr`
  note: set by OnSetActiveControl; NOT reliable for input routing (use foreground panel instead) when multiple panels pre-instantiate in one frame
- L274 — `static bool g_drilledIntoSubScreen = false`
  note: armed on strip-icon Enter or auto-armed by the sub-screen monitor; retargets chain from the InGameMenu strip to the visible sub-screen
- L311-322 — `void* s_pendingAnnouncePanel/Control` / `bool s_synthesizedNav`
  note: s_synthesizedNav suppresses the press-release tracker during PollHomeEndKeys' synthesised dispatch
- L330 — `static void* g_lastTitledPanel = nullptr`
- L346 — `bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy)`
- L366 — `static bool GetListBoxRowScreenCenter(void* lb, void* row, int& outCx, int& outCy)`
  note: listbox row extents are listbox-local; adds listbox origin to translate to screen-absolute
- L391 — `bool acc::menus::detail::IsChainNavigable(void* control)`
  note: buttons/toggles/sliders only — MoveMouseToPosition's hover→active promotion crashes on labels
- L413 — `static void AnnouncePanelTitle(void* panel)`
  note: chains listbox/editbox/powers_levelup title overrides, then MainMenu DLC-notice special case (also the cold-start DirectInput reacquire + background update-check trigger point), then generic label-walk skipping short-numeric labels
- L603 — `void* acc::menus::detail::FindControlById(void* panel, int id)`
- L651 — `bool acc::menus::detail::IsSaveLoadPanel(void* panel)`
  note: matches the {0,11,12,14} .gui-ID quartet + vtable-typed button check (workbench upgrade.gui collides on IDs alone)
- L687 — `const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset)`
- L709 — `bool acc::menus::detail::DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel, ListBoxNavResult& out)`
  note: raw field write, no-wrap clamp; minSel=1 for equip-picker LB_ITEMS (row 0 is a template)
- L772 — `bool acc::menus::detail::DriveListBoxSelectionEngine(void* listbox, ListBoxNavOp op, short minSel, ListBoxNavResult& out)`
  note: same clamp logic but commits via the engine's SetSelectedControl (plays select sound)
- L843 — `bool acc::menus::detail::QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix)`
- L866 — `bool acc::menus::detail::IsClassSelectionIcon(void* panel, void* control)`
  note: positional — panel+0x6c + i*0x25c for 0<=i<6
- L891-929 — `struct ClassLabelCacheEntry` / `g_classLabelCache[8]` / `ClassLabelCacheLookup` / `ClassLabelCacheStore`
  note: first-write-wins per (panel, icon); keyed by panel too so a chargen restart doesn't leak stale entries
- L948 — `using acc::engine::IsModalPopupPanel`
- L993 — `static void PrefillClassIconCacheOnTransition(void* panel, void* newControl)`
  note: fires at SetActiveControl entry before active_control is overwritten — catches every transition, not just dwell time
- L1024 — `static void UpdateFocusedPanelState(void* panel)`
- L1035 — `static void WalkAndCaptureOnFirstSight(void* panel)`
  note: dumps every child + captures cycle-button categories once per panel pointer
- L1104 — `static void SpeakPanelTitleOnFirstSight(void* panel)`
  note: skips in-game sub-screens (handled by AnnounceNewSubScreens) and dialog panels (dialog_speech.cpp owns those)
- L1129 — `static void AnnounceNewFocusedControl(int n, void* panel, void* newControl)`
  note: suppresses tutorial-popup body, Container/Store listbox-blob dumps; cross-panel overwrite guard flushes stale pending before switching panels
- L1199 — `extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl)`
  note: hooked mid-function at CSWGuiPanel::SetActiveControl 0x0040a638; gated on movie-foreground + save/module-load suppression; InGameMenu strip speech-suppressed (architecturally invisible)
- L1321 — `extern "C" void __cdecl OnListBoxSetActiveControl(void* listBox, void* newRow, int param2)`
  note: hooked mid-function 0x0041c16b; per-row focus doesn't bubble to panel SetActiveControl, so this is the only signal for listbox row nav
- L1505 — `extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1)`
  note: demoted to log-only (fires twice per nav — old-loses/new-gains — would echo if spoken)
- L1528 — `extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2)`
  note: hooked mid-function CSWGuiManager::HandleInputEvent 0x0040c907, before the val==0 early-out; central press/release pair-consume tracker (s_lastConsumedPress) prevents double-fire when our handler consumes a press but the engine still sees the release
- L2054 — `void* acc::menus::detail::FindListBoxChild(void* panel)`
- L2086 — `namespace acc::menus` (public surface block)
- L2088 — `void ValidatePanels()`
  note: ValidateTabbedPanel + ValidateChainPanel, guards against panels the engine already freed
- L2107 — `void TickMonitors()`
  note: Store first (trade-outcome speech must land before the focus monitor's re-extract)
- L2128 — `void PollHomeEndKeys()`
  note: engine drops Home/End pre-manager-hook (no [Keymapping] action); synthesises OnHandleInputEvent re-entry
- L2164 — `void TickPendingOps()`
- L2188 — `void DrainPendingAnnounce()`
  note: multi-row listbox guard + chain-coherence drop (suppresses engine's wrong-sibling SetActive echo after a voluntary chain step); "control N" fallback deliberately bypasses SpeakIfChanged dedup
- L2255 — `void ClearPendingAnnounce()`
- L2260-2263 — `bool IsDrilledIntoSubScreen()` / `void SetDrilledIntoSubScreen(bool)`
