// Read-only view of NVDA's saved configuration (%APPDATA%\nvda\nvda.ini).
//
// Lets the mod imitate the screen reader's voice for positional speech: which
// synthesizer NVDA uses and with which settings. NVDA offers no API to query
// its live settings (its controller client only accepts text), so this is
// the SAVED state — it changes when NVDA saves its config (NVDA+Ctrl+C, or on
// exit with "save configuration on exit"). Re-loaded on every call; the file
// is a few KB.
//
// nvda.ini is ConfigObj: "[section]", indented "[[subsection]]", "key = value".

#pragma once

#include <string>
#include <vector>

namespace acc::nvda_config {

struct Entry {
    std::string section;     // without brackets, e.g. "speech"
    std::string subsection;  // without brackets, "" at section level
    std::string key;
    std::string value;
};

struct Config {
    std::vector<Entry> entries;

    // Value of `key` in [section] / [[subsection]] ("" subsection = section
    // level), or nullptr when absent.
    const char* Find(const char* section, const char* subsection, const char* key) const;
    // Same, parsed as an int; `fallback` when absent.
    int FindInt(const char* section, const char* subsection, const char* key, int fallback) const;
};

// %APPDATA%\nvda — the installed NVDA's config folder (a portable NVDA keeps
// its own elsewhere and is not found). "" when APPDATA is unset.
std::string ConfigDir();

// Parses nvda.ini. False (and an empty config) when the file is missing.
bool Load(Config& out);

}  // namespace acc::nvda_config
