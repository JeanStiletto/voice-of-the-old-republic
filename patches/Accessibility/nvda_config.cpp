#include "nvda_config.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace acc::nvda_config {

namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "[[name]]" / "[name]" → "name".
std::string Unbracket(const std::string& t) {
    size_t a = t.find_first_not_of('[');
    size_t b = t.find_last_not_of(']');
    if (a == std::string::npos || b == std::string::npos || b < a) return std::string();
    return t.substr(a, b - a + 1);
}

}  // namespace

const char* Config::Find(const char* section, const char* subsection, const char* key) const {
    for (const Entry& e : entries) {
        if (e.section == section && e.subsection == subsection && e.key == key) {
            return e.value.c_str();
        }
    }
    return nullptr;
}

int Config::FindInt(const char* section, const char* subsection, const char* key,
                    int fallback) const {
    const char* v = Find(section, subsection, key);
    return v ? std::atoi(v) : fallback;
}

std::string ConfigDir() {
    char appData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("APPDATA", appData, sizeof(appData));
    if (n == 0 || n >= sizeof(appData)) return std::string();
    return std::string(appData) + "\\nvda";
}

bool Load(Config& out) {
    out.entries.clear();
    const std::string dir = ConfigDir();
    if (dir.empty()) return false;
    FILE* f = std::fopen((dir + "\\nvda.ini").c_str(), "r");
    if (!f) return false;
    std::string section, subsection;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.compare(0, 2, "[[") == 0) { subsection = Unbracket(t); continue; }
        if (t[0] == '[') { section = Unbracket(t); subsection.clear(); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        out.entries.push_back(Entry{section, subsection, Trim(t.substr(0, eq)),
                                    Trim(t.substr(eq + 1))});
    }
    std::fclose(f);
    return true;
}

}  // namespace acc::nvda_config
