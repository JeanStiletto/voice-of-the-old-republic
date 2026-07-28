# strfmt.h (45 lines)

Header-only heap-backed printf-to-std::string helper. Exists because the
project's localised, `%s`-heavy strings.h templates were being formatted into
fixed char buffers that silently truncated real announcements (e.g. a Taris
store-door label dropped off the end of a 96-byte buffer). Two-pass
`vsnprintf` (measure then fill) into an exactly-sized std::string; a malformed
read shows as visible garbage instead of a silent clean cut, which the project
prefers.

## Declarations (in source order)

- L20 — `inline std::string VFormat(const char* fmt, va_list ap)`
  note: two-pass vsnprintf; relies on std::string's guaranteed data()[size()]=='\0' slot
- L37 — `inline std::string Format(const char* fmt, ...)`
  note: returns "" on null/empty format or encoding error
