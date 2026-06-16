#include "mod_settings_store.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "log.h"

namespace acc::settings {

namespace {

std::mutex                          g_mtx;
std::map<std::string, std::string>  g_kv;
bool                                g_loaded = false;
char                                g_path[MAX_PATH] = "";

// <install>\acc_settings.ini, derived from acclog::PatchDir() (=<install>\
// patches) by stripping the last path component. Returns false if the patch
// dir isn't known yet (very early DLL attach) — caller then runs on defaults.
bool ResolvePath() {
    if (g_path[0]) return true;
    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) return false;
    char buf[MAX_PATH];
    strncpy_s(buf, patchDir, _TRUNCATE);
    char* slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';  // <install>\patches -> <install>
    snprintf(g_path, sizeof(g_path), "%s\\acc_settings.ini", buf);
    return true;
}

// Must hold g_mtx.
void EnsureLoaded() {
    if (g_loaded) return;
    g_loaded = true;  // set first: a failed/missing load shouldn't retry per call
    if (!ResolvePath()) {
        acclog::Write("Settings",
            "patch dir unknown; running on in-memory defaults");
        return;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, g_path, "rb") != 0 || !f) {
        acclog::Write("Settings", "no file at %s; defaults apply", g_path);
        return;
    }
    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' '  || line[n - 1] == '\t')) {
            line[--n] = '\0';
        }
        if (n == 0 || line[0] == '#' || line[0] == ';') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        g_kv[std::string(line)] = std::string(eq + 1);
        ++count;
    }
    fclose(f);
    acclog::Write("Settings", "loaded %d setting(s) from %s", count, g_path);
}

// Must hold g_mtx. Full rewrite — the file is tiny.
void Save() {
    if (!ResolvePath()) return;
    FILE* f = nullptr;
    if (fopen_s(&f, g_path, "wb") != 0 || !f) {
        acclog::Write("Settings", "WARNING: could not write %s", g_path);
        return;
    }
    fputs("# Voice of the Old Republic - mod settings (auto-generated)\r\n", f);
    for (const auto& kv : g_kv) {
        fprintf(f, "%s=%s\r\n", kv.first.c_str(), kv.second.c_str());
    }
    fclose(f);
}

}  // namespace

bool GetBool(const char* key, bool defValue) {
    if (!key) return defValue;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    auto it = g_kv.find(key);
    if (it == g_kv.end()) return defValue;
    const std::string& v = it->second;
    if (v == "1" || v == "true"  || v == "on"  || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no")  return false;
    return defValue;
}

int GetInt(const char* key, int defValue) {
    if (!key) return defValue;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    auto it = g_kv.find(key);
    if (it == g_kv.end()) return defValue;
    char* end = nullptr;
    long v = strtol(it->second.c_str(), &end, 10);
    if (end == it->second.c_str()) return defValue;  // unparseable
    return static_cast<int>(v);
}

void SetBool(const char* key, bool value) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    g_kv[key] = value ? "1" : "0";
    Save();
    acclog::Write("Settings", "set %s=%d", key, value ? 1 : 0);
}

void SetInt(const char* key, int value) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    g_kv[key] = buf;
    Save();
    acclog::Write("Settings", "set %s=%d", key, value);
}

bool GetStr(const char* key, char* outBuf, int bufSize) {
    if (!outBuf || bufSize <= 0) return false;
    outBuf[0] = '\0';
    if (!key) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    auto it = g_kv.find(key);
    if (it == g_kv.end()) return false;
    strncpy_s(outBuf, static_cast<size_t>(bufSize), it->second.c_str(), _TRUNCATE);
    return true;
}

void SetStr(const char* key, const char* value) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureLoaded();
    g_kv[key] = value ? value : "";
    Save();
    acclog::Write("Settings", "set %s=%s", key, value ? value : "");
}

}  // namespace acc::settings
