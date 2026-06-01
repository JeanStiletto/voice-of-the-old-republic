#include "log.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace acclog {

namespace {

// --- Paths + file handle ---------------------------------------------------
char  g_logDir[MAX_PATH]   = "logs";
char  g_logPath[MAX_PATH]  = "logs\\patch.log";
char  g_patchDir[MAX_PATH] = "";
FILE* g_logFile            = nullptr;
bool  g_logOpenAttempted   = false;

// --- Thread safety ---------------------------------------------------------
// Hooks can fire from non-main threads (audio dispatch, async loaders). One
// CRITICAL_SECTION guards both the file handle and all dedup state so the
// helpers stay correct under concurrent fire.
CRITICAL_SECTION g_lock;
bool             g_lockInited = false;

struct Locker {
    Locker()  { if (g_lockInited) EnterCriticalSection(&g_lock); }
    ~Locker() { if (g_lockInited) LeaveCriticalSection(&g_lock); }
};

// --- Trace dedup state -----------------------------------------------------
//
// Per-tag entry. Tag is matched by content (strcmp) so the caller can pass
// a string literal or a stack-formatted key — both work. Stale entries
// (no fire for >kStaleMs) auto-flush on next helper call.

constexpr size_t kMessageBuf = 1024;   // bumped from 512 to absorb RE-peek dumps
constexpr int    kMaxKeys    = 96;
constexpr DWORD  kStaleMs    = 1000;   // auto-flush window

struct DedupEntry {
    char  tag[64];
    char  lastContent[kMessageBuf];
    int   suppressed;     // count of identical fires after `lastContent` was emitted
    int   state;          // for Edge: most recent reported state
    int   stateHeld;      // for Edge: count of equal-state fires before the last edge
    bool  hasContent;     // Trace ever emitted for this tag
    bool  hasState;       // Edge ever emitted for this tag
    bool  isOnce;         // Once: tag has been emitted, drop subsequent
    DWORD lastSeenMs;
};

DedupEntry g_keys[kMaxKeys];
int        g_keyCount = 0;

DedupEntry* GetOrCreateEntry(const char* tag) {
    for (int i = 0; i < g_keyCount; ++i) {
        if (strcmp(g_keys[i].tag, tag) == 0) return &g_keys[i];
    }
    if (g_keyCount >= kMaxKeys) {
        // Out of slots — return null; caller falls through to plain Write so
        // information isn't lost.
        return nullptr;
    }
    DedupEntry* e = &g_keys[g_keyCount++];
    strncpy_s(e->tag, tag, _TRUNCATE);
    e->lastContent[0] = '\0';
    e->suppressed     = 0;
    e->state          = 0;
    e->stateHeld      = 0;
    e->hasContent     = false;
    e->hasState       = false;
    e->isOnce         = false;
    e->lastSeenMs     = 0;
    return e;
}

// --- Pointer registry ------------------------------------------------------
struct PtrName {
    const void* ptr;
    char        name[32];
};
constexpr int kMaxPtrNames = 32;
PtrName g_ptrNames[kMaxPtrNames];
int     g_ptrNameCount = 0;

// --- Internal write (no dedup, must be called under lock) -----------------
void RawWriteLocked(const char* tag, const char* content) {
    SYSTEMTIME ts;
    GetSystemTime(&ts);

    char line[kMessageBuf + 128];
    int n;
    if (tag && *tag) {
        n = snprintf(line, sizeof(line),
            "%04d-%02d-%02dT%02d:%02d:%02dZ [accessibility] %s: %s\n",
            ts.wYear, ts.wMonth, ts.wDay,
            ts.wHour, ts.wMinute, ts.wSecond,
            tag, content);
    } else {
        n = snprintf(line, sizeof(line),
            "%04d-%02d-%02dT%02d:%02d:%02dZ [accessibility] %s\n",
            ts.wYear, ts.wMonth, ts.wDay,
            ts.wHour, ts.wMinute, ts.wSecond,
            content);
    }
    (void)n;

    // Live debug stream — ONLY when an actual debugger is attached.
    //
    // OutputDebugStringA takes a system-wide named mutex (DBWinMutex) and,
    // when a listener exists, blocks on an ack event. Any background process
    // that drains the debug buffer (AV/EDR agents, DebugView, telemetry) can
    // therefore stall EVERY call here — and since this fires on every log
    // line on the game's main tick, a contended drain serialises the whole
    // tick and the game freezes for seconds (audio underruns because KOTOR
    // refills its streaming buffers on that same tick). IsDebuggerPresent is
    // a single PEB byte-read (no syscall, no lock), so gating costs nothing
    // and loses no information: the full log still goes to the file below,
    // which is what `kdev logs` and beta-tester reports read. The live
    // OutputDebugString view re-enables automatically under a real debugger.
    if (IsDebuggerPresent()) OutputDebugStringA(line);

    // Lazy-open — see comment-of-record in pre-2026-05-08 history.
    if (!g_logOpenAttempted) {
        g_logOpenAttempted = true;
        CreateDirectoryA(g_logDir, nullptr);
        if (fopen_s(&g_logFile, g_logPath, "ab") != 0 || !g_logFile) {
            g_logFile = nullptr;
            OutputDebugStringA("[accessibility] WARNING: log file open failed\n");
        }
    }
    if (g_logFile) {
        fputs(line, g_logFile);
        fflush(g_logFile);
    }
}

// Flush a single Trace entry's pending suppression count. Must be called
// under lock. Resets the counter.
void FlushTraceLocked(DedupEntry* e) {
    if (!e || e->suppressed <= 0) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "(repeated %dx more)", e->suppressed);
    RawWriteLocked(e->tag, buf);
    e->suppressed = 0;
}

