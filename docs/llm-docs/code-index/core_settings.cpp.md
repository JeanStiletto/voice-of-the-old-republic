# core_settings.cpp (16 lines)

Trivial accessor: returns a static default-constructed `NavSettings` instance. All actual defaults live as in-class member initialisers in core_settings.h, so tuning a value means editing one line there, not here. Placeholder for a future Phase 7 config-file-backed mutable version behind the same `Get()` signature.

## Declarations (in source order)

- L3 — `namespace acc::core`
- L5 — `const NavSettings& Get()`
  note: static-storage default instance; signature is locked for the eventual config-backed replacement
