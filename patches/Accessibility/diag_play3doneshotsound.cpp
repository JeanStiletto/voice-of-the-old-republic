#include "diag_play3doneshotsound.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "log.h"

// CExoSound::Play3DOneShotSound — instrumentation hook @0x005D5E16.
//
// Hook-entry stack frame (no pushes since function entry; the prologue
// MOV ECX,[ECX] / TEST ECX,ECX / JZ +0x4b doesn't touch the stack):
//   [esp+0]    return address       ← caller EIP
//   [esp+4]    CResRef* res
//   [esp+8]    position.x
//   [esp+0xc]  position.y
//   [esp+0x10] position.z
//   [esp+0x14] z_offset
//   [esp+0x18] priority_group
//   [esp+0x1c] delay_ms
//   [esp+0x20] looping
//   [esp+0x24] volume
//   [esp+0x28] max_distance
//
// Per memory entry `project_kpatchmanager_lea_bug.md`, the framework
// emits LEA (not MOV) for `source = "esp+X"` parameters, so `arg_addr`
// is the *address* of the [esp+4] slot — deref once to get CResRef*,
// and read (uint32_t*)arg_addr - 1 to grab [esp+0] (the return EIP).
//
// CResRef is { char string[16]; } — same layout audio_bus.cpp uses.
// Strings may be NUL-padded inside the 16 bytes, hence the bounded copy.
extern "C" void __cdecl OnPlay3DOneShotSound(void* arg_addr) {
    if (!arg_addr) return;

    char resref[17];
    std::memset(resref, 0, sizeof(resref));
    uint32_t caller_eip = 0;

    __try {
        auto* slot4 = reinterpret_cast<uint32_t*>(arg_addr);
        caller_eip = *(slot4 - 1);                              // [esp+0]
        const char* res_ptr = reinterpret_cast<const char*>(*slot4);  // [esp+4]
        if (res_ptr) {
            for (int i = 0; i < 16; ++i) {
                char c = res_ptr[i];
                if (c == '\0') break;
                resref[i] = c;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Play3DOneShot: deref faulted (arg_addr=%p)", arg_addr);
        return;
    }

    acclog::Write("Play3DOneShot: caller=0x%08x resref=[%s]",
                  caller_eip, resref);
}
