#include "core_settings.h"

namespace acc::core {

const NavSettings& Get() {
    // Static-storage default-constructed instance. The struct's in-class
    // member initializers in core_settings.h are the locked plan defaults;
    // changing a default = editing one line there.
    //
    // Phase 7 (deferred) replaces this with config-file-backed mutable
    // state; Get()'s signature stays put so consumers don't change.
    static const NavSettings kInstance;
    return kInstance;
}

}  // namespace acc::core