// Flush an Edge entry's hold count. Must be called under lock.
void FlushEdgeHoldLocked(DedupEntry* e) {
    if (!e || e->stateHeld <= 0) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "(prev state held %dx)", e->stateHeld);
    RawWriteLocked(e->tag, buf);
    e->stateHeld = 0;
}

// --- Block dedup state (BlockLog) ------------------------------------------
//
// One entry per BlockLog tag. We store only a hash of the last block (not its
// full text) plus the first line as a human-readable descriptor for the
// "(repeated Nx)" summary — a 64-bit FNV-1a collision (and thus a missed
// re-print) is ~1-in-2^64, acceptable for diagnostics.

constexpr int kMaxBlockKeys = 16;

struct BlockEntry {
    char     tag[64];
    uint64_t lastHash;
    bool     hasLast;
    int      repeated;        // identical blocks folded since lastHash emitted
    char     descriptor[96];  // first line of the folded block, for the summary
    DWORD    lastSeenMs;
};

BlockEntry g_blocks[kMaxBlockKeys];
int        g_blockCount = 0;

uint64_t Fnv1a64(const char* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

BlockEntry* GetOrCreateBlock(const char* tag) {
    for (int i = 0; i < g_blockCount; ++i) {
        if (strcmp(g_blocks[i].tag, tag) == 0) return &g_blocks[i];
    }
    if (g_blockCount >= kMaxBlockKeys) return nullptr;
    BlockEntry* e = &g_blocks[g_blockCount++];
    strncpy_s(e->tag, tag, _TRUNCATE);
    e->lastHash      = 0;
    e->hasLast       = false;
    e->repeated      = 0;
    e->descriptor[0] = '\0';
    e->lastSeenMs    = 0;
    return e;
}

// Emit the "(repeated Nx)" summary for a folded run. Must be called under lock.
void FlushBlockLocked(BlockEntry* e) {
    if (!e || e->repeated <= 0) return;
    char buf[160];
    snprintf(buf, sizeof(buf), "(repeated %dx — unchanged: %s)",
             e->repeated, e->descriptor);
    RawWriteLocked(e->tag, buf);
    e->repeated = 0;
}

// Emit a '\n'-separated block buffer as one tagged line per segment. Under lock.
void EmitBlockLinesLocked(const char* tag, const char* buf) {
    const char* start = buf;
    while (*start) {
        const char* nl = strchr(start, '\n');
        if (!nl) { RawWriteLocked(tag, start); break; }
        char line[kMessageBuf];
        size_t n = static_cast<size_t>(nl - start);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, start, n);
        line[n] = '\0';
        RawWriteLocked(tag, line);
        start = nl + 1;
    }
}

