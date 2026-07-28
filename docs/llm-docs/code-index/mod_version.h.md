# mod_version.h (16 lines)

Single source of truth for the installed mod version string, bumped together
with `manifest.toml`'s `[patch].version` on every release. Read by
`core_dllmain.cpp`'s startup greeting and `update_checker.cpp`'s
no-update/version-compare cues.

## Declarations (in source order)

- L13 — `constexpr const char* kModVersion = "0.6.3"`
