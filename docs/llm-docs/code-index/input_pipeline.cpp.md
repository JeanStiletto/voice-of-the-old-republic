# input_pipeline.cpp (595 lines)

Two engine detours upstream of the GUI manager: `OnProcessInput` (frame-
boundary seq tick, log line deleted for volume but the counter stays) and
`OnClientHandleInputEvent` (`CClientExoAppInternal::HandleInputEvent`,
0x00621210) — the big one. It logs every in-world event under
"Diag.ClientHIE", then does real dispatch work: puzzle-room bare-R ownership,
the modifier-space reservation (any Shift/Ctrl/Alt-held press on an
engine-bound key is swallowed so the mod's own Win32 poller owns it), the
in-world overlay Esc-consume race fix (paired with the poll-driven latch in
input_pipeline.h), bare-1..7 dispatch prep (refreshes `action_lists` against
the narrated target + re-stamps the user's last-cycled variant before the
engine's own switch-case fires), Q/E hostile-cycle re-announce deferral, and
modal arrow-key forwarding for popups the engine's own routing drops. Talks
to `unified_action_menu`, `engine_actionbar`, `engine_radial`,
`narrated_target`, `combat_queue`/`combat_diag`, `engine_keymap`, `hotkeys`.

## Declarations (in source order)

- L60 — `volatile LONG s_seq` — process-wide frame/event sequence counter
- L66 — `DWORD s_escClosedAt` / `kEscLatchWindowMs = 150` — overlay-Esc consume latch state
- L74 — `DWORD s_editboxSubmitAt` / `kEditboxSubmitLatchWindowMs = 150` — editbox-submit consume latch state
- L79 — `unsigned int NextSeq()`
- L83 — `void NoteOverlayEscClosed()` / L87 `bool ConsumeOverlayEscLatch()`
- L94 — `void NoteEditboxSubmitClosed()` / L98 `bool ConsumeEditboxSubmitLatch()`
- L116 — `void PrepareBareDispatchForNarratedTarget()`
  note: stamps the engine's target client id from narrated_target (or the invalid-object sentinel) then calls PrepareBareDispatch — shared by the bare-key path and the ModShadow-consumed Shift+1..7 path
- L148 — `extern "C" void OnProcessInput(void*)` — hook at CClientExoAppInternal::ProcessInput+0x1b, just bumps NextSeq()
- L168 — `extern "C" int OnClientHandleInputEvent(void* this_ptr, void* p1_addr, void* p2_addr)`
  note: SEH-guarded deref of stack params (esp+X emits LEA per project_kpatchmanager_lea_bug); the many gated blocks inside are, in order: puzzle bare-R consume, modifier-space reservation (mods held on non-passthrough code → consume), in-world overlay Esc consume (with suspended-menu exception + latch), bare 1..7 target-refresh + variant re-stamp (skipped during dialog), Q/E reannounce deferral (via passive_narrate::RequestQEReannounce), modal direction-key forwarding to CSWGuiManager::HandleInputEvent when modal_stack non-empty. Returns 1 to consume (POP*5+RET 8), 0 to fall through.
