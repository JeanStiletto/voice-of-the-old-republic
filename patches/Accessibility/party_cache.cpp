#include "party_cache.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_area.h"     // GetObjectDisplayNameByHandle
#include "engine_player.h"   // GetPartyMembers, kPartyTableMaxMembers
#include "log.h"             // acclog::Write

namespace acc::combat {

namespace {

constexpr int   kMaxMembers       = ::kPartyTableMaxMembers;
constexpr int   kNameCap          = 96;
constexpr DWORD kRefreshIntervalMs = 1000;

char  g_names[kMaxMembers][kNameCap];
int   g_name_count        = 0;
DWORD g_last_refresh_tick = 0;
bool  g_initialised       = false;

void Refresh() {
    g_name_count = 0;
    uint32_t handles[kMaxMembers] = {};
    int n = acc::engine::GetPartyMembers(handles, kMaxMembers);
    for (int i = 0; i < n; ++i) {
        char buf[kNameCap] = {};
        if (acc::engine::GetObjectDisplayNameByHandle(handles[i], buf, sizeof(buf)) &&
            buf[0] != '\0') {
            // Trim trailing whitespace / dots so byte-exact comparison
            // survives any quirks in the engine's accessor (also mirrors
            // CopyRange's behavior in the summary parser).
            size_t len = strnlen(buf, sizeof(buf));
            while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '.')) --len;
            if (len == 0) continue;
            if (len >= kNameCap) len = kNameCap - 1;
            memcpy(g_names[g_name_count], buf, len);
            g_names[g_name_count][len] = '\0';
            ++g_name_count;
        }
    }
    g_last_refresh_tick = GetTickCount();
    g_initialised = true;

    // Log the resolved party set so we can verify name-byte parity with
    // the message buffer if the filter ever silently mis-classifies a
    // hit-on-party as "suppressed".
    char joined[512] = {};
    size_t pos = 0;
    for (int i = 0; i < g_name_count && pos < sizeof(joined) - 1; ++i) {
        if (i > 0 && pos < sizeof(joined) - 2) { joined[pos++] = ','; joined[pos++] = ' '; }
        size_t nlen = strnlen(g_names[i], kNameCap);
        if (pos + nlen >= sizeof(joined)) nlen = sizeof(joined) - 1 - pos;
        memcpy(joined + pos, g_names[i], nlen);
        pos += nlen;
    }
    joined[pos] = '\0';
    acclog::Write("Party.Cache", "refresh: n=%d [%s]", g_name_count, joined);
}

void RefreshIfStale() {
    DWORD now = GetTickCount();
    if (!g_initialised || (now - g_last_refresh_tick) >= kRefreshIntervalMs) {
        Refresh();
    }
}

}  // namespace

bool IsPartyMember(const char* name) {
    if (!name || !*name) return false;
    RefreshIfStale();
    for (int i = 0; i < g_name_count; ++i) {
        if (strcmp(g_names[i], name) == 0) return true;
    }
    return false;
}

void InvalidatePartyCache() {
    g_initialised = false;
}

}  // namespace acc::combat
