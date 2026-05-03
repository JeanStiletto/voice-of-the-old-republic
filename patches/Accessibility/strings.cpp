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
    }
    return "";
}

}  // namespace acc::strings
