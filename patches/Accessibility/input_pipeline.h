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

}  // namespace acc::input
