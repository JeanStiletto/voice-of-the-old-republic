// French string table — currently aliases English for the Id::* speech
// path. The combat msg-bus parser uses its own per-locale table in
// combat_strings.cpp::kFr (engine anchors extracted from dialog_fr.tlk),
// so French players already get fully French combat announcements.
// Full FR translation of the Id::* table is deferred — when a French
// translator/tester is available, replace this alias with a real switch.
//
// See strings.h for the encoding convention and string-id semantics.

#include "strings.h"

namespace acc::strings::lang_fr {

const char* Get(Id id) {
    return lang_en::Get(id);
}

}  // namespace acc::strings::lang_fr
