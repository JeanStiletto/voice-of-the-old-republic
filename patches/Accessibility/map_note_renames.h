// Curated spoken-label overrides for cryptic authored map notes.
//
// Vanilla K1 reuses corridor-style note strings across areas: dialog.tlk
// strref 33413 ("S\xFCdlicher Pfad") marks three different destinations in
// three Dantooine modules, 33425 ("Nordpfad") three more, 33461
// ("Ausgang") four. Sighted players cross-reference the map picture; the
// spoken cycle can't, so these notes get replaced with the DESTINATION
// name of the transition they mark (resolved from the destination area's
// own tlk name strref — self-localising in every game language), or with
// a mod-localised label where the destination name doesn't disambiguate
// (danm16's two exits share one strref and are told apart by position).
//
// Speech-side only — no game file is touched. Keyed by (module resref,
// strref) so the override never fires on modded notes that changed
// either; a mod's embedded note text always wins (the callers in
// engine_area consult this only when the note resolves via strref).
// Mapping data verified against the K1CP-patched .mod files 2026-07-27;
// see docs/known-issues.md "Waypoints" for the investigation.

#pragma once

#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::map_note_renames {

// True + outBuf filled when (current module, strref) has a curated
// replacement label. `notePos` disambiguates same-strref twins within one
// module (nearest anchor wins); pass the note's world position. False on
// no match or failed resolution — callers keep the vanilla text, so a
// miss can never silence a note.
bool Override(uint32_t strref, const Vector& notePos,
              char* outBuf, size_t bufSize);

}  // namespace acc::map_note_renames
