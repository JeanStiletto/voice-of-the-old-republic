#include "save_crash_guard.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "log.h"

namespace acc::save_guard {

namespace {

// void* __cdecl ImageScale(byte* src, int srcW, int srcH, int bpp,
//                          int dstW, int dstH)
//   @0x0045dad0. Divisor (srcW*srcH) is unguarded; destination area is.
constexpr uintptr_t kImageScaleAddr     = 0x0045dad0;
// Continue point = entry + 8: just past the three prologue instructions we
// relocate into the trampoline (SUB ESP,0x30 / PUSH EBX / MOV EBX,[ESP+0x4c]).
constexpr uintptr_t kImageScaleContinue = 0x0045dad8;
// Engine operator new (malloc-backed in this binary — AurSaveGameSnapshot
// allocates the source buffer with it and frees it with _free, so a buffer
// we return here is freed correctly by the caller's _free(pbVar5)).
constexpr uintptr_t kEngineOperatorNew  = 0x006fa7e6;

// The first 8 bytes of ImageScale — three whole instructions, none of them
// position-relative, so they relocate verbatim into the trampoline.
constexpr uint8_t kPrologue[8] = {
    0x83, 0xec, 0x30,        // SUB ESP, 0x30
    0x53,                    // PUSH EBX
    0x8b, 0x5c, 0x24, 0x4c,  // MOV EBX, [ESP+0x4c]
};

typedef void* (__cdecl* PFN_ImageScale)(unsigned char* src, int srcW, int srcH,
                                        int bpp, int dstW, int dstH);
typedef void* (__cdecl* PFN_OperatorNew)(size_t size);

// Trampoline that runs the relocated prologue then jumps to kImageScaleContinue
// — calling this is equivalent to calling the unhooked ImageScale.
PFN_ImageScale g_origImageScale = nullptr;

PFN_OperatorNew g_engineNew =
    reinterpret_cast<PFN_OperatorNew>(kEngineOperatorNew);

// Our replacement. Source area 0 → return a zeroed dest buffer instead of
// letting the engine divide by it; everything else passes straight through.
void* __cdecl ImageScaleGuard(unsigned char* src, int srcW, int srcH,
                              int bpp, int dstW, int dstH) {
    if (srcW > 0 && srcH > 0) {
        return g_origImageScale(src, srcW, srcH, bpp, dstW, dstH);
    }

    // Degenerate source (framebuffer capture empty — Frame Buffer Effects
    // off). The original divides by srcW*srcH here and #DE's. Hand back a
    // valid, zeroed destination buffer the exact size the original would
    // have produced (bpp*dstW*dstH); the caller copies it into a black
    // thumbnail and _free()s it. Save proceeds.
    long long bytes = static_cast<long long>(bpp) * dstW * dstH;
    if (bpp <= 0 || dstW <= 0 || dstH <= 0 ||
        bytes <= 0 || bytes > (64LL * 1024 * 1024)) {
        // Destination is also nonsensical — nothing safe to synthesize, and
        // we don't want to mask an unrelated bug. Let the original run.
        acclog::Write("SaveGuard", "ImageScale degenerate src %dx%d but dst "
            "%dx%dx%d also bad; passing through", srcW, srcH, dstW, dstH, bpp);
        return g_origImageScale(src, srcW, srcH, bpp, dstW, dstH);
    }

    void* buf = g_engineNew(static_cast<size_t>(bytes));
    if (!buf) {
        acclog::Write("SaveGuard", "ImageScale degenerate src %dx%d; "
            "operator_new(%lld) failed; passing through", srcW, srcH, bytes);
        return g_origImageScale(src, srcW, srcH, bpp, dstW, dstH);
    }
    std::memset(buf, 0, static_cast<size_t>(bytes));
    acclog::Write("SaveGuard", "ImageScale degenerate src %dx%d -> blank "
        "%dx%dx%d thumbnail (save would have divided by zero)",
        srcW, srcH, dstW, dstH, bpp);
    return buf;
}

void WriteRel32Jmp(uint8_t* at, uintptr_t target) {
    at[0] = 0xe9;
    int32_t rel = static_cast<int32_t>(
        target - (reinterpret_cast<uintptr_t>(at) + 5));
    std::memcpy(at + 1, &rel, 4);
}

}  // namespace

void InstallSaveScreenshotGuard() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    // Layout of the executable block we allocate:
    //   [0]  trampoline: 8 relocated prologue bytes + JMP kImageScaleContinue
    //   [13] wrapper stub: JMP [pWrapper]  (absolute-indirect, reaches the
    //        DLL wrapper regardless of load address)
    //   [19] pWrapper: 4-byte absolute address of ImageScaleGuard
    constexpr size_t kTrampOff   = 0;
    constexpr size_t kTrampLen   = sizeof(kPrologue) + 5;   // 8 + JMP rel32
    constexpr size_t kStubOff    = kTrampOff + kTrampLen;   // 13
    constexpr size_t kStubLen    = 6;                       // FF 25 disp32
    constexpr size_t kSlotOff    = kStubOff + kStubLen;     // 19
    constexpr size_t kBlockSize  = kSlotOff + 4;            // 23

    auto* block = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kBlockSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!block) {
        acclog::Write("SaveGuard",
            "save-crash guard install failed: VirtualAlloc returned NULL");
        return;
    }

    // Trampoline: relocated prologue, then JMP to entry+8.
    std::memcpy(block + kTrampOff, kPrologue, sizeof(kPrologue));
    WriteRel32Jmp(block + kTrampOff + sizeof(kPrologue), kImageScaleContinue);
    g_origImageScale = reinterpret_cast<PFN_ImageScale>(block + kTrampOff);

    // Wrapper stub: JMP DWORD PTR [pWrapper]. Absolute indirect so the DLL
    // wrapper is reachable even if it loaded far from the .text section.
    uint8_t* stub = block + kStubOff;
    uint8_t* slot = block + kSlotOff;
    stub[0] = 0xff;
    stub[1] = 0x25;
    uintptr_t slotAddr = reinterpret_cast<uintptr_t>(slot);
    std::memcpy(stub + 2, &slotAddr, 4);
    uintptr_t wrapperAddr = reinterpret_cast<uintptr_t>(&ImageScaleGuard);
    std::memcpy(slot, &wrapperAddr, 4);

    // Patch ImageScale's entry: JMP rel32 -> wrapper stub.
    void* entry = reinterpret_cast<void*>(kImageScaleAddr);
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        acclog::Write("SaveGuard",
            "save-crash guard install failed: VirtualProtect entry");
        return;
    }
    WriteRel32Jmp(static_cast<uint8_t*>(entry),
                  reinterpret_cast<uintptr_t>(block + kStubOff));
    DWORD ignored = 0;
    VirtualProtect(entry, 5, oldProtect, &ignored);

    FlushInstructionCache(GetCurrentProcess(), entry, 5);
    FlushInstructionCache(GetCurrentProcess(), block, kBlockSize);

    acclog::Write("SaveGuard",
        "save-crash guard installed (ImageScale @%p, trampoline @%p)",
        entry, block);
}

}  // namespace acc::save_guard
