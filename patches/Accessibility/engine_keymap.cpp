#include "engine_keymap.h"

#include <windows.h>
#include <cstdint>

#include "log.h"

namespace acc::engine_keymap {

namespace {

// One (engine command code -> physical-key VK) pair. A code can map to several
// VKs (e.g. an action with a primary + alternate physical key), so the table
// holds many rows per code.
struct Pair { int code; int vk; };

constexpr int kMaxPairs = 128;
Pair s_pairs[kMaxPairs];
int  s_count = 0;
bool s_built = false;

// ---- Engine hardcoded keyboard map -----------------------------------------
// The engine's in-world client handler (CClientExoAppInternal::HandleInputEvent)
// receives an internal "quick action" COMMAND code, then runs the bound action.
// These command codes are their OWN namespace — NOT the swkotor.ini [Keymapping]
// ActionNNN ids. Proven in patch-20260616-075935.log: pressing L emits command
// 221 and opens Quests, yet [Keymapping] Action221=DIK_O; pressing U emits 214
// (Equip), yet Action214=DIK_F4. So we map command -> DIK scancode from the
// engine's hardcoded keyboard handling (confirmed empirically), and resolve the
// scancode to a VK against the ACTIVE keyboard layout below. The shadowed
// gameplay hotkeys here are hardcoded in the engine (they are not exposed in the
// in-game Key Mapping screen — none of their scancodes appear in [Keymapping]),
// so they are stable regardless of the user's game keybinds.
//
// DIK scancodes (DIK == PS/2 set-1):
//   main-row 1..7 = 0x02..0x08; E=0x12 Q=0x10 J=0x24 U=0x16 L=0x26.
struct CodeScan { int code; int scancode; };
constexpr CodeScan kEngineCommands[] = {
    // Action-menu hotkeys — main-row digits. The 6/7 logical codes are swapped
    // (key 6 -> 0xee, key 7 -> 0xec); see input_pipeline.cpp's bare-key notes.
    {0xe2, 0x02},  // 1 -> DoTargetAction row 0
    {0xe4, 0x03},  // 2 -> row 1
    {0xe6, 0x04},  // 3 -> row 2
    {0xe8, 0x05},  // 4 -> DoPersonalAction col 0
    {0xea, 0x06},  // 5 -> col 1
    {0xee, 0x07},  // 6 -> col 2   (logical 6/7 swap)
    {0xec, 0x08},  // 7 -> col 3
    // Quick-menu screen hotkeys (confirmed command codes). Only L is shadowed by
    // a default mod hotkey (Shift+L = LevelUpOpen); J/U are included for the
    // upcoming free-config conflict checker and are inert for consume until a
    // mod binding lands on them. K/M/O/P/I codes are not yet confirmed — they
    // surface in the Diag.ModShadow "not consumed" log when pressed.
    {213,  0x24},  // J — Messages (Nachrichten)
    {214,  0x16},  // U — Equip (Ausrüstung)
    {221,  0x26},  // L — Quests (Aufträge)
    // Target cycle — bound to Q/E with NO modifier by the mod (so never
    // consumed), listed for completeness / the free-config checker.
    {204,  0x12},  // E — cycle right
    {205,  0x10},  // Q — cycle left
};

// Resolve a DirectInput scancode to a Win32 VK against the active keyboard
// layout. Scancodes are physical (DIK == PS/2 set-1), so the layout-aware
// conversion yields the VK that GetAsyncKeyState reports for the same physical
// key — the basis of QWERTY/QWERTZ/AZERTY correctness (both the engine code and
// the mod's VK binding resolve to the same physical key on the user's layout).
int ScancodeToVk(int scancode) {
    if (scancode <= 0) return 0;
    HKL layout = GetKeyboardLayout(0);
    UINT vk = MapVirtualKeyEx(static_cast<UINT>(scancode),
                              MAPVK_VSC_TO_VK_EX, layout);
    if (vk == 0) {
        vk = MapVirtualKey(static_cast<UINT>(scancode), MAPVK_VSC_TO_VK);
    }
    return static_cast<int>(vk);
}

void AddPair(int code, int vk) {
    if (vk == 0 || s_count >= kMaxPairs) return;
    for (int i = 0; i < s_count; ++i) {
        if (s_pairs[i].code == code && s_pairs[i].vk == vk) return;  // dedup
    }
    s_pairs[s_count].code = code;
    s_pairs[s_count].vk   = vk;
    ++s_count;
}

}  // namespace

void Rebuild() {
    s_count = 0;
    for (const CodeScan& cs : kEngineCommands) {
        AddPair(cs.code, ScancodeToVk(cs.scancode));
    }
    s_built = true;
    acclog::Write("EngineKeymap",
                  "table built: %d (command -> vk) pairs (active layout)",
                  s_count);
    for (int i = 0; i < s_count; ++i) {
        acclog::Write("EngineKeymap", "  command %d (0x%x) -> vk 0x%02x",
                      s_pairs[i].code, s_pairs[i].code, s_pairs[i].vk);
    }
}

int VksForCode(int code, int* out, int cap) {
    if (!s_built) Rebuild();
    int n = 0;
    for (int i = 0; i < s_count && n < cap; ++i) {
        if (s_pairs[i].code != code) continue;
        bool dup = false;
        for (int j = 0; j < n; ++j) {
            if (out[j] == s_pairs[i].vk) { dup = true; break; }
        }
        if (!dup) out[n++] = s_pairs[i].vk;
    }
    return n;
}

int CodeForVk(int vk) {
    if (!s_built) Rebuild();
    for (int i = 0; i < s_count; ++i) {
        if (s_pairs[i].vk == vk) return s_pairs[i].code;
    }
    return 0;
}

}  // namespace acc::engine_keymap
