#include "menu_speak.h"

#include <cstdarg>
#include <cstdio>

#include "log.h"
#include "prism.h"

namespace acc::menu_speak {

void SpeakChoice(const char* tag, const char* label,
                 const char* ctxFmt, ...) {
    char ctx[128];
    va_list ap;
    va_start(ap, ctxFmt);
    std::vsnprintf(ctx, sizeof(ctx), ctxFmt, ap);
    va_end(ap);
    if (!label || label[0] == '\0') {
        acclog::Write(tag, "speak %s -> empty", ctx);
        return;
    }
    prism::Speak(label, /*interrupt=*/true);
    acclog::Write(tag, "speak %s [%s]", ctx, label);
}

}  // namespace acc::menu_speak
