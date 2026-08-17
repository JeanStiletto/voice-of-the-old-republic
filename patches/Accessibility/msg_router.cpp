#include "msg_router.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "engine_offsets.h"   // CExoString, Vector
#include "engine_area.h"      // IsLoadingSaveGame — engine save-load flag
#include "engine_player.h"    // GetPlayerPosition — world-live replay gate
#include "log.h"
#include "menus_listbox.h"    // RegisterPickerMsgRules — equip-preview suppression
#include "prism.h"
#include "engine_game.h"

namespace acc::msg {

Router& Router::Instance() {
    static Router inst;
    return inst;
}

void Router::AddRule(const char* name, RuleFn fn) {
    if (rule_count_ >= kMaxRules) {
        acclog::Write(log_tag_, "rule table full, dropping: %s",
                      name ? name : "(unnamed)");
        return;
    }
    rules_[rule_count_].name = name;
    rules_[rule_count_].fn   = fn;
    ++rule_count_;
}

void Router::AddOnUnmatched(OnUnmatchedFn fn) {
    if (unmatched_count_ >= kMaxUnmatched) {
        acclog::Write(log_tag_, "unmatched table full, dropping");
        return;
    }
    unmatched_[unmatched_count_++] = fn;
}

void Router::SetLogTag(const char* tag) {
    if (tag && *tag) log_tag_ = tag;
}

void Router::Dispatch(const char* text) {
    LogRaw(text);
    for (int i = 0; i < rule_count_; ++i) {
        if (rules_[i].fn && rules_[i].fn(text)) return;
    }
    for (int i = 0; i < unmatched_count_; ++i) {
        if (unmatched_[i]) unmatched_[i](text);
    }
    Speak(text);
}

void Router::Speak(const char* text) {
    // Single seam for backend selection. Future: priority routing
    // (interrupt for crits, Prism backchannel for urgent lines, etc.)
    // lives here.
    prism::Speak(text, /*interrupt=*/false);
}

void Router::LogRaw(const char* text) {
    acclog::Write(log_tag_, "raw: [%.300s]", text);
}

void Router::LogEmit(const char* subTag, const char* text) {
    acclog::Write(log_tag_, "%s: [%.500s]",
                  subTag ? subTag : "emit", text);
}

}  // namespace acc::msg

// ============================================================================
// CGuiInGame::AppendToMsgBuffer @0x0062b5c0 — hook entry.
//
// At hook entry:
//   ECX     = this (CGuiInGame*) — ignored by this handler
//   [ESP+4] = param_1 (CExoString*) — the row text to append
//
// `source = "esp+4"` emits LEA per project_kpatchmanager_lea_bug.md, so
// the handler receives the *address* of the stack slot and dereferences
// once to get the CExoString*.
//
// All combat-block parsing and category routing happens through
// acc::msg::Router. Rules register lazily on first fire — combat rules
// live in combat.cpp, future heal/loot/etc. rules will live in their
// own modules.

namespace { void EnsureRulesRegistered(); }

