# Known Issues

Status tracker for accessibility-mod work, in three buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

- **Tab content echoing.** When the tab content list is too long, the announcement starts echoing — the same line is spoken multiple times instead of just once.

## Planned

(no current entries — add here when picking up new feature work)

## Polish

- **Cycle items have English description labels.** Cycle widgets such as Difficulty announce as `"Difficulty, Leicht"` rather than `"Schwierigkeitsgrad, Leicht"` in the German build. The captured category text comes from the `.gui` file's default at panel construction time, which is English. Localized German lives in the parent Options panel's listbox-blob (per-tab) or in TLK str_refs we haven't located yet — fixing this needs either cross-panel blob lookup or a hooked panel-init function that captures the text before the engine's localization step replaces it.
