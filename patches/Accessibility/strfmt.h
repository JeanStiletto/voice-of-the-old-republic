// Heap-backed printf → std::string. The accessibility project speaks
// localized, format-string-driven text (the strings.h table is full of
// "%s …" patterns); routing those through fixed char buffers truncated
// real announcements at arbitrary lengths (the Taris store-door exit
// dropped off the end of a 96-byte cluster label). These helpers format
// into an exactly-sized std::string so spoken/announced content is never
// silently shortened — and a malformed read shows up as visible garbage
// rather than being hidden by a clean cut, which is what we want.
//
// Header-only; no TU. Two-pass vsnprintf: measure, then fill.

#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace acc::strfmt {

inline std::string VFormat(const char* fmt, va_list ap) {
    if (!fmt || !fmt[0]) return std::string();
    va_list ap2;
    va_copy(ap2, ap);
    int needed = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    // out has `needed` chars + the guaranteed null at out[needed];
    // vsnprintf writes `needed` chars and a terminating null into that
    // null slot, which std::string permits (data()[size()] == '\0').
    std::vsnprintf(&out[0], static_cast<size_t>(needed) + 1, fmt, ap);
    return out;
}

// printf-style, unbounded. Returns "" on null/empty format or encoding
// error.
inline std::string Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string out = VFormat(fmt, ap);
    va_end(ap);
    return out;
}

}  // namespace acc::strfmt