extern "C" void __cdecl OnAppendToMsgBuffer(void* /*guiInGame*/,
                                            void* esp_param1_addr) {
    // Runs on both games since Batch 2. KOTOR 2 reaches it via
    // OnAppendToMsgBufferK2 below — hooked on BOTH of KOTOR 2's category
    // rings (0x007BE090 / 0x007BE1B0), which together are the split twin of
    // KOTOR 1's single buffer. The save-load replay gate's chain
    // (IsLoadingSaveGame, GetPlayerPosition) is K2-resolved: GetLoadFromSave-
    // Game via the facade-cluster alignment, the player position via
    // GetPlayerCreature + the engine's own GetServerCreature.
    EnsureRulesRegistered();

    CExoString* exoStr = nullptr;
    __try {
        if (esp_param1_addr) {
            exoStr = *reinterpret_cast<CExoString**>(esp_param1_addr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (!exoStr) {
        acclog::Write("MsgBuf", "fire: param=null");
        return;
    }

    const char* cstr   = nullptr;
    uint32_t    length = 0;
    __try {
        cstr   = exoStr->c_string;
        length = exoStr->length;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (!cstr || length == 0) {
        acclog::Write("MsgBuf", "fire: empty (cstr=%p len=%u)",
                      cstr, length);
        return;
    }

    char text[512];
    size_t n = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
    __try {
        memcpy(text, cstr, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("MsgBuf", "fire: copy fault cstr=%p len=%u",
                      cstr, length);
        return;
    }
    text[n] = '\0';

    // Suppress the message-buffer REPLAY that floods on save-load. When the
    // engine restores a saved game it re-appends the entire persisted
    // feedback log — every historical combat result, loot pickup, XP gain
    // and journal entry — to the in-game message window, firing this hook
    // once per line. Speaking the whole history back is pure noise; the
    // player only wants to know which area they loaded into, which
    // transitions.cpp::SpeakArea already announces on the area-pointer
    // change.
    //
    // Primary gate: the engine's own load_from_savegame flag
    // (IsLoadingSaveGame), set across the whole load by CSWGuiSaveLoad::
    // LoadGame / DoQuickLoad — true for main-menu, in-game AND F9 quickload
    // restores, false during a plain save and normal play. This catches the
    // in-game case, where the old player creature lingers so GetPlayerPosition
    // still succeeds during the replay. The !GetPlayerPosition fallback covers
    // the main-menu load (no PC at all). Either way live combat/loot narration
    // is untouched. (Module/door transitions keep the same CGuiInGame and do
    // NOT replay, so this is a no-op there.)
    Vector scratch{};
    if (acc::engine::IsLoadingSaveGame() ||
        !acc::engine::GetPlayerPosition(scratch)) {
        acclog::Write("MsgBuf", "replay-suppressed (save-load in progress): "
                      "[%.200s]", text);
        return;
    }

    acc::msg::Router::Instance().Dispatch(text);
}

// Lazy registration. Each subsystem exposes a `Register*MsgRules()` function;
// we call them once on the first hook fire. This avoids DllMain-order
// concerns while still keeping the registration explicit.

namespace acc::combat { void RegisterCombatMsgRules(); }
namespace acc::locked_recall { void RegisterMsgRule(); }
namespace acc::endar { void RegisterMsgRule(); }

namespace {

void EnsureRulesRegistered() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    acc::msg::Router::Instance().SetLogTag("Combat.MsgBuf");
    // FIRST — first-match-wins. While the equip picker is open the engine
    // equips each row as you arrow onto it (its live preview) and reports the
    // inventory change here; that chatter must be claimed before any combat
    // rule or the raw-speech fallback sees it. See menus_listbox_picker.cpp.
    acc::menus::listbox::RegisterPickerMsgRules();
    acc::combat::RegisterCombatMsgRules();
    // Story-locked-object bark recall — notes the "This object is locked"
    // feedback (strref 1437), never consumes it. See locked_recall.h.
    acc::locked_recall::RegisterMsgRule();
    // Endar Spire tutorial door guidance — explains the locked doors of the
    // opening off the engine's own locked report, never consumes it. See
    // endar_softlock.h for why this hangs here and not on the interact path.
    acc::endar::RegisterMsgRule();
}

}  // namespace

// KOTOR 2 frame-unpacking wrapper (Batch 2). Both category-ring appends
// (0x007BE090 / 0x007BE1B0) hook this same function at entry+3, after the
// prologue stored nothing but the frame — `this` is still in ECX and the row
// text CExoString* sits at [EBP+8]. The handler expects the ADDRESS of that
// stack slot (KOTOR 1 esp+4 LEA semantics), which is exactly EBP+8.
// [EBP+0xC] = type, [EBP+0x10] = color — available if routing ever wants
// the category, unused today to keep the handler shared.
extern "C" void __cdecl OnAppendToMsgBufferK2(void* thisGui, void* ebp) {
    if (!acc::game::IsKotor2()) return;
    if (!thisGui || !ebp) return;
    OnAppendToMsgBuffer(thisGui, static_cast<char*>(ebp) + 8);
}
