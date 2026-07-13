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

// ---- Movement / turn axis buckets ------------------------------------------
// The player's bound movement + turn keys, resolved from swkotor.ini
// [Keymapping] alongside the flat game-VK set above. These let the mod detect
// "the player is moving / turning" and steer the map cursor by the SAME keys
// the player uses in the world — no matter which physical keys those are
// (so nothing breaks when turn is rebound off A/D).
//
// The [Keymapping] movement/turn actions come in slot pairs "Action<id>A" /
// "Action<id>B". Verified against the stock kotor.ini defaults, slot 'A' of
// each action group is the "negative" direction and slot 'B' the "positive":
//   move  Action280/282 : A=W (forward)  B=S (back)    | Action285 : A=Up B=Down
//   turn  Action283/284 : A=A-key (left) B=D (right)   | Action286 : A=Left B=Right
// A rebind changes the KEY inside a slot, not which slot is which direction,
// so reading a slot always yields that direction's current key.
constexpr int kMoveAxisCount = 4;  // Forward, Backward, TurnLeft, TurnRight
constexpr int kMaxAxisVks    = 8;
int s_axisVks[kMoveAxisCount][kMaxAxisVks];
int s_axisVkCount[kMoveAxisCount] = {0, 0, 0, 0};

// The DIK scancode camera_orient synthesises to drive the engine's turn axis,
// per direction [0]=left [1]=right. Captured from the configured primary turn
// binds (Action283/284) so a rebind is honoured; 0 = not configured → the
// A/D DIK fallback in TurnScancode(). Scancode (not VK) because the engine
// reads keyboard via DirectInput, which sees scancodes only.
int s_turnScan[2] = {0, 0};

// Default VK per axis (WASD) — always seeded so the buckets are never empty,
// even before the ini is read or if it is missing. Index matches MoveAxis.
constexpr int kAxisDefaultVk[kMoveAxisCount] = {'W', 'S', 'A', 'D'};

// (axis, [Keymapping] action id, slot letter) contributors.
struct AxisContrib { int axis; int actionId; char slot; };
constexpr AxisContrib kAxisContribs[] = {
    {0, 280, 'A'}, {0, 282, 'A'}, {0, 285, 'A'},   // Forward  : W / Up
    {1, 280, 'B'}, {1, 282, 'B'}, {1, 285, 'B'},   // Backward : S / Down
    {2, 283, 'A'}, {2, 284, 'A'}, {2, 286, 'A'},   // TurnLeft : A / Left
    {3, 283, 'B'}, {3, 284, 'B'}, {3, 286, 'B'},   // TurnRight: D / Right
};

