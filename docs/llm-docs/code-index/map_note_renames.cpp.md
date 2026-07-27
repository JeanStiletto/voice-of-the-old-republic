# map_note_renames.cpp (110 lines)

Speech-only override table for vanilla K1 map notes that reuse identical
corridor-style text ("Südlicher Pfad", "Nordpfad", "Ausgang") across multiple
Dantooine modules — sighted players disambiguate visually, the spoken cycle
can't. Each curated `Rename` row maps (module, strref) to either the
destination area's own TLK name (planet-prefix stripped) or a mod-localised
label; same-(module,strref) twins (danm16's two exits) are disambiguated by
nearest-anchor-position. `Override()` does a cheap strref pre-pass before
walking the module name, so non-curated areas pay almost nothing. Talks to
`engine_area::GetCurrentAreaResName` and `engine_reads::LookupTlk`.

## Declarations (in source order)

- L19 — `struct Rename { const char* module; uint32_t strref; bool hasAnchor; float anchorX, anchorY; uint32_t destAreaStrref; acc::strings::Id stringId; }`
- L35 — `constexpr acc::strings::Id kNoString = acc::strings::Id::Count_`
- L36 — `const Rename kRenames[]`
  note: mapping verified against K1CP-patched .mod files 2026-07-27; danm16's two 33461 "Ausgang" entries share the strref and are told apart by authored note position (front vs back estate door)
- L59 — `void StripPlanetPrefix(char* buf)` — strips "{Planet} - " prefix when present; leaves full string otherwise (never empties it)
- L67 — `bool Override(uint32_t strref, const Vector& notePos, char* outBuf, size_t bufSize)`
  note: false on no match or failed resolution → caller keeps vanilla text, so a miss never silences a note