// Sweep all entries; flush any whose dedup window has gone stale. Called
// at the top of every helper so a paused subsystem's count surfaces
// without explicit FlushAll calls.
void SweepStaleLocked() {
    DWORD now = GetTickCount();
    for (int i = 0; i < g_keyCount; ++i) {
        DedupEntry* e = &g_keys[i];
        if (e->lastSeenMs == 0) continue;
        if (now - e->lastSeenMs < kStaleMs) continue;
        // Stale — flush whichever counter is non-zero.
        if (e->suppressed > 0) FlushTraceLocked(e);
        if (e->stateHeld  > 0) FlushEdgeHoldLocked(e);
    }
    for (int i = 0; i < g_blockCount; ++i) {
        BlockEntry* e = &g_blocks[i];
        if (e->lastSeenMs == 0 || e->repeated <= 0) continue;
        if (now - e->lastSeenMs < kStaleMs) continue;
        FlushBlockLocked(e);
    }
}

}  // namespace

// Public API

void Init(HINSTANCE hinstDLL) {
    if (!g_lockInited) {
        InitializeCriticalSection(&g_lock);
        g_lockInited = true;
    }

    char dllPath[MAX_PATH] = "";
    DWORD n = GetModuleFileNameA(hinstDLL, dllPath, sizeof(dllPath));
    if (n == 0 || n >= sizeof(dllPath)) {
        OutputDebugStringA("[accessibility] GetModuleFileNameA failed; using CWD-relative paths\n");
        return;
    }

    char* slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';                          // <install>\patches
    strncpy_s(g_patchDir, dllPath, _TRUNCATE);

    slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';                          // <install>

    snprintf(g_logDir, sizeof(g_logDir), "%s\\logs", dllPath);

    SYSTEMTIME ts;
    GetSystemTime(&ts);
    snprintf(g_logPath, sizeof(g_logPath),
        "%s\\patch-%04d%02d%02d-%02d%02d%02d.log",
        g_logDir,
        ts.wYear, ts.wMonth, ts.wDay,
        ts.wHour, ts.wMinute, ts.wSecond);

    char dbg[MAX_PATH + 64];
    snprintf(dbg, sizeof(dbg), "[accessibility] log path: %s\n", g_logPath);
    OutputDebugStringA(dbg);

    // Consolidated header line so the log is self-describing in isolation.
    // Prism + version SHA are reported separately later (Prism init is
    // deferred out of DllMain; SHA comes from the env var sourced from
    // kdev's build artefacts) — this header captures what's known *now*.
    SYSTEMTIME utc, local;
    GetSystemTime(&utc);
    GetLocalTime(&local);
    TIME_ZONE_INFORMATION tz;
    GetTimeZoneInformation(&tz);
    char locale[16] = "?";
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SNAME, locale, sizeof(locale));
    {
        Locker lk;
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "session start utc=%04d-%02d-%02dT%02d:%02d:%02dZ "
            "local=%02d:%02d:%02d locale=%s pid=%lu",
            utc.wYear, utc.wMonth, utc.wDay,
            utc.wHour, utc.wMinute, utc.wSecond,
            local.wHour, local.wMinute, local.wSecond,
            locale, GetCurrentProcessId());
        RawWriteLocked("Init.header", hdr);
        snprintf(hdr, sizeof(hdr), "log path=%s", g_logPath);
        RawWriteLocked("Init.header", hdr);
    }
}

void Shutdown() {
    if (!g_lockInited) return;
    EnterCriticalSection(&g_lock);
    SweepStaleLocked();
    for (int i = 0; i < g_keyCount; ++i) {
        if (g_keys[i].suppressed > 0) FlushTraceLocked(&g_keys[i]);
        if (g_keys[i].stateHeld  > 0) FlushEdgeHoldLocked(&g_keys[i]);
    }
    for (int i = 0; i < g_blockCount; ++i) {
        if (g_blocks[i].repeated > 0) FlushBlockLocked(&g_blocks[i]);
    }
    if (g_logFile) {
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = nullptr;
    }
    LeaveCriticalSection(&g_lock);
    // Deliberately leave g_lock initialized — late writes after Shutdown
    // (DLL_PROCESS_DETACH from another thread) still go to OutputDebugStringA.
}

