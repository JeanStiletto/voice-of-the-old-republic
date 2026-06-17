// Single source of truth for the installed mod version. Bumped together
// with manifest.toml [patch].version on every release.
//
// Read by:
//   - core_dllmain.cpp's "loaded, version X" greeting
//   - update_checker.cpp's "no update available, you are on version X" cue
//   - update_checker.cpp's remote-vs-local version comparison

#pragma once

namespace acc {

constexpr const char* kModVersion = "0.5.4";

}  // namespace acc