bool IsDownVk(int vk) {
    return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void AddAxisVk(int axis, int vk) {
    if (vk == 0 || axis < 0 || axis >= kMoveAxisCount) return;
    if (s_axisVkCount[axis] >= kMaxAxisVks) return;
    for (int i = 0; i < s_axisVkCount[axis]; ++i) {
        if (s_axisVks[axis][i] == vk) return;  // dedup
    }
    s_axisVks[axis][s_axisVkCount[axis]++] = vk;
}

// Engine InputIndices value -> DIK (PS/2 set-1) scancode, DIRECTLY. Both are
// physical-key identifiers, so this is layout-independent — unlike routing
// through InputIndexToVk + the active layout, which on QWERTZ swaps Y/Z (turn
// bound to physical Z came out as physical Y and drove nothing). DirectInput,
// which the engine polls for the turn axis, wants the physical scancode. Covers
// the keys a turn bind realistically uses (letters, digits, space); returns 0
// for anything else so the caller can fall back.
int InputIndexToScancode(int ii) {
    // A..Z (InputIndex 0x33..0x4c) → DIK by physical position (not alphabetical).
    static const unsigned char kLetterDik[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
        0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
        0x15, 0x2C};
    if (ii >= 0x33 && ii <= 0x4c) return kLetterDik[ii - 0x33];  // A..Z
    if (ii >= 0x4d && ii <= 0x55) return 0x02 + (ii - 0x4d);     // 1..9
    if (ii == 0x56)               return 0x0B;                   // 0
    if (ii == 0x57)               return 0x39;                   // Space
    return 0;
}

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
    // Seed the movement/turn axis buckets with the WASD defaults BEFORE any
    // early-out below, so the buckets are never empty even if the ini is
    // missing/unreadable. Configured binds are layered on top during the parse.
    for (int a = 0; a < kMoveAxisCount; ++a) {
        s_axisVkCount[a] = 0;
        AddAxisVk(a, kAxisDefaultVk[a]);
    }
    s_turnScan[0] = 0;
    s_turnScan[1] = 0;
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
        int vk = InputIndexToVk(inputIndex);
        AddGameVk(vk);

        // LHS form "Action<digits>[A|B]" — parse the action id and slot so we
        // can route movement/turn binds into the directional axis buckets.
        int  actionId = 0;
        char slot     = 0;
        if (_strnicmp(p, "Action", 6) == 0) {
            const char* q = p + 6;
            while (*q >= '0' && *q <= '9') { actionId = actionId * 10 + (*q - '0'); ++q; }
            if (q < eq && (*q == 'A' || *q == 'a')) slot = 'A';
            else if (q < eq && (*q == 'B' || *q == 'b')) slot = 'B';
        }
        if (slot != 0) {
            for (const AxisContrib& c : kAxisContribs) {
                if (c.actionId != actionId || c.slot != slot) continue;
                AddAxisVk(c.axis, vk);
                // Capture the configured turn scancode for camera_orient's
                // synthetic-key drive. Only the A/D-family turn actions
                // (283/284) — skip the arrow alt (286) to avoid extended-
                // scancode SendInput handling. First configured value wins.
                // Convert straight from the InputIndex so the physical scancode
                // is layout-correct (the VK path swaps Y/Z on QWERTZ).
                if ((c.axis == 2 || c.axis == 3) &&
                    (c.actionId == 283 || c.actionId == 284)) {
                    int di = (c.axis == 2) ? 0 : 1;
                    int sc = InputIndexToScancode(inputIndex);
                    if (sc != 0 && s_turnScan[di] == 0) s_turnScan[di] = sc;
                }
            }
        }
    }
    fclose(f);
    acclog::Write("EngineKeymap",
                  "loaded %d configurable game bind VK(s) from swkotor.ini "
                  "(axis fwd=%d back=%d left=%d right=%d; turnScan L=0x%02x "
                  "R=0x%02x)",
                  s_gameVkCount, s_axisVkCount[0], s_axisVkCount[1],
                  s_axisVkCount[2], s_axisVkCount[3], s_turnScan[0],
                  s_turnScan[1]);
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

int MoveAxisVks(MoveAxis axis, int* out, int cap) {
    if (!s_gameLoaded) ReloadGameConfig();  // populates axis buckets
    int a = static_cast<int>(axis);
    if (a < 0 || a >= kMoveAxisCount || !out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < s_axisVkCount[a] && n < cap; ++i) {
        out[n++] = s_axisVks[a][i];
    }
    return n;
}

int TurnScancode(bool left) {
    if (!s_gameLoaded) ReloadGameConfig();
    int idx = left ? 0 : 1;
    if (s_turnScan[idx] != 0) return s_turnScan[idx];
    return left ? 0x1E : 0x20;  // DIK A / D fallback (vanilla / ini unreadable)
}

bool AnyMovementKeyHeld() {
    if (!s_gameLoaded) ReloadGameConfig();
    // The four directional buckets (bound move/turn keys + WASD defaults)…
    for (int a = 0; a < kMoveAxisCount; ++a) {
        for (int i = 0; i < s_axisVkCount[a]; ++i) {
            if (IsDownVk(s_axisVks[a][i])) return true;
        }
    }
    // …plus the legacy extras the German QWERTZ set relies on that aren't in
    // the directional groups (the Z/C axis + the physical-Z VK 'Y'). Kept so
    // this never regresses below the old hardcoded {W,S,A,D,C,Y} check.
    static const int kExtra[] = {'C', 'Y', 'Z'};
    for (int vk : kExtra) {
        if (IsDownVk(vk)) return true;
    }
    return false;
}

}  // namespace acc::engine_keymap
