# map_note_renames.h (37 lines)

Header for the curated map-note label overrides. Keyed by (module resref,
strref) so the override never fires on a modded note that changed either
value — a mod's embedded note text always wins since callers only consult
this when the note resolves via strref.

## Declarations (in source order)

- L33 — `bool Override(uint32_t strref, const Vector& notePos, char* outBuf, size_t bufSize)`
  note: notePos disambiguates same-strref twins within one module (nearest anchor wins); false on no match/failed resolution — vanilla text is always the fallback
