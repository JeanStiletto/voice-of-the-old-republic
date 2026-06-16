#include "engine_keymap.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// Game-configurable bindings, read from swkotor.ini [Keymapping]. Each entry is
// "Action<id>=<DIK scancode>"; we resolve the scancode to a VK and keep the
// distinct set so the configurator can warn when a chosen bare key collides with
// the player's own game keybinds (not just the hardcoded quick keys above).
constexpr int kMaxGameVks = 256;
int  s_gameVks[kMaxGameVks];
int  s_gameVkCount = 0;
bool s_gameLoaded  = false;

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

void AddPair(int code, int vk) {
    if (vk == 0 || s_count >= kMaxPairs) return;
    for (int i = 0; i < s_count; ++i) {
        if (s_pairs[i].code == code && s_pairs[i].vk == vk) return;  // dedup
    }
    s_pairs[s_count].code = code;
    s_pairs[s_count].vk   = vk;
    ++s_count;
}

void AddGameVk(int vk) {
    if (vk == 0 || s_gameVkCount >= kMaxGameVks) return;
    for (int i = 0; i < s_gameVkCount; ++i) {
        if (s_gameVks[i] == vk) return;  // dedup
    }
    s_gameVks[s_gameVkCount++] = vk;
}

// Resolve <install>\swkotor.ini from acclog::PatchDir() (=<install>\patches) by
// stripping the last path component — the same derivation mod_settings_store
// uses. Returns false (empty out) until the patch dir is known.
bool ResolveIniPath(char* out, size_t cap) {
    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) return false;
    char buf[MAX_PATH];
    strncpy_s(buf, patchDir, _TRUNCATE);
    char* slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';  // <install>\patches -> <install>
    _snprintf_s(out, cap, _TRUNCATE, "%s\\swkotor.ini", buf);
    return true;
}

}  // namespace

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

int InputIndexToVk(int ii) {
    // KOTOR's keymap stores an `InputIndices` enum value (NOT a DIK scancode):
    // CExoInput::GetLastCapturedKeyboardKey returns one, the keymap row's
    // key_code holds one, and swkotor.ini [Keymapping] writes it in decimal.
    // Values from the InputIndices enum (k1_win_gog_swkotor.exe.xml). We map to
    // the Win32 VK the mod's GetAsyncKeyState bindings use. (Direct VK mapping:
    // exact on US layouts and for every physical key the mod actually binds;
    // the only ambiguity is the QWERTZ Y/Z swap, which no mod hotkey touches.)
    if (ii >= 0x33 && ii <= 0x4c) return 'A' + (ii - 0x33);  // KEYBOARD_A..Z
    if (ii >= 0x4d && ii <= 0x55) return '1' + (ii - 0x4d);  // KEYBOARD_1..9
    if (ii == 0x56)               return '0';                // KEYBOARD_0
    if (ii >= 0x27 && ii <= 0x32) return VK_F1  + (ii - 0x27);  // F1..F12
    if (ii >= 0x5b && ii <= 0x5d) return VK_F13 + (ii - 0x5b);  // F13..F15
    if (ii >= 0x0b && ii <= 0x13) return VK_NUMPAD1 + (ii - 0x0b);  // NUMPAD1..9
    switch (ii) {
    case 0x06: return VK_RETURN;       // KEYBOARD_RETURN
    case 0x07: return VK_LEFT;
    case 0x08: return VK_RIGHT;
    case 0x09: return VK_UP;
    case 0x0a: return VK_DOWN;
    case 0x14: return VK_NUMPAD0;
    case 0x15: return VK_DECIMAL;      // NUMPADDECIMAL
    case 0x16: return VK_SUBTRACT;     // NUMPADMINUS
    case 0x17: return VK_ADD;          // NUMPADPLUS
    case 0x18: return VK_LSHIFT;
    case 0x19: return VK_RSHIFT;
    case 0x1a: return VK_LMENU;
    case 0x1b: return VK_RMENU;
    case 0x1c: return VK_LCONTROL;
    case 0x1d: return VK_RCONTROL;
    case 0x1e: return VK_TAB;
    case 0x1f: return VK_ESCAPE;
    case 0x20: return VK_HOME;
    case 0x21: return VK_END;
    case 0x22: return VK_PRIOR;        // PAGEUP
    case 0x23: return VK_NEXT;         // PAGEDOWN
    case 0x24: return VK_INSERT;
    case 0x25: return VK_DELETE;
    case 0x26: return VK_SNAPSHOT;     // PRINTSCREEN
    case 0x57: return VK_SPACE;
    case 0x58: return VK_RETURN;       // NUMPADENTER
    case 0x59: return VK_CAPITAL;
    case 0x5a: return VK_PAUSE;
    case 0x5e: return VK_OEM_MINUS;
    case 0x5f: return VK_OEM_PLUS;     // EQUALS
    case 0x60: return VK_BACK;
    case 0x61: return VK_OEM_4;        // LBRACKET
    case 0x62: return VK_OEM_6;        // RBRACKET
    case 0x63: return VK_OEM_1;        // SEMICOLON
    case 0x64: return VK_OEM_7;        // APOSTROPHE
    case 0x65: return VK_OEM_3;        // GRAVE
    case 0x66: return VK_OEM_5;        // BACKSLASH
    case 0x67: return VK_OEM_COMMA;
    case 0x68: return VK_OEM_PERIOD;
    case 0x69: return VK_OEM_2;        // SLASH
    case 0x6a: return VK_MULTIPLY;
    case 0x6c: return VK_DIVIDE;
    case 0x6d: return VK_OEM_102;      // OEM_102 (<>| key)
    default:   return 0;
    }
}

void ReloadGameConfig() {
    s_gameVkCount = 0;
    s_gameLoaded  = true;  // mark loaded even on read failure: a missing ini just
                           // means no configurable binds to warn about (defaults
                           // are the hardcoded set, already covered).
    char path[MAX_PATH];
    if (!ResolveIniPath(path, sizeof(path))) {
        s_gameLoaded = false;  // patch dir unknown yet — retry on next query
        return;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        acclog::Write("EngineKeymap", "no swkotor.ini at %s; game binds unknown",
                      path);
        return;
    }
    bool inSection = false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Trim trailing whitespace / EOL.
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' '  || line[n - 1] == '\t')) {
            line[--n] = '\0';
        }
        // Skip leading whitespace.
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            inSection = (_strnicmp(p, "[Keymapping]", 12) == 0);
            continue;
        }
        if (!inSection) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        int inputIndex = atoi(eq + 1);   // RHS = decimal InputIndices value (key_code)
        AddGameVk(InputIndexToVk(inputIndex));
    }
    fclose(f);
    acclog::Write("EngineKeymap",
                  "loaded %d configurable game bind VK(s) from swkotor.ini",
                  s_gameVkCount);
}

bool IsKeyUsedByGame(int vk) {
    if (vk == 0) return false;
    if (CodeForVk(vk) != 0) return true;       // hardcoded quick keys
    if (!s_gameLoaded) ReloadGameConfig();
    for (int i = 0; i < s_gameVkCount; ++i) {
        if (s_gameVks[i] == vk) return true;
    }
    return false;
}

void Rebuild() {
    s_count = 0;
    for (const CodeScan& cs : kEngineCommands) {
        AddPair(cs.code, ScancodeToVk(cs.scancode));
    }
    s_built = true;
    ReloadGameConfig();  // also (re)load the player's configurable game binds
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
