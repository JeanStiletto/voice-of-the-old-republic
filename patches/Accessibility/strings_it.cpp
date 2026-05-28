// Italian string table — currently aliases English for the Id::* speech
// path. Combat speech is fully Italian via combat_strings.cpp::kIt
// (engine anchors extracted from dialog_it.tlk). Full IT translation of
// the Id::* table is deferred — when an Italian translator/tester is
// available, replace this alias with a real switch.
//
// See strings.h for the encoding convention and string-id semantics.

#include "strings.h"

namespace acc::strings::lang_it {

const char* Get(Id id) {
    return lang_en::Get(id);
}

}  // namespace acc::strings::lang_it
