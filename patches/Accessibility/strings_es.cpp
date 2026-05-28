// Spanish string table — currently aliases English for the Id::* speech
// path. Combat speech is fully Spanish via combat_strings.cpp::kEs
// (engine anchors extracted from dialog_es.tlk). Full ES translation of
// the Id::* table is deferred — when a Spanish translator/tester is
// available, replace this alias with a real switch.
//
// See strings.h for the encoding convention and string-id semantics.

#include "strings.h"

namespace acc::strings::lang_es {

const char* Get(Id id) {
    return lang_en::Get(id);
}

}  // namespace acc::strings::lang_es
