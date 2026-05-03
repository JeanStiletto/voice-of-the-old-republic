#include "engine_area.h"

#include <windows.h>
#include <cstdint>

#include "engine_player.h"  // GetPlayerArea

namespace acc::engine {

namespace {

typedef void* (__thiscall* PFN_CSWSAreaGetRoom)(void* this_,
                                                Vector* pos,
                                                int* outRoomIndex);

}  // namespace

void* GetCurrentArea() {
    return GetPlayerArea();  // already SEH-guarded inside engine_player
}

int GetObjectKind(void* gameObject) {
    if (!gameObject) return -1;
    __try {
        return static_cast<int>(*reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(gameObject) + kObjectKindOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool GetObjectPosition(void* gameObject, Vector& out) {
    if (!gameObject) return false;
    __try {
        out = *reinterpret_cast<Vector*>(
            reinterpret_cast<unsigned char*>(gameObject) +
            kServerObjectPositionOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetRoomAt(void* area, const Vector& pos) {
    if (!area) return nullptr;
    Vector local = pos;  // GetRoom takes Vector* — pass a writable address
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaGetRoom>(kAddrCSWSAreaGetRoom);
        return fn(area, &local, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

AreaObjectIterator::AreaObjectIterator(void* area)
    : data_(nullptr), size_(0), index_(0) {
    if (!area) return;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        data_ = *reinterpret_cast<void***>(base + kAreaGameObjectsOffset);
        size_ = *reinterpret_cast<int*>   (base + kAreaGameObjectCountOffset);
        if (size_ < 0) size_ = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        data_ = nullptr;
        size_ = 0;
    }
}

void* AreaObjectIterator::Next() {
    if (!data_) return nullptr;
    __try {
        while (index_ < size_) {
            void* obj = data_[index_++];
            if (obj) return obj;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        size_ = 0;
    }
    return nullptr;
}

}  // namespace acc::engine
