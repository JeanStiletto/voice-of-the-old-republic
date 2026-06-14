#include "same_name_suffix.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine_area.h"
#include "log.h"
#include "state_overrides.h"
#include "strings.h"

namespace acc::narration {

namespace {

// One entry per game object we have ever narrated in the current
// area. Tiny by design: lookup is linear, but combat encounters and
// puzzle clusters rarely push the live narrated set past a few dozen.
struct Entry {
    uint32_t handle;       // server-side handle (stable across area lifetime)
    int      bucketIdx;    // index into s_buckets
    int      serial;       // 1-based per-bucket ordinal
    char     locName[64];  // bucket key snapshot — for re-key detection
};

// One bucket per distinct LocName ever seen in this area.
struct Bucket {
    char locName[64];
    int  size;             // total serials ever assigned (incl. dead members)
};

std::vector<Entry>  s_entries;
std::vector<Bucket> s_buckets;

int FindBucketIdx(const char* name) {
    for (size_t i = 0; i < s_buckets.size(); ++i) {
        if (std::strcmp(s_buckets[i].locName, name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int GetOrCreateBucketIdx(const char* name) {
    int idx = FindBucketIdx(name);
    if (idx >= 0) return idx;
    s_buckets.push_back({});
    Bucket& nb = s_buckets.back();
    std::strncpy(nb.locName, name, sizeof(nb.locName) - 1);
    nb.locName[sizeof(nb.locName) - 1] = '\0';
    nb.size = 0;
    return static_cast<int>(s_buckets.size() - 1);
}

Entry* FindEntry(uint32_t handle) {
    for (auto& e : s_entries) {
        if (e.handle == handle) return &e;
    }
    return nullptr;
}

// Live "empty" tag for loot containers. Appended last (after the numeric
// disambiguation suffix and any state-override label) so it reads as a
// trailing note: "Fu\xDFschlie\xDFfach 2, leer". No-op for everything that
// isn't an emptied loot container — IsEmptyContainer self-gates on object
// kind + has_inventory and reads the repository item count fresh, so the
// tag tracks the player looting the container without any caching.
void AppendEmptyContainerLabel(void* gameObject, char* outBuf,
                               size_t bufSize) {
    if (!acc::engine::IsEmptyContainer(gameObject)) return;
    const char* word =
        acc::strings::Get(acc::strings::Id::ContainerEmptySuffix);
    if (!word || word[0] == '\0') return;
    size_t curLen = std::strlen(outBuf);
    if (curLen + std::strlen(word) + 3 >= bufSize) return;
    std::snprintf(outBuf + curLen, bufSize - curLen, ", %s", word);
}

}  // namespace

void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize) {
    if (!gameObject || !outBuf || bufSize < 4) return;
    if (outBuf[0] == '\0') return;

    uint32_t handle = acc::engine::GetObjectHandle(gameObject);
    if (handle == 0u || handle == 0xFFFFFFFFu) return;

    // Use current outBuf contents as the bucket key (snapshot before
    // we mutate it).
    char rawName[64];
    std::strncpy(rawName, outBuf, sizeof(rawName) - 1);
    rawName[sizeof(rawName) - 1] = '\0';

    int bucketIdx = GetOrCreateBucketIdx(rawName);
    Entry* entry  = FindEntry(handle);

    if (!entry) {
        s_buckets[bucketIdx].size += 1;
        s_entries.push_back({});
        Entry& ne = s_entries.back();
        ne.handle    = handle;
        ne.bucketIdx = bucketIdx;
        ne.serial    = s_buckets[bucketIdx].size;
        std::strncpy(ne.locName, rawName, sizeof(ne.locName) - 1);
        ne.locName[sizeof(ne.locName) - 1] = '\0';
        entry = &ne;
        acclog::Write("SameNameSuffix",
            "assign handle=0x%08x name=[%s] serial=%d bucketSize=%d",
            handle, rawName, ne.serial, s_buckets[bucketIdx].size);
    } else if (entry->bucketIdx != bucketIdx) {
        // LocName changed under us (placeable script swapped the name,
        // creature polymorph, etc.). Re-bucket: new serial in the new
        // bucket; the old bucket's slot is left in place so other
        // members of the old name keep their numbering.
        s_buckets[bucketIdx].size += 1;
        entry->bucketIdx = bucketIdx;
        entry->serial    = s_buckets[bucketIdx].size;
        std::strncpy(entry->locName, rawName, sizeof(entry->locName) - 1);
        entry->locName[sizeof(entry->locName) - 1] = '\0';
        acclog::Write("SameNameSuffix",
            "rekey handle=0x%08x name=[%s] serial=%d", handle, rawName,
            entry->serial);
    }

    if (s_buckets[bucketIdx].size >= 2) {
        size_t curLen = std::strlen(outBuf);
        if (curLen + 5 < bufSize) {
            std::snprintf(outBuf + curLen, bufSize - curLen, " %d",
                          entry->serial);
        }
    }
}

bool GetSpokenName(void* gameObject, char* outBuf, size_t bufSize) {
    if (!gameObject || !outBuf || bufSize < 2) return false;
    outBuf[0] = '\0';

    if (!acc::engine::GetObjectName(gameObject, outBuf, bufSize) ||
        outBuf[0] == '\0') {
        return false;
    }
    AppendSuffix(gameObject, outBuf, bufSize);
    acc::state::AppendStateLabel(gameObject, outBuf, bufSize);
    AppendEmptyContainerLabel(gameObject, outBuf, bufSize);
    return true;
}

void Reset() {
    if (!s_entries.empty() || !s_buckets.empty()) {
        acclog::Write("SameNameSuffix",
            "reset on area transition (entries=%zu buckets=%zu)",
            s_entries.size(), s_buckets.size());
    }
    s_entries.clear();
    s_buckets.clear();
}

}  // namespace acc::narration
