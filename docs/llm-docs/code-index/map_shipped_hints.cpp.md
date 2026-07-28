# map_shipped_hints.cpp (76 lines)

Static table of curated world-position hints for story spots the game never
marks (Sandral estate backdoor, Promised-Land rebel corpses in the Taris
sewers, the bantha grazing band). Positions sourced from module .git data.
Rows are identified by exact table-address (`AsTableRow`), and validity also
requires the current module to match (`ModuleMatches`) — a stamp from a
previous area invalidates automatically. No engine object backs these; they
are folded into the map hint cycle listing fog-of-war-gated exactly like
vanilla map notes, but never materialised as a CSWCMapPin.

## Declarations (in source order)

- L21 — `const ShippedHint kHints[]`
  note: danm14ad backdoor, two tar_m05aa rebel-corpse containers, tat_m18ac bantha-herd centroid; positions verified against K1CP-patched .mods 2026-07-27
- L29 — `bool ModuleMatches(const char* module)`
- L36 — `const ShippedHint* AsTableRow(const void* p)` — row-pointer identity check
- L45 — `int CollectForCurrentModule(const ShippedHint** out, int maxOut)`
- L61 — `bool IsShippedHint(const void* p)` — table-row AND module-match (narrated_target staleness contract)
- L66 — `bool GetLabel(const void* p, char* outBuf, size_t bufSize)`