void Write(const char* tag, const char* fmt, ...) {
    if (!fmt) return;

    char message[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Locker l;
    SweepStaleLocked();
    RawWriteLocked(tag ? tag : "", message);
}

void Trace(const char* tag, const char* fmt, ...) {
    if (!tag || !*tag || !fmt) return;

    char message[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Locker l;
    SweepStaleLocked();

    DedupEntry* e = GetOrCreateEntry(tag);
    if (!e) {
        // Slot table full — fall back to plain write so info isn't lost.
        RawWriteLocked(tag, message);
        return;
    }

    DWORD now = GetTickCount();

    if (e->hasContent && strcmp(e->lastContent, message) == 0) {
        e->suppressed++;
        e->lastSeenMs = now;
        return;
    }

    // Content changed (or first call) — flush prior suppression count first.
    if (e->suppressed > 0) FlushTraceLocked(e);

    RawWriteLocked(tag, message);
    strncpy_s(e->lastContent, message, _TRUNCATE);
    e->hasContent = true;
    e->suppressed = 0;
    e->lastSeenMs = now;
}

void Once(const char* tag, const char* fmt, ...) {
    if (!tag || !*tag || !fmt) return;

    char message[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Locker l;
    SweepStaleLocked();

    DedupEntry* e = GetOrCreateEntry(tag);
    if (!e) {
        RawWriteLocked(tag, message);
        return;
    }
    if (e->isOnce) return;
    RawWriteLocked(tag, message);
    e->isOnce     = true;
    e->lastSeenMs = GetTickCount();
}

void Edge(const char* tag, int state, const char* fmt, ...) {
    if (!tag || !*tag || !fmt) return;

    char message[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Locker l;
    SweepStaleLocked();

    DedupEntry* e = GetOrCreateEntry(tag);
    if (!e) {
        RawWriteLocked(tag, message);
        return;
    }

    DWORD now = GetTickCount();

    if (e->hasState && e->state == state) {
        e->stateHeld++;
        e->lastSeenMs = now;
        return;
    }

    // Edge transition (or first call). Flush hold count of previous state.
    if (e->stateHeld > 0) FlushEdgeHoldLocked(e);

    RawWriteLocked(tag, message);
    e->state      = state;
    e->hasState   = true;
    e->stateHeld  = 0;
    e->lastSeenMs = now;
}

void WriteHex(const char* tag, const char* label, const void* bytes, size_t len) {
    if (!bytes || len == 0) {
        Write(tag, "%s len=0", label ? label : "");
        return;
    }
    auto* p = static_cast<const unsigned char*>(bytes);
    constexpr size_t kPerLine = 64;
    char buf[kPerLine * 3 + 8];

    if (label && *label) {
        Write(tag, "%s len=%zu", label, len);
    }
    for (size_t off = 0; off < len; off += kPerLine) {
        size_t chunk = (len - off) < kPerLine ? (len - off) : kPerLine;
        size_t pos = 0;
        for (size_t i = 0; i < chunk; ++i) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", p[off + i]);
        }
        // Trim trailing space.
        if (pos > 0 && buf[pos - 1] == ' ') buf[pos - 1] = '\0';
        Write(tag, "  +%04zx %s", off, buf);
    }
}

void RegisterPtr(const void* ptr, const char* name) {
    if (!ptr || !name || !*name) return;
    Locker l;
    for (int i = 0; i < g_ptrNameCount; ++i) {
        if (g_ptrNames[i].ptr == ptr) {
            strncpy_s(g_ptrNames[i].name, name, _TRUNCATE);
            return;
        }
    }
    if (g_ptrNameCount >= kMaxPtrNames) return;
    g_ptrNames[g_ptrNameCount].ptr = ptr;
    strncpy_s(g_ptrNames[g_ptrNameCount].name, name, _TRUNCATE);
    g_ptrNameCount++;
}

const char* FmtPtr(const void* ptr) {
    // Rotating buffer so multiple FmtPtr calls in one snprintf coexist.
    static char buffers[8][32];
    static int  next = 0;
    {
        Locker l;
        for (int i = 0; i < g_ptrNameCount; ++i) {
            if (g_ptrNames[i].ptr == ptr) return g_ptrNames[i].name;
        }
    }
    char* b = buffers[next];
    next = (next + 1) % 8;
    snprintf(b, sizeof(buffers[0]), "%p", ptr);
    return b;
}

void FlushAll() {
    if (!g_lockInited) return;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_keyCount; ++i) {
        if (g_keys[i].suppressed > 0) FlushTraceLocked(&g_keys[i]);
        if (g_keys[i].stateHeld  > 0) FlushEdgeHoldLocked(&g_keys[i]);
    }
    for (int i = 0; i < g_blockCount; ++i) {
        if (g_blocks[i].repeated > 0) FlushBlockLocked(&g_blocks[i]);
    }
    if (g_logFile) fflush(g_logFile);
    LeaveCriticalSection(&g_lock);
}

// --- BlockLog --------------------------------------------------------------

BlockLog::BlockLog(const char* tag)
    : tag_(tag ? tag : ""), len_(0), ended_(false), passthrough_(false),
      hasKey_(false), keyHash_(1469598103934665603ull) {
    buf_[0] = '\0';
}

BlockLog::~BlockLog() { End(); }

void BlockLog::Key(const char* fmt, ...) {
    if (ended_ || !fmt) return;
    char msg[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    // Stream the bytes into the running FNV-1a hash (plus a '\n' separator so
    // adjacent keys can't run together) — no buffer, identity only.
    for (const char* p = msg; *p; ++p) {
        keyHash_ ^= static_cast<unsigned char>(*p);
        keyHash_ *= 1099511628211ull;
    }
    keyHash_ ^= static_cast<unsigned char>('\n');
    keyHash_ *= 1099511628211ull;
    hasKey_ = true;
}

void BlockLog::Line(const char* fmt, ...) {
    if (ended_ || !fmt) return;

    char msg[kMessageBuf];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (passthrough_) {
        Write(tag_, "%s", msg);
        return;
    }

    size_t mlen = strlen(msg);
    if (len_ + mlen + 2 >= kCap) {
        // Block exceeds the buffer — emit what we have plus this line
        // verbatim and switch to passthrough so the rest still logs in full
        // (just un-folded). Rare: only fires for >16KB blocks.
        Locker l;
        EmitBlockLinesLocked(tag_, buf_);
        RawWriteLocked(tag_, msg);
        passthrough_ = true;
        return;
    }
    memcpy(buf_ + len_, msg, mlen);
    len_ += mlen;
    buf_[len_++] = '\n';
    buf_[len_]   = '\0';
}

void BlockLog::End() {
    if (ended_) return;
    ended_ = true;
    if (passthrough_ || len_ == 0) return;   // already emitted / nothing buffered

    // Identity = the Key() stream if any was supplied (folds volatile noise
    // like churning heap pointers), else the full display text.
    uint64_t h = hasKey_ ? keyHash_ : Fnv1a64(buf_, len_);

    Locker l;
    SweepStaleLocked();

    BlockEntry* e = GetOrCreateBlock(tag_);
    if (!e) {                                 // table full — emit, don't fold
        EmitBlockLinesLocked(tag_, buf_);
        return;
    }

    DWORD now = GetTickCount();
    if (e->hasLast && e->lastHash == h) {
        e->repeated++;
        e->lastSeenMs = now;
        return;                               // fold: identical to previous block
    }

    // Different (or first) block — flush any pending fold count, then emit.
    if (e->repeated > 0) FlushBlockLocked(e);
    EmitBlockLinesLocked(tag_, buf_);

    // Remember the first line as the summary descriptor for the next fold.
    const char* nl = strchr(buf_, '\n');
    size_t dn = nl ? static_cast<size_t>(nl - buf_) : strlen(buf_);
    if (dn >= sizeof(e->descriptor)) dn = sizeof(e->descriptor) - 1;
    memcpy(e->descriptor, buf_, dn);
    e->descriptor[dn] = '\0';

    e->lastHash   = h;
    e->hasLast    = true;
    e->repeated   = 0;
    e->lastSeenMs = now;
}

const char* PatchDir() {
    return g_patchDir;
}

namespace {
ULONGLONG g_bringupBaselineMs = 0;
}

void BringupMark(const char* name) {
    ULONGLONG now = GetTickCount64();
    if (g_bringupBaselineMs == 0) {
        g_bringupBaselineMs = now;
        Write("Bringup", "%s t+0ms (baseline)", name);
        return;
    }
    Write("Bringup", "%s t+%llums", name,
          static_cast<unsigned long long>(now - g_bringupBaselineMs));
}

}  // namespace acclog
