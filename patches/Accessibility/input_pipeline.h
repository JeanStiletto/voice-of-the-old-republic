// Engine-input pipeline — upstream-of-manager hooks on
// CClientExoAppInternal::ProcessInput / HandleInputEvent, plus a
// cross-stream sequence counter shared with the manager-side log.
//
// The upstream HandleInputEvent hook does two jobs:
//   * Logs every event the client-app sees under "Diag.ClientHIE", paired
//     with the manager-side "Menus.Input" line by the shared seq counter.
//   * Production work for bare 1..7 dispatch — refreshes action_lists
//     against the narrated target via PrepareBareDispatch + stamps the
//     user's last-cycled variant via SelectActionInRow / SelectVariant so
//     DoTargetAction / DoPersonalAction fire the chosen action, not the
//     stale engine default. See project_bare_combat_keys_dispatch.md.
//
// The ProcessInput hook is a frame-boundary seq tick only (the per-frame
// "Diag.ProcInput" log line was deleted at 60 fps because it was 99.8 %
// of log volume; the hook stays so gaps in the other streams encode
// frame boundaries).

#pragma once

namespace acc::input {

// Monotonic counter shared across the upstream input-stream log and the
// manager hook (see menus.cpp::OnHandleInputEvent). Bumped on every
// upstream / manager call so the log streams interleave with comparable
// seq values across surfaces.
unsigned int NextSeq();

// Overlay-Esc consume latch — defeats a poll-vs-event race on the Escape key.
//
// The in-world overlays (unified action menu, combat queue, examine view,
// help) close via interact_hotkey's Win32 poll, which can disarm the overlay
// BEFORE the engine's matching Esc DirectInput event reaches
// OnClientHandleInputEvent. When the poll wins the frame the consume guard
// there sees an already-inactive overlay, lets Esc fall through, and the
// engine opens its pause/Options menu (the bug: Esc closes the overlay AND
// pops Options — confirmed in patch-20260617-215141.log around 22:18:12).
//
// NoteOverlayEscClosed() stamps GetTickCount() whenever the poll routes Esc
// to close an overlay. ConsumeOverlayEscLatch() returns true (and clears the
// latch) if that stamp is within a short window, so the engine Esc is
// swallowed regardless of which pipeline ran first. Self-expiring so it can
// never swallow an unrelated later Escape.
void NoteOverlayEscClosed();
bool ConsumeOverlayEscLatch();

}  // namespace acc::input
