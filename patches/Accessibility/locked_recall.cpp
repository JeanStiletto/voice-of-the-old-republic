#include "locked_recall.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_reads.h"     // LookupTlk
#include "log.h"
#include "msg_router.h"
#include "narrated_target.h"
#include "prism.h"

namespace acc::locked_recall {

namespace {

// strref 1437 = "This object is locked." — the engine's generic locked
// feedback, appended to the message buffer on every failed open. Resolved to
// the current locale once (via the engine TLK) then matched against the
// router's rendered line, so the trigger stays language-independent.
constexpr uint32_t kLockedFeedbackStrref = 1437;

// How long after the "locked" line an ownerless bark still counts as that
// object's explanation. On a first failed open the engine emits the locked
// line and the story bark together (same frame in the captured logs); the
// window only needs to bridge the message-then-bark ordering plus a tick.
constexpr unsigned int kCaptureWindowMs = 2500;

char s_lockedText[256] = {0};
bool s_lockedTextBuilt = false;

// Small per-object cache. Doors/containers per area comfortably fit; once full
// the oldest entry is overwritten (a locked object re-visited after 32 others
// simply re-captures on its next first-attempt bark).
struct Entry {
    uint32_t handle;
    char     bark[512];
};
constexpr int kMaxEntries = 32;
Entry s_cache[kMaxEntries];
int   s_cacheCount = 0;
int   s_cacheNext  = 0;   // ring overwrite cursor once full

uint32_t     s_recentHandle = 0;
unsigned int s_recentTickMs = 0;

char s_pendingReplay[512] = {0};

// Trim-insensitive compare (mirrors tutorial_hints::EqualsTrimmed) — guards a
// stray padding space between the router's rendered line and our TLK
// resolution of the same strref.
bool EqualsTrimmed(const char* a, const char* b) {
    while (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r') ++a;
    while (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r') ++b;
    size_t la = std::strlen(a), lb = std::strlen(b);
    while (la > 0 && (a[la-1] == ' ' || a[la-1] == '\t' ||
                      a[la-1] == '\n' || a[la-1] == '\r')) --la;
    while (lb > 0 && (b[lb-1] == ' ' || b[lb-1] == '\t' ||
                      b[lb-1] == '\n' || b[lb-1] == '\r')) --lb;
    return la == lb && std::memcmp(a, b, la) == 0;
}

bool IsLockedMessage(const char* text) {
    if (!s_lockedTextBuilt) {
        // Retry until the TLK table is live (fails on main-menu / early load).
        if (acc::engine::LookupTlk(kLockedFeedbackStrref, s_lockedText,
                                   sizeof(s_lockedText)) && s_lockedText[0]) {
            s_lockedTextBuilt = true;
        } else {
            return false;
        }
    }
    return EqualsTrimmed(text, s_lockedText);
}

const char* Lookup(uint32_t handle) {
    for (int i = 0; i < s_cacheCount; ++i) {
        if (s_cache[i].handle == handle) return s_cache[i].bark;
    }
    return nullptr;
}

void Store(uint32_t handle, const char* bark) {
    for (int i = 0; i < s_cacheCount; ++i) {
        if (s_cache[i].handle == handle) {  // refresh in place
            std::strncpy(s_cache[i].bark, bark, sizeof(s_cache[i].bark) - 1);
            s_cache[i].bark[sizeof(s_cache[i].bark) - 1] = '\0';
            return;
        }
    }
    int slot;
    if (s_cacheCount < kMaxEntries) {
        slot = s_cacheCount++;
    } else {
        slot = s_cacheNext;
        s_cacheNext = (s_cacheNext + 1) % kMaxEntries;
    }
    s_cache[slot].handle = handle;
    std::strncpy(s_cache[slot].bark, bark, sizeof(s_cache[slot].bark) - 1);
    s_cache[slot].bark[sizeof(s_cache[slot].bark) - 1] = '\0';
}

// Feedback-router rule. Never consumes — the generic "locked" line still speaks
// through the router's default path; we only note the interacted object and, on
// a repeat, queue the cached explanation.
bool RuleLocked(const char* text) {
    if (!IsLockedMessage(text)) return false;

    // The locked feedback carries no object id; the object is whatever the
    // player just acted on, i.e. the narrated-target slot the interact used.
    acc::narrated_target::Slot slot;
    if (!acc::narrated_target::TryGet(slot) || slot.isMapPin ||
        slot.handle == 0) {
        return false;  // no object handle to key on
    }

    s_recentHandle = slot.handle;
    s_recentTickMs = GetTickCount();

    const char* cached = Lookup(slot.handle);
    if (cached && cached[0]) {
        std::strncpy(s_pendingReplay, cached, sizeof(s_pendingReplay) - 1);
        s_pendingReplay[sizeof(s_pendingReplay) - 1] = '\0';
        acclog::Write("LockedRecall",
                      "repeat lock handle=0x%08x -> queue replay", slot.handle);
    } else {
        acclog::Write("LockedRecall",
                      "first lock handle=0x%08x -> awaiting bark", slot.handle);
    }
    return false;
}

}  // namespace

void RegisterMsgRule() {
    acc::msg::Router::Instance().AddRule("LockedRecall", RuleLocked);
}

void MaybeCapture(const char* barkText, bool ownerless) {
    if (!ownerless || !barkText || !barkText[0]) return;
    if (s_recentHandle == 0) return;
    if (GetTickCount() - s_recentTickMs > kCaptureWindowMs) return;
    if (Lookup(s_recentHandle)) return;  // already have this object's line
    Store(s_recentHandle, barkText);
    acclog::Write("LockedRecall", "captured handle=0x%08x -> [%.200s]",
                  s_recentHandle, barkText);
    s_recentHandle = 0;  // consume — one bark per locked interaction
}

void Tick() {
    if (!s_pendingReplay[0]) return;
    prism::Speak(s_pendingReplay, /*interrupt=*/false);
    acclog::Write("LockedRecall", "replay -> [%.200s]", s_pendingReplay);
    s_pendingReplay[0] = '\0';
}

}  // namespace acc::locked_recall
