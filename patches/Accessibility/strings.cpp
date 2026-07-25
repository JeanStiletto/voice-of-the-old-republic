#include "strings.h"

namespace acc::strings {

namespace {
// German default per user direction at Phase 2 lay-off 4 wiring. Phase 7
// (user options UI, deferred) will surface a runtime toggle; for now,
// SetLanguage is the only switch.
Lang g_lang = Lang::De;
}  // namespace

void SetLanguage(Lang l) { g_lang = l; }
Lang GetLanguage()       { return g_lang; }

const char* Get(Id id) {
    switch (g_lang) {
        case Lang::En: return lang_en::Get(id);
        case Lang::De: return lang_de::Get(id);
        case Lang::Fr: return lang_fr::Get(id);
        case Lang::It: return lang_it::Get(id);
        case Lang::Es: return lang_es::Get(id);
        case Lang::Ru: return lang_ru::Get(id);
    }
    return "";
}

unsigned CodepageFor(Lang l) {
    switch (l) {
        // Western European — the byte escapes in these tables are Windows-1252,
        // which is also CP_ACP on their users' systems.
        case Lang::En:
        case Lang::De:
        case Lang::Fr:
        case Lang::It:
        case Lang::Es: return 1252;
        // Cyrillic. Must be pinned: a Russian KOTOR install is commonly run on
        // a non-Russian Windows, where CP_ACP would be 1252 and garble it.
        case Lang::Ru: return 1251;
    }
    return 1252;
}

}  // namespace acc::strings
