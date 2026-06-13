#include "engine_picker.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_offsets.h"     // CExoString, kAddrAppManagerPtr,
                                // kAppManagerClientAppOffset,
                                // kClientExoAppInternalOffset
#include "engine_player.h"      // SetPlayerInputEnabled (auto-restore
                                // gate around the dispatch — same pattern
                                // guidance::UseObject uses) +
                                // GetPlayerCharacterName for the empty-
                                // descriptor diagnostic.
#include "engine_radial.h"      // ResolveTargetActionMenu, LogStateWide,
                                // LogTargetDiag — used around the empty-
                                // descriptor wrapper call to capture both
                                // the wrapper's TAM output and the input
                                // target's class+state so empty-rows logs
                                // are self-explanatory.
#include "log.h"

namespace {

// CClientExoAppInternal field offsets we need beyond what engine_player
// already declares. Sourced from the C header at
// docs/llm-docs/re/swkotor.exe.h (struct CClientExoAppInternal,
// 2026-04-29 export) and confirmed by decompile of HandleMouseClickInWorld
// + GetDefaultActions.
//
//   +0x040  CGuiInGame*           gui_in_game
//   +0x2b4  ulong                 last_target          (client-side)
//   +0x2b8  ulong                 last_clicked_on_target (client-side)
//   +0x4a4  ulong                 field283_0x4a4       (hover target;
//                                                       must equal
//                                                       last_target for
//                                                       the click gate
//                                                       to dispatch)
//   +0x4c8  CSWGuiInterfaceAction* descriptor_array     (first slot used
//                                                        by HandleMouse-
//                                                        ClickInWorld)
//   +0x4cc  int                   descriptor_count     (gate: must be
//                                                        > 0 to dispatch)
constexpr size_t kInternalGuiInGameOffset           = 0x040;
constexpr size_t kInternalLastTargetOffset          = 0x2b4;
constexpr size_t kInternalLastClickedOnTargetOffset = 0x2b8;
constexpr size_t kInternalHoverTargetOffset         = 0x4a4;
constexpr size_t kInternalDescriptorArrayOffset     = 0x4c8;
constexpr size_t kInternalDescriptorCountOffset     = 0x4cc;

// CGuiInGame.main_interface offset (CSWGuiMainInterface*). Counted from the
// CGuiInGame struct in swkotor.exe.h (line 10256: main_interface comes
// after a long pointer table starting at field0_0x0).
constexpr size_t kGuiInGameMainInterfaceOffset = 0x90;

// CSWGuiInterfaceAction layout (decompile of GetDefaultActions writes,
// + struct in swkotor.exe.h line 5437). Stride 0x38 between entries.
constexpr size_t kInterfaceActionLabelOffset    = 0x00;  // CExoString
constexpr size_t kInterfaceActionIdOffset       = 0x08;  // ulong
constexpr size_t kInterfaceActionFnOffset       = 0x0c;  // void*
constexpr size_t kInterfaceActionTargetOffset   = 0x1c;  // ulong
constexpr size_t kInterfaceActionIconOffset     = 0x20;  // CResRef (16B)
constexpr size_t kInterfaceActionStride         = 0x38;
constexpr size_t kResRefMaxLen                  = 16;

// Engine entry points. Addresses confirmed from XML symbol table
// (k1_win_gog_swkotor.exe.xml) on 2026-05-05; GoG bytes match Steam
// per memory project_ghidra_gog_steam_bytes_match.
constexpr uintptr_t kAddrCClientExoAppInternalGetDefaultActions      = 0x00620620;
constexpr uintptr_t kAddrCClientExoAppInternalHandleMouseClickInWorld = 0x00620350;
constexpr uintptr_t kAddrCGuiInGameSetMainInterfaceTarget            = 0x0062b000;
// CSWGuiMainInterface::PopulateMenus @0x00689d80 — builds the radial
// action menu. Called by HandleMouseClickInWorld's NOT-MATCH branch
// (vanilla first-click-on-target behavior). We invoke it directly when
// the engine descriptor is empty (no default action available) so the
// user gets the same fallback UI a sighted player would see.
constexpr uintptr_t kAddrCSWGuiMainInterfacePopulateMenus            = 0x00689d80;

typedef void (__thiscall* PFN_GetDefaultActions)(void* this_);
typedef void (__thiscall* PFN_HandleMouseClickInWorld)(void* this_);
typedef void (__thiscall* PFN_SetMainInterfaceTarget)(void* this_,
                                                     uint32_t target);
typedef void (__thiscall* PFN_PopulateMenus)(void* this_);

void* GetClientExoApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetClientExoAppInternal(void* exoApp) {
    if (!exoApp) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(exoApp) +
            kClientExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetGuiInGame(void* internal) {
    if (!internal) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kInternalGuiInGameOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetMainInterface(void* guiInGame) {
    if (!guiInGame) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(guiInGame) +
            kGuiInGameMainInterfaceOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void WriteUInt32(void* base, size_t offset, uint32_t value) {
    if (!base) return;
    __try {
        *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + offset) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Engine state moved out from under us mid-call — caller's
        // observable signal is the absent log line on the next read,
        // not a fatal here.
    }
}

uint32_t ReadUInt32(void* base, size_t offset) {
    if (!base) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Copy a null-terminated string from src into dst (capacity bytes
// including terminator) under SEH. Returns true if the copy completed
// without faulting; false otherwise (dst[0] = '\0' on fault).
bool CopyCStringSafe(const char* src, char* dst, size_t cap) {
    if (cap == 0) return false;
    dst[0] = '\0';
    if (!src) return false;
    __try {
        size_t i = 0;
        for (; i + 1 < cap; ++i) {
            char c = src[i];
            dst[i] = c;
            if (c == '\0') return true;
        }
        dst[i] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = '\0';
        return false;
    }
}

// Read an inline CResRef (a 16-byte fixed-buffer string used for
// engine resource identifiers like "i_dialog"). Treated as
// NUL-terminated within the 16-byte window.
void ReadResRef(void* base, size_t offset, char* out, size_t outCap) {
    if (outCap == 0) return;
    out[0] = '\0';
    if (!base) return;
    __try {
        const char* src = reinterpret_cast<const char*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        size_t lim = outCap - 1;
        if (lim > kResRefMaxLen) lim = kResRefMaxLen;
        size_t i = 0;
        for (; i < lim; ++i) {
            char c = src[i];
            out[i] = c;
            if (c == '\0') return;
        }
        out[i] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
}

// Read a CExoString (c_string + length pair). Falls back to "" on any
// fault. Truncates into the caller's buffer.
void ReadExoString(void* base, size_t offset, char* out, size_t outCap) {
    if (outCap == 0) return;
    out[0] = '\0';
    if (!base) return;
    __try {
        auto* es = reinterpret_cast<CExoString*>(
            reinterpret_cast<unsigned char*>(base) + offset);
        if (!es->c_string) return;
        CopyCStringSafe(es->c_string, out, outCap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
    }
}

// Snapshot the descriptor at internal+0x4c8. Sets snap.valid only if
// the pointer is non-null AND count > 0.
void SnapshotDescriptor(void* internal, acc::picker::ActionSnapshot* snap) {
    std::memset(snap, 0, sizeof(*snap));
    if (!internal) return;

    uint32_t descPtr = ReadUInt32(internal, kInternalDescriptorArrayOffset);
    int      count   = static_cast<int>(
        ReadUInt32(internal, kInternalDescriptorCountOffset));
    snap->count = count;

    if (descPtr == 0 || count <= 0) return;

    void* action = reinterpret_cast<void*>(static_cast<uintptr_t>(descPtr));
    snap->action_id = ReadUInt32(action, kInterfaceActionIdOffset);
    snap->target_id = ReadUInt32(action, kInterfaceActionTargetOffset);
    ReadExoString(action, kInterfaceActionLabelOffset,
                  snap->label, sizeof(snap->label));
    ReadResRef(action, kInterfaceActionIconOffset,
               snap->icon, sizeof(snap->icon));
    snap->valid = true;
}

}  // namespace

namespace acc::picker {

bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot,
           bool forceRadial) {
    ActionSnapshot localSnap = {};

    if (targetServerHandle == 0u || targetServerHandle == 0xFFFFFFFFu ||
        targetServerHandle == kInvalidObjectId) {
        if (outSnapshot) *outSnapshot = localSnap;
        return false;
    }

    // server → client conversion: server ids have the high bit clear
    // (per memory project_object_handle_namespaces); the cursor system
    // uses the OR'd form. If the caller already passed a client handle
    // (high bit set) we leave it untouched.
    uint32_t targetClient =
        (targetServerHandle & 0x80000000u) ? targetServerHandle
                                           : (targetServerHandle | 0x80000000u);

    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    void* guiIn    = GetGuiInGame(internal);
    void* mainIf   = GetMainInterface(guiIn);

    if (!internal || !guiIn) {
        acclog::Write("Picker", "chain unresolved (target=0x%08x exoApp=%p "
            "internal=%p guiIn=%p mainIf=%p)",
            targetClient, exoApp, internal, guiIn, mainIf);
        if (outSnapshot) *outSnapshot = localSnap;
        return false;
    }

    // Step 1 — point CSWGuiMainInterface.field1_0x64 at our target.
    // GetDefaultActions reads main_interface->field1_0x64 to know
    // which target to compute actions for.
    if (mainIf) {
        __try {
            auto fn = reinterpret_cast<PFN_SetMainInterfaceTarget>(
                kAddrCGuiInGameSetMainInterfaceTarget);
            // SetMainInterfaceTarget takes the CGuiInGame*, not the
            // main_interface — the wrapper resolves through this.
            fn(guiIn, targetClient);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("Picker", "SetMainInterfaceTarget SEH-FAULT target=0x%08x",
                targetClient);
            if (outSnapshot) *outSnapshot = localSnap;
            return false;
        }
    } else {
        acclog::Write("Picker", "main_interface null (in-world too early?); "
            "target=0x%08x — skipping Drive",
            targetClient);
        if (outSnapshot) *outSnapshot = localSnap;
        return false;
    }

    // Step 2 — populate +0x4c8 / +0x4cc by running the engine's own
    // descriptor builder against the target we just installed.
    __try {
        auto fn = reinterpret_cast<PFN_GetDefaultActions>(
            kAddrCClientExoAppInternalGetDefaultActions);
        fn(internal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Picker", "GetDefaultActions SEH-FAULT target=0x%08x",
            targetClient);
        if (outSnapshot) *outSnapshot = localSnap;
        return false;
    }

    // Step 3 — read back what the engine picked. Useful for the log
    // and for refined pre-roll narration even when we don't end up
    // dispatching.
    SnapshotDescriptor(internal, &localSnap);

    if (!localSnap.valid || forceRadial) {
        // Diagnostic — capture leader name + target class/state BEFORE
        // calling the wrapper. Lets us tell, when action_lists comes back
        // empty, whether the target is in a "no actions remaining" final
        // state (open door, used placeable) or in a state where the engine
        // *should* have surfaced something but didn't (locked door, leader
        // skill mismatch). GetActiveLeaderName resolves the *currently-
        // controlled* party member — Tab-swapping to Trask is reflected
        // here, unlike GetPlayerCharacterName which always returns the
        // PC's chargen name.
        const char* tag = forceRadial ? "force-radial diag"
                                      : "empty-descriptor diag";
        char leaderName[64] = "";
        acc::engine::GetActiveLeaderName(leaderName, sizeof(leaderName));
        acclog::Write("Picker", "%s — leader=[%s] target=0x%08x descriptor_count=%d",
            tag, leaderName[0] ? leaderName : "?", targetClient,
            localSnap.count);
        acc::engine_radial::LogTargetDiag(targetClient, "before-wrapper");

        // Engine has no default action for this target (count==0).
        // Vanilla mouse flow at this point opens the radial menu via
        // HandleMouseClickInWorld's NOT-MATCH branch, which calls
        // CSWGuiMainInterface::PopulateMenus(main_interface) — no args.
        // We do the same. The wrapper's decompile (Decompile.java run
        // 2026-05-05, see docs/radial-menu-investigation.md) shows that
        // it internally:
        //   1. Resolves the player creature via GetSWParty/GetPlayerCharacter.
        //   2. Resolves the target object via GetGameObject(field1_0x64)
        //      AND downcasts CGameObject* -> CSWCObject* via
        //      vtable->AsSWCObject(). This downcast is the missing piece
        //      that broke our previous direct inner-PopulateMenus calls.
        //   3. Refills field5_0x74[0..5] (six personal-action lists on
        //      the main interface) via CSWCCreature::GetPersonalActions.
        //   4. Calls inner CSWGuiTargetActionMenu::PopulateMenus(player,
        //      mode, swc_target, &result) with the correct types. Inner
        //      then refills target_action_menu.action_lists[0..2] via
        //      CSWCCreature::GetTargetActions(player, swc_target, row).
        //   5. Updates main_interface UI rows from the personal-action lists.
        // So the wrapper alone produces the correct populated state.
        bool radialOpened = false;
        __try {
            auto fn = reinterpret_cast<PFN_PopulateMenus>(
                kAddrCSWGuiMainInterfacePopulateMenus);
            fn(mainIf);
            radialOpened = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("Picker", "PopulateMenus SEH-FAULT target=0x%08x "
                "(empty-descriptor radial fallback)",
                targetClient);
        }

        // Synchronous wide diagnostic — captures the wrapper's output so
        // we can verify action_lists[] populated correctly without
        // depending on a next-frame OnUpdate tick that might never come.
        void* dtam = acc::engine_radial::ResolveTargetActionMenu();
        if (dtam) {
            acc::engine_radial::LogStateWide(dtam, "after-wrapper");
        }

        localSnap.radial_opened = radialOpened;
        if (outSnapshot) *outSnapshot = localSnap;

        if (radialOpened) {
            acclog::Write("Picker", "%s — opened radial via PopulateMenus "
                "(target=0x%08x descriptor_count=%d)",
                forceRadial ? "force-radial"
                            : "empty descriptor",
                targetClient, localSnap.count);
        } else {
            acclog::Write("Picker", "%s (target=0x%08x descriptor_count=%d) and "
                "PopulateMenus faulted",
                forceRadial ? "force-radial requested"
                            : "GetDefaultActions returned empty descriptor",
                targetClient, localSnap.count);
        }
        return radialOpened;
    }
    if (outSnapshot) *outSnapshot = localSnap;

    acclog::Write("Picker", "descriptor populated target=0x%08x action_id=0x%x "
        "label=[%s] icon=[%s] count=%d",
        targetClient, localSnap.action_id,
        localSnap.label, localSnap.icon, localSnap.count);

    // Step 4 — set the engine's click gate so HandleMouseClickInWorld
    // takes the dispatch branch instead of the PopulateMenus branch.
    // All three slots must hold the same value AND last_clicked_on_target
    // must be != INVALID. We write directly (engine helpers like
    // SetLastTarget add side flags we don't want to bias).
    WriteUInt32(internal, kInternalLastTargetOffset,          targetClient);
    WriteUInt32(internal, kInternalLastClickedOnTargetOffset, targetClient);
    WriteUInt32(internal, kInternalHoverTargetOffset,         targetClient);

    // Disable the per-tick player-input movement clobber for the duration of
    // whatever the engine action enqueues — walk-to-then-use, attack, … share
    // the same composite shape. TickPlayerInputRestore flips control back when
    // the action queue drains (or the walk stalls); on any SEH fault below we
    // restore immediately. A repeat dispatch (e.g. mashing Enter) re-issues the
    // engine action but does not extend the restore window — see the no-re-arm
    // guard in SetPlayerInputEnabled (that was the janicebug livelock).
    //
    // EXCEPTION — dialog (action 0x3ea): we do NOT disable input. Verified by
    // decompile + in-game (docs/llm-docs/interaction-dispatch-model.md): the
    // engine's native walk-then-talk (server input case 8 → SetAILevel(player,1)
    // → AIActionDialogObject, which AddMoveToPointActionToFront's the player when
    // >10 m out) only moves the PC with input left ENABLED; disabling it (→
    // SwitchMode(player,0)) suppressed the approach and produced the ~4 s
    // distant-talk freeze (project_distant_npc_dialogue_stuck). Because we leave
    // input enabled here there is no TickPlayerInputRestore session for talk;
    // the interact feature arms its own dialog-approach watchdog instead (it
    // breaks the rare livelock where the target is reachable on screen but no
    // walkable point lands within conversation range — e.g. an NPC behind a
    // railing — and announces "way blocked").
    constexpr uint32_t kActionIdDialog = 0x3ea;  // GetDefaultActions talk verb
    const bool skipInputDisable = (localSnap.action_id == kActionIdDialog);
    bool inputDisabled = false;
    if (!skipInputDisable) {
        inputDisabled = acc::engine::SetPlayerInputEnabled(false);
    }

    // Step 5 — actual dispatch.
    bool dispatched = true;
    __try {
        auto fn = reinterpret_cast<PFN_HandleMouseClickInWorld>(
            kAddrCClientExoAppInternalHandleMouseClickInWorld);
        fn(internal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dispatched = false;
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("Picker", "HandleMouseClickInWorld SEH-FAULT target=0x%08x "
            "action_id=0x%x label=[%s]",
            targetClient, localSnap.action_id, localSnap.label);
    }

    if (dispatched) {
        acclog::Write("Picker", "HandleMouseClickInWorld dispatched target=0x%08x "
            "action_id=0x%x label=[%s] (input_disabled=%d)",
            targetClient, localSnap.action_id, localSnap.label,
            inputDisabled ? 1 : 0);
    }

    return dispatched;
}

bool ReanchorRadial(uint32_t targetServerHandle) {
    if (targetServerHandle == 0u || targetServerHandle == 0xFFFFFFFFu ||
        targetServerHandle == kInvalidObjectId) {
        return false;
    }
    uint32_t targetClient =
        (targetServerHandle & 0x80000000u) ? targetServerHandle
                                           : (targetServerHandle | 0x80000000u);

    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    void* guiIn    = GetGuiInGame(internal);
    void* mainIf   = GetMainInterface(guiIn);
    if (!internal || !guiIn || !mainIf) return false;

    // Same three engine calls as Drive's force-radial branch, minus the
    // diagnostics — this runs on every radial keypress, so it must stay
    // quiet. SetMainInterfaceTarget points field1_0x64 at our target;
    // GetDefaultActions + PopulateMenus rebuild the target-action menu for
    // it, overwriting whatever the cursor's last mouse-move left there.
    __try {
        auto setTgt = reinterpret_cast<PFN_SetMainInterfaceTarget>(
            kAddrCGuiInGameSetMainInterfaceTarget);
        setTgt(guiIn, targetClient);

        auto getActions = reinterpret_cast<PFN_GetDefaultActions>(
            kAddrCClientExoAppInternalGetDefaultActions);
        getActions(internal);

        auto populate = reinterpret_cast<PFN_PopulateMenus>(
            kAddrCSWGuiMainInterfacePopulateMenus);
        populate(mainIf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Picker", "ReanchorRadial SEH-FAULT target=0x%08x",
            targetClient);
        return false;
    }
    return true;
}

bool ReadCurrent(ActionSnapshot* outSnapshot) {
    ActionSnapshot localSnap = {};
    void* exoApp   = GetClientExoApp();
    void* internal = GetClientExoAppInternal(exoApp);
    SnapshotDescriptor(internal, &localSnap);
    if (outSnapshot) *outSnapshot = localSnap;
    return localSnap.valid;
}

}  // namespace acc::picker
