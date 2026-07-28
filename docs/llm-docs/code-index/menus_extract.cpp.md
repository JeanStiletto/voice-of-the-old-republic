# menus_extract.cpp (1897 lines)

Implements `FromControl`, the control-text "announce ladder" — the single function every focus/chain-step/monitor path calls to get a control's spoken text. Tries (in order): virtual-anchor short-circuits (mod-settings root sentinel), per-kind virtual-row formatters (charsheet stat rows, credits row, equip-stat rows, pazaak deck/wager widgets, journal quest-items button, keybinding rows), tooltip, button/buttontoggle/label/labelhilight text, slider (with sibling-label category lookup), editbox, single-row listbox content, PartySelection portrait resolver, speculative vtable-override reads for known label/button subclasses that hide behind an overridden `AsLabel`/`AsButton`, then a long series of per-kind hardcoded fallbacks (InGameMenu icon strip, equip slot buttons, in-game map prev/next-note buttons, workbench upgrade slot names + occupancy, chargen class-selection icon cache, chargen portrait cycle value), and finally a spatial sibling-label fallback plus cycle-category-prefix and toggle-state/disabled-suffix appends. Heavy use of SEH (`__try`/`__except`) around raw struct reads since many of these are speculative offsets on possibly-mismatched vtables. Talks to `engine_panels`, `engine_reads`, `engine_player`, `menus_charsheet`, `menus_credits`, `menus_equipstats`, `menus_pazaakdeck`, `menus_modsettings`, `menus_internal` (seam helpers).

## Declarations (in source order)

- L51 — `typedef PFN_CSWCCreatureGetPortraitId/GetPortrait` — thiscall typedefs for the chargen portrait accessors
- L70 — `constexpr const char* kPortraitByRow[32]` — chargen portrait row → baseresref fallback table (rows 12/13 nullptr = T3/Carth, unreachable via chargen cycle)
- L92 — `struct CycleCategoryEntry { control, category[128] }`; `s_cycleCategories[16]`, `s_cycleCategoryCount` — cache of (cycle-control → category name) captured before the engine overwrites the button's CExoString with the new value
- L100 — `const char* LookupCycleCategory(void* control)` (anonymous ns)
- L129 — `bool IsCycleFlankerArrow(panel, control)` — detects image-only `[◀]`/`[▶]` buttons flanking a cycle value-display, to suppress the sibling-label fallback for them
- L200 — `const char* FindSiblingLabel(panel, control, outBuf, bufSize)` — spatial nearest-label search (same-row-left, or above/below within tolerance)
- L290 — `bool IsSoundOptionsMovieSlider(panel, control)` — fingerprints optionssound.gui by slider-id set {1,4,7,8} to fix the stock mislabelled "Video volume" slider
- L325 — `kWagerCurrentValueOffset`, `kWagerMaxLabelGuiId`; `void* FindWagerMaxLabel(panel)`; `bool ExtractWagerRow(panel, maxLabel, outBuf, bufSize)` — Pazaak wager popup virtual top-row (live wager + max + credits, newline-flattened)
- L385 — `void acc::menus::extract::ResetCycleCategoryCache()` / L389 `CaptureCycleCategory(control, category)` — upsert semantics so a panel-specific override can replace the generic capture
- L410 — `const char* acc::menus::extract::FromControl(control, outBuf, bufSize, ownerPanel)` — the announce ladder; see summary. Ends with cycle-category prefix (skipped if identical to value — capture-timing guard) and toggle-state / "disabled" suffix appends (InGameLevelUp/CharGen key on bit 3 SetEnabled instead of bit 1)
  note: section 7b (PartySelection portraits) reads `selected` at control+0x1c4 as ground truth over the stale +0x44c partyId snapshot
- L1883 — `void acc::menus::extract::ForEachWagerRowAnchor(panel, callback, userData)` — registers the Pazaak wager virtual row (sortCy=1, top of chain)
