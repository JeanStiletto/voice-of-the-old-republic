#include "map_note_renames.h"

#include <cstdio>
#include <cstring>

#include "engine_area.h"   // GetCurrentAreaResName
#include "engine_reads.h"  // LookupTlk
#include "strings.h"

namespace acc::map_note_renames {

namespace {

// One curated replacement. destAreaStrref != 0 speaks the destination
// area's tlk name (planet prefix stripped); otherwise stringId supplies a
// mod-localised label. hasAnchor marks same-(module,strref) twins whose
// meaning depends on WHICH note it is — resolution picks the entry whose
// anchor lies nearest the note's authored position.
struct Rename {
    const char*      module;          // module resref, lowercase
    uint32_t         strref;          // vanilla note strref
    bool             hasAnchor;
    float            anchorX, anchorY;
    uint32_t         destAreaStrref;  // 0 -> use stringId
    acc::strings::Id stringId;
};

// Destination-area name strrefs (dialog.tlk): 7470 "Dantooine - Hof",
// 7851 "- Matale-Anwesen", 8000 "- Geh\xF6lz", 8724 "- Sandral-Anwesen".
// Note strrefs: 33413 "S\xFCdlicher Pfad", 33425 "Nordpfad", 33461
// "Ausgang". Each note sits 2-11m from exactly one transition trigger in
// the module data, so the destination is unambiguous; danm14aa's two
// 33413 notes both lead to danm14ab (west + east path) and share one
// entry — the cycle's position ordinal then numbers them 1/2.
constexpr acc::strings::Id kNoString = acc::strings::Id::Count_;
const Rename kRenames[] = {
    { "danm14aa", 33413, false, 0.0f,  0.0f,  7851, kNoString },
    { "danm14ab", 33425, false, 0.0f,  0.0f,  7470, kNoString },
    { "danm14ab", 33413, false, 0.0f,  0.0f,  8000, kNoString },
    { "danm14ac", 33425, false, 0.0f,  0.0f,  7851, kNoString },
    { "danm14ac", 33413, false, 0.0f,  0.0f,  8724, kNoString },
    { "danm14ad", 33425, false, 0.0f,  0.0f,  8000, kNoString },
    { "danm14ae", 33461, false, 0.0f,  0.0f,  8724, kNoString },
    { "danm15",   33461, false, 0.0f,  0.0f,  7470, kNoString },
    // Sandral estate interior: both exits carry 33461 "Ausgang" and lead
    // to danm14ad through different doors. Front (dan16_door03, links to
    // the marked outdoor entrance) vs back (dan16_door04, the estate
    // backdoor used in the feud quest) — anchors are the notes' authored
    // positions.
    { "danm16",   33461, true,  45.3f, 30.6f, 0, acc::strings::Id::MapNoteFrontExit },
    { "danm16",   33461, true,  19.6f, 63.6f, 0, acc::strings::Id::MapNoteBackExit },
};
constexpr int kRenameCount = sizeof(kRenames) / sizeof(kRenames[0]);

// Area names follow the "{Planet} - {Area}" pattern in every official
// localisation; the cycle already speaks them inside one planet, so the
// prefix is dropped when present. A translation without the separator
// speaks the full string — never empty.
void StripPlanetPrefix(char* buf) {
    const char* sep = std::strstr(buf, " - ");
    if (!sep || sep[3] == '\0') return;
    std::memmove(buf, sep + 3, std::strlen(sep + 3) + 1);
}

}  // namespace

bool Override(uint32_t strref, const Vector& notePos,
              char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize < 2) return false;

    // Cheap pre-pass: bail before the module-name walk when the strref
    // isn't curated at all (the common case for every other area).
    bool candidate = false;
    for (int i = 0; i < kRenameCount; ++i) {
        if (kRenames[i].strref == strref) { candidate = true; break; }
    }
    if (!candidate) return false;

    char module[64] = "";
    if (!acc::engine::GetCurrentAreaResName(module, sizeof(module))) {
        return false;
    }

    const Rename* best = nullptr;
    float bestDist2 = 0.0f;
    for (int i = 0; i < kRenameCount; ++i) {
        const Rename& r = kRenames[i];
        if (r.strref != strref || _stricmp(r.module, module) != 0) continue;
        if (!r.hasAnchor) { best = &r; break; }  // unique per module
        float dx = notePos.x - r.anchorX;
        float dy = notePos.y - r.anchorY;
        float d2 = dx * dx + dy * dy;
        if (!best || d2 < bestDist2) { best = &r; bestDist2 = d2; }
    }
    if (!best) return false;

    if (best->destAreaStrref != 0) {
        if (!acc::engine::LookupTlk(best->destAreaStrref, outBuf, bufSize) ||
            outBuf[0] == '\0') {
            return false;  // caller falls back to the vanilla note text
        }
        StripPlanetPrefix(outBuf);
        return outBuf[0] != '\0';
    }
    std::snprintf(outBuf, bufSize, "%s", acc::strings::Get(best->stringId));
    return outBuf[0] != '\0';
}

}  // namespace acc::map_note_renames
