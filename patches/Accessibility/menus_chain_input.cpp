// chain-navigation input handlers.
//
// Split out of menus_chain.cpp by the Phase-1 structure pass (refactoring
// candidate 2). menus_chain.cpp builds and maintains the chain (RebindChain
// and its validation/helper cluster); this file consumes input against an
// already-built chain. All five entry points were already declared
// individually in menus_chain.h, so the move is verbatim - no header
// change, no call-site change.
//
//   HandleEnterActivation  Enter / Shift+Enter on the chain cursor
//   WalkChildren           child-list walker (also the empty-chain probe)
//   HandleNavStep          Up / Down chain step + cursor warp
//   HandleLeftRight        Left / Right (cycle widgets, sliders, tabs)
//   HandleEsc              Esc back-out / modal close
//
// Handler ORDER is decided by the caller in menus_dispatch.cpp, not here.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_chain.h"
#include "engine_rebase.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_chargen_attr.h"
#include "menus_chargen_skills.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "menus_journal.h"
#include "menus_listbox.h"
#include "menus_modsettings.h"
#include "menus_monitors.h"
#include "menus_pending.h"
#include "menus_store.h"
#include "minigame_pazaak.h"
#include "peek_description.h"
#include "prism.h"
#include "strings.h"

namespace acc::menus::chain {

// Defined in menus_chain.cpp next to the chain-build code. Not in
// menus_chain.h because it is internal to the chain module - the Enter
// handler below is its only caller outside that file.
void SpeakLevelUpDoStepFirst();

void HandleEnterActivation(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputEnter1 && code != kInputEnter2) return;
    if (activePanel == nullptr) return;

    // Lazy rebind: previously Enter required a prior arrow press, which
    // stranded engine-pushed modals (StatusSummary after a skill check, the
    // quit-confirm MessageBox, AreaTransition prompts) where the chain was
    // bound to the previous panel. Mirroring HandleNavStep's rebind here lets
    // popups the engine pre-focuses (quit-confirm pre-focused on Abbrechen)
    // activate the focused button on Enter alone. RebindChain anchors
    // g_chainIndex on panel.activeControl when present.
    if (g_chainPanel != activePanel) {
        RebindChain(activePanel);
    }
    if (g_chainPanel != activePanel) return;          // rebind landed elsewhere
    if (g_chainCount <= 0) return;
    if (g_chainIndex < 0 || g_chainIndex >= g_chainCount) return;

    ChainEntry& e = g_chain[g_chainIndex];

    // Virtual chain entries route through their owning module BEFORE any
    // subclass-specific reads below — the entry's `control` field is a
    // sentinel pointer and any vtable / offset dereference would AV.
    // Currently only the mod-settings root entry; new virtual kinds add a
    // case here.
    if (e.virtualKind == kVirtualMod_SettingsRoot) {
        acc::menus::modsettings::OpenSubMenu(g_chainPanel);
        acclog::Write("Menus.Enter", "open mod-settings submenu (parent=%p)",
                      g_chainPanel);
        consumed = true;
        return;
    }

    bool isTabButton = false;
    if (g_tabbedPanel && g_tabsCount >= 2) {
        auto* tlist = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
        if (tlist && tlist->data) {
            for (int i = g_tabsStart;
                 i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
                if (tlist->data[i] == e.control) { isTabButton = true; break; }
            }
        }
    }

    // Detect equip-screen slot buttons up front. They need the full click
    // pipeline (cursor warp + LMouseDown/Up) to fire the engine's OnSelectSlot
    // — which is what populates LB_ITEMS with items matching the slot. Direct
    // vtable[15] activate on a slot button routes to a different handler
    // (likely OnEnterSlot, the keyboard shortcut path) that pops a "no items"
    // modal instead of populating the picker. Same gate-mismatch shape as
    // Options tab buttons: the mouse path is the only one that triggers the
    // populate.
    bool isEquipSlot = false;
    int  equipSlotCid = 0;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameEquip) {
        equipSlotCid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(e.control) + kControlIdOffset);
        isEquipSlot =
            equipSlotCid == kEquipBtnHeadId    || equipSlotCid == kEquipBtnImplantId ||
            equipSlotCid == kEquipBtnBodyId    || equipSlotCid == kEquipBtnArmLId    ||
            equipSlotCid == kEquipBtnArmRId    || equipSlotCid == kEquipBtnWeapLId   ||
            equipSlotCid == kEquipBtnWeapRId   || equipSlotCid == kEquipBtnBeltId    ||
            equipSlotCid == kEquipBtnHandsId;
    }

    // Workbench upgrade slot buttons (per-game .gui ids — see
    // IsWorkbenchUpgradeSlotButtonId). Same shape as equip-screen slot
    // buttons: direct vtable[15] activate doesn't populate LB_ITEMS with the
    // mods compatible with this slot — only the mouse-driven hover+click
    // pipeline does. We don't have an RE'd equivalent of
    // OnEnterSlot/OnSelectSlot for the workbench yet, so the safe path is a
    // full click-sim at the chain entry's extent center (mirrors the
    // tab-button activation pattern).
    bool isWorkbenchUpgradeSlot = false;
    int  workbenchUpgradeSlotCid = 0;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::WorkbenchUpgrade) {
        workbenchUpgradeSlotCid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(e.control) + kControlIdOffset);
        isWorkbenchUpgradeSlot =
            acc::engine::IsWorkbenchUpgradeSlotButtonId(workbenchUpgradeSlotCid);
    }

    // Store item row Enter — route to the engine's trade-action handler
    // (OnControlInvAButton / OnControlStoreAButton based on mode) instead of
    // the generic FireActivate. The default vtable[15] event 0x27 path just
    // refreshes the description listbox via OnControlEntered — never actually
    // sells or buys. Action buttons (Verkaufsliste / Schliess. / Kaufen) fall
    // through to the default activate path below; they're plain CSWGuiButton
    // instances, not CSWGuiStoreItemEntry rows.
    bool isStoreItemRow =
        acc::menus::store::IsStorePanel(g_chainPanel) &&
        acc::menus::store::IsStoreItemRow(e.control);

    // Journal quest-row Enter — read the description text. The row's own
    // activate handler is a no-op in the engine; the description text the
    // engine paints next to the list on mouse hover is the only signal a
    // sighted user gets, so we surface it on Enter instead.
    bool isJournalRow =
        acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameJournal &&
        acc::menus::journal::IsJournalEntry(e.control);

    // Quest-item row Enter — the QuestItem sub-screen ("Auftrags-Gegenstände")
    // lists plot items as CSWGuiInGameItemEntry rows with no meaningful
    // activate action (only BTN_BACK does anything). Mirror the journal: read
    // the item's property description on Enter. BTN_BACK is a plain button
    // (not an item row) so it falls through to the generic activate → close.
    bool isQuestItemRow =
        acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameQuestItems &&
        acc::engine::IsInventoryItemRow(e.control);

    // PartySelection portrait Enter where the add will be refused because the
    // party is full. The engine's CSWGuiPartySelection::OnToggled @0x006bf2a0
    // early-outs an *add* (portrait not yet selected) once two companions are
    // chosen — selected_count at panel +0x68 climbs to 2 and the toggle does
    // nothing, leaving no state change for the focus monitor to re-announce.
    // We still let the engine run (the no-op path plays its UI click sound and
    // refreshes the count label) but speak "Gruppe voll" so the silent refusal
    // is audible. selected flag at portrait +0x1c4 (written by
    // CSWGuiPartySelectionButton::SetSelected @0x006be370): 1 = in party.
    bool isPartyAddBlocked = false;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::PartySelection) {
        void** vt = *reinterpret_cast<void***>(e.control);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiPartySelectionButton) {
            __try {
                int selected = *reinterpret_cast<int*>(
                    reinterpret_cast<unsigned char*>(e.control) + 0x1c4);
                int selCount = *reinterpret_cast<int*>(
                    reinterpret_cast<unsigned char*>(g_chainPanel) + 0x68);
                isPartyAddBlocked = (selected == 0 && selCount > 1);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                isPartyAddBlocked = false;
            }
        }
    }

    // InGameLevelUp category / Annehmen the engine has not currently enabled.
    // The wizard enforces sequential leveling: only the "current" step carries
    // bit_flags bit 3 (0x8 = CSWGuiControl::SetEnabled); a real mouse click is
    // inert on the rest. Our deferred FireActivate force-raises is_active and
    // would otherwise let the user open a later step out of order — and doing
    // the Kräfte (powers) step before an earlier one makes the engine's
    // ChangeState/ClearPowers resync wipe the freshly-chosen power on commit
    // (root cause: memory project_levelup_power_lost_investigation). Block the
    // activation and point the user at the step the engine wants next. bit 3
    // is the real gate (bit 1 / is_active were both wrong on different button
    // sets); Annehmen gains bit 3 only once every step is done, so this also
    // correctly refuses a premature accept without trapping the user.
    bool isLevelUpInactiveStep = false;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameLevelUp) {
        __try {
            uint32_t bf = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(e.control) +
                kControlBitFlagsOffset);
            isLevelUpInactiveStep = (bf & 0x8) == 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            isLevelUpInactiveStep = false;
        }
    }

    // Pazaak wager popup less/more buttons (CSWGuiSpeedButton, gui ids 4/5).
    // They act only on their push callback (OnMinus/OnPlusButtonPushed →
    // panel HandleInputEvent 0x2f/0x30); the generic vtable[15] activate
    // (0x27) they ignore — its switch case 0x27 is the popup's *commit*, and
    // the button-level activate never reaches it, so Enter was a silent no-op.
    // The wager also starts AT the maximum (the constructor seeds current =
    // max), so "more" is a no-op until "less" has lowered it. Route Enter to
    // the panel's own less/more dispatch. cid 4 = less, cid 5 = more.
    bool isWagerStepButton = false;
    int  wagerStepCode = 0;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::PazaakWager) {
        __try {
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(e.control) + kControlIdOffset);
            if (cid == 4) {
                isWagerStepButton = true;
                wagerStepCode = acc::pazaak::kWagerLessCode;
            } else if (cid == 5) {
                isWagerStepButton = true;
                wagerStepCode = acc::pazaak::kWagerMoreCode;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            isWagerStepButton = false;
        }
    }

    if (acc::menus::pending::IsPending()) {
        acclog::Write("Enter", "op already pending; ignoring (target=%p)",
                      e.control);
        consumed = true;
    } else if (isLevelUpInactiveStep) {
        SpeakLevelUpDoStepFirst();
        acclog::Write("Menus.Enter",
                      "level-up step not enabled (bit3 clear) — blocked "
                      "out-of-order activation panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else if (isStoreItemRow) {
        acc::menus::pending::QueueStoreItemActivate(g_chainPanel, e.control);
        acclog::Write("Menus.Enter",
                      "store-item-activate panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else if (isJournalRow) {
        acc::menus::journal::SpeakDescription(g_chainPanel, e.control);
        consumed = true;
    } else if (isQuestItemRow) {
        acc::peek::SpeakItemRowDescription(e.control);
        acclog::Write("Menus.Enter",
                      "quest-item-description panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else if (e.textOnly) {
        // Modal body text — non-activatable. Re-speak so a user who missed
        // the open-time announce can hear it again. Don't fire vtable[15]
        // (the listbox has no activate handler).
        acc::menus::monitors::AnnounceControl(e.control);
        acclog::Write("Menus.Enter",
                      "re-announce panel=%p index=%d target=%p (text-only)",
                      activePanel, g_chainIndex, e.control);
        consumed = true;
    } else if (isTabButton) {
        int cursorY = e.cy + ComputeTabClickOffsetY(g_chainPanel);
        acc::menus::pending::QueueClickAt(e.cx, cursorY, e.control);
        acclog::Write("Menus.Enter",
                      "click-sim panel=%p index=%d target=%p cursorY=%d (tab)",
                      activePanel, g_chainIndex, e.control, cursorY);
        consumed = true;
    } else if (isEquipSlot) {
        // Bypass click-sim entirely. Calling OnEnterSlot then OnSelectSlot
        // directly invokes the same engine path that mouse-driven hover+click
        // does, but without depending on hit-test landing on the slot button
        // (the labels cover the buttons in z-order — see
        // docs/equip-flow-investigation.md). Deferred to OnUpdate to stay
        // clear of mid-input-dispatch recursion.
        acc::menus::pending::QueueEquipSelect(g_chainPanel, e.control);
        // Arm the picker zone now: OnSelectSlot raises field33_0x4270 |= 1
        // and the user proceeds to LB_ITEMS browsing. Self-clears on panel
        // close, picker Esc, or BTN_EQUIP dispatch.
        acc::menus::listbox::ArmEquipPicker(g_chainPanel);
        acclog::Write("EquipPicker",
                      "armed via direct OnEnterSlot+OnSelectSlot "
                      "(Enter on slot id=%d btn=%p panel=%p)",
                      equipSlotCid, e.control, g_chainPanel);
        consumed = true;
    } else if (isWorkbenchUpgradeSlot) {
        // Click-sim landed on a label (z-order trap); vtable[15] is the
        // keyboard-shortcut path that doesn't populate LB_ITEMS. Both verified
        // in patch-20260525-141557.log and -142247.log. RE'd the workbench
        // slot-pick chain in Lane's gzf — calling CSWGuiUpgrade::OnEnterSlot
        // + OnSlotSelected directly is the engine path that builds the
        // compatible-mods list from CSWPartyTable items + upgrades_2da /
        // upcrystals_2da and AddControls-replaces LB_ITEMS contents.
        acc::menus::pending::QueueWorkbenchSlotSelect(g_chainPanel, e.control);
        acc::menus::listbox::ArmWorkbenchUpgradePicker(g_chainPanel);
        acclog::Write("WorkbenchUpgrade",
                      "armed via direct OnEnterSlot+OnSlotSelected "
                      "(Enter on slot id=%d btn=%p panel=%p)",
                      workbenchUpgradeSlotCid, e.control, g_chainPanel);
        consumed = true;
    } else if (isWagerStepButton) {
        acc::menus::pending::QueueWagerInput(g_chainPanel, wagerStepCode);
        acclog::Write("Menus.Enter",
                      "pazaak-wager-step panel=%p index=%d target=%p code=0x%x",
                      g_chainPanel, g_chainIndex, e.control, wagerStepCode);
        consumed = true;
    } else if (isPartyAddBlocked) {
        // Run the engine's (no-op) toggle for the UI click sound, then tell
        // the user why the composition didn't change.
        acc::menus::pending::QueueActivate(e.control);
        prism::Speak(acc::strings::Get(acc::strings::Id::PartySelectionFull),
                     /*interrupt=*/false);
        acclog::Write("Menus.Enter",
                      "party-full add blocked panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else {
        acc::menus::pending::QueueActivate(e.control);
        // Drill flag is armed centrally inside the OnSwitchToSWInGameGui
        // detour — every path that opens a sub-screen (strip-icon Enter,
        // vanilla M/I/J hotkeys) flows through that one function, so no
        // per-caller arm is needed here.
        acclog::Write("Menus.Enter", "activate panel=%p index=%d target=%p",
                      activePanel, g_chainIndex, e.control);
        consumed = true;
    }
}

void WalkChildren(const char* label, void* parent, size_t offset,
                  const char* kindName) {
    if (!parent) return;
    // One BlockLog per walk. Line() is the full display (with pointers); Key()
    // is the same content with the volatile heap pointers stripped, so a walk
    // of a panel the engine recreated at a fresh address still hashes equal and
    // folds to a "(repeated Nx)" summary. The block emits-or-folds when `block`
    // leaves scope on any return path. Stable vtable code-addresses stay in the
    // key (they identify the control class).
    acclog::BlockLog block(label);
    if (kindName && *kindName) {
        block.Line("panel=%p kind=%s", parent, kindName);
        block.Key("kind=%s", kindName);
    }

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(parent) + offset);
    if (!list->data || list->size <= 0) {
        block.Line("walk parent=%p children=0", parent);
        block.Key("children=0");
        return;
    }
    int count = list->size;
    if (count > 256) {
        block.Line("walk parent=%p size_oob=%d (capped)", parent, count);
        block.Key("size_oob=%d", count);
        count = 256;
    }
    block.Line("walk parent=%p children=%d", parent, list->size);
    block.Key("children=%d", list->size);
    for (int i = 0; i < count; ++i) {
        void* child = list->data[i];
        if (!child) {
            block.Line("  [%d]=NULL", i);
            block.Key("  [%d]=NULL", i);
            continue;
        }
        int id = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(child) + kControlIdOffset);
        char text[256];
        // Pass `parent` so the perkind fallback resolves correctly when
        // walking InGameMenu's children — the icon labels/buttons have empty
        // CExoString/strref/text_object/gui_string and only resolve via the
        // panel-keyed perkind table.
        const char* source = acc::menus::extract::FromControl(
            child, text, sizeof(text), parent);
        if (source) {
            block.Line("  [%d] %p id=%d src=%s text=\"%s\"",
                       i, child, id, source, text);
            block.Key("  [%d] id=%d src=%s text=\"%s\"", i, id, source, text);
        } else {
            char vtbl[160];
            acc::engine::DumpControlVtable(child, vtbl, sizeof(vtbl));
            block.Line("  [%d] %p id=%d src=none %s",
                       i, child, id, vtbl);
            block.Key("  [%d] id=%d src=none %s", i, id, vtbl);
        }
    }
}

void HandleNavStep(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    bool navKeyIsHome = (code == kInputHome);
    bool navKeyIsEnd  = (code == kInputEnd);
    bool navKeyIsUp   = (code == kInputNavUp);
    bool navKeyIsDown = (code == kInputNavDown);
    if (!(navKeyIsUp || navKeyIsDown || navKeyIsHome || navKeyIsEnd)) return;
    if (activePanel == nullptr) return;

    if (activePanel != g_chainPanel) {
        RebindChain(activePanel);
    }
    if (g_chainCount == 0) {
        // Foreground panel has no navigable controls. Log so we can see
        // which panels are routing-only (e.g. the recurring 074FE618
        // overlay and the dialog routing target 0FDEE418 observed in
        // the in-game session) and decide whether to add a fallback
        // strategy (walk down the modal stack to the next chain-eligible
        // panel, or surface the panel's content via the title/listbox
        // path). For now: log only, leave the input unconsumed so the
        // engine sees it.
        acc::engine::PanelKind emptyKind = acc::engine::IdentifyPanel(activePanel);
        acclog::Write("Menus.Chain", "empty panel=%p kind=%s has no navigable "
                      "controls; input not consumed",
                      activePanel, acc::engine::PanelKindName(emptyKind));

        // Walk the panel ONCE so we can see what's actually in it.
        // OnSetActiveControl's panel-walk gate (s_lastPanel) doesn't
        // fire on these panels because the engine never sets focus on
        // them. Without a walk we never learn their structure — log-only
        // diagnostics give us nothing actionable.
        static void* s_walkedEmptyPanels[16];
        static int   s_walkedEmptyCount = 0;
        bool walked = false;
        for (int i = 0; i < s_walkedEmptyCount; ++i) {
            if (s_walkedEmptyPanels[i] == activePanel) { walked = true; break; }
        }
        if (!walked && s_walkedEmptyCount < 16) {
            s_walkedEmptyPanels[s_walkedEmptyCount++] = activePanel;
            acclog::Write("Menus.EmptyChain", "walk panel=%p kind=%s",
                          activePanel, acc::engine::PanelKindName(emptyKind));
            WalkChildren("Menus.EmptyChain", activePanel, kPanelControlsOffset);
        }
    }
    if (g_chainCount <= 0) return;

    int newIndex;
    if (navKeyIsHome) {
        newIndex = 0;
    } else if (navKeyIsEnd) {
        newIndex = g_chainCount - 1;
    } else {
        int delta = navKeyIsDown ? +1 : -1;
        newIndex = g_chainIndex + delta;
        if (newIndex < 0)              newIndex = 0;
        if (newIndex >= g_chainCount)  newIndex = g_chainCount - 1;
    }
    g_chainIndex = newIndex;

    ChainEntry& e = g_chain[g_chainIndex];
    // Chargen Fähigkeiten descriptions are long (~10s of speech each) but the
    // user navigates Up/Down faster than they can read. With interrupt=false
    // (our default), each step queues "label, suffix, description" behind the
    // previous step's still-playing description — the user hears descriptions
    // one row behind their focus. Silence any in-flight speech before
    // announcing the new row, so each chain step starts fresh and the
    // just-arrived focus wins the speech channel. No-op on every other panel
    // (their descriptions are short enough to drain naturally).
    if (acc::menus::chargen_skills::IsChargenSkillsPanel(g_chainPanel)) {
        prism::Silence();
    }
    acc::menus::monitors::AnnounceControl(e.control);
    // Mirror chain focus into the chargen Attributes panel's selected_ability
    // so the next Left/Right press routes OnPlusButton / OnMinusButton to the
    // focused ability rather than the default top row (STR). No-op on every
    // other panel.
    acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus();
    // Same for the chargen Skills panel — different field
    // (selected_skill_index) on a different panel, same mechanism.
    acc::menus::chargen_skills::SyncSelectedSkillFromChainFocus();
    // Per-row info suffixes / descriptions across panels that need them.
    // Each helper no-ops on every panel except its own.
    acc::menus::chargen_attr::AnnounceChainStepSuffix(g_chainPanel, e.control);
    acc::menus::chargen_attr::AnnounceChainStepDescription(g_chainPanel, e.control);
    acc::menus::chargen_skills::AnnounceChainStepSuffix(g_chainPanel, e.control);
    acc::menus::chargen_skills::AnnounceChainStepDescription(g_chainPanel, e.control);
    acc::menus::store::AnnounceChainStepSuffix(g_chainPanel, e.control);
    // Inventory rows (CSWGuiInGameInventory / Container loot listbox): append
    // "N Stück" when stack_size > 1. Store rows are deliberately excluded —
    // the store suffix above already speaks "Lager N" / "du besitzt N".
    // Silent on stack_size == 1 so weapons / armour stay quiet.
    if (acc::engine::IsInventoryItemRow(e.control)) {
        int stack = acc::engine::ReadItemRowStackCount(e.control);
        if (stack > 1) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix),
                     acc::strings::Get(acc::strings::Id::FmtItemStackSuffix),
                     stack);
            prism::Speak(suffix, /*interrupt=*/false);
        }
        // Charged consumables can't stack, so this is mutually exclusive with
        // the suffix above. charges == 0 is still meaningful (depleted item).
        int charges = acc::engine::ReadItemRowCharges(e.control);
        if (charges >= 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix),
                     acc::strings::Get(acc::strings::Id::FmtItemChargeSuffix),
                     charges);
            prism::Speak(suffix, /*interrupt=*/false);
        }
    }
    int cursorX = e.cx;
    int cursorY = e.cy;
    if (!e.textOnly) {
        // Cursor warp + suppress-budget exist to make hover-to-focus work for
        // activatable controls. Text-only entries (modal body listboxes) have
        // no hover semantics worth chasing — skipping keeps the cursor stable
        // on whatever button the user just left, and avoids spurious
        // engine-side SetActiveControl echoes from the listbox under the cursor.
        if (IsTabButton(e.control)) {
            cursorY += ComputeTabClickOffsetY(g_chainPanel);
        }
        if (acc::menus::detail::IsClassSelectionIcon(g_chainPanel, e.control) &&
            g_classIconClickOffsetX > 0) {
            cursorX += g_classIconClickOffsetX;
        }
        // Chargen Attribute / Skills hit-test-shifts-up-one-row compensation.
        // Without it the cursor lands on the row above and the engine's
        // OnEnterPointsButton populates description_listbox for the wrong row.
        {
            int abilityPitch = acc::menus::chargen_attr::RowPitchForCursorWarp(
                g_chainPanel, e.control);
            if (abilityPitch > 0) cursorY += abilityPitch;
        }
        {
            int skillPitch = acc::menus::chargen_skills::RowPitchForCursorWarp(
                g_chainPanel, e.control);
            if (skillPitch > 0) cursorY += skillPitch;
        }
        acc::menus::pending::QueueMoveCursor(cursorX, cursorY, e.control);
        // No explicit suppress needed for the engine-side focus echo:
        // AnnounceControl above primed channel-0 dedup via MarkSpoken, so
        // DrainPendingAnnounce will short-circuit when the cursor-warp's
        // SetActive echo arrives with the same text.
    }
    const char* dirTag =
        navKeyIsHome ? "HOME" :
        navKeyIsEnd  ? "END"  :
        navKeyIsDown ? "DOWN" : "UP";
    acclog::Write("Menus.Chain",
                  "step panel=%p index=%d/%d target=%p center=(%d,%d) cursor=(%d,%d)%s %s",
                  g_chainPanel, g_chainIndex, g_chainCount,
                  e.control, e.cx, e.cy, cursorX, cursorY,
                  e.textOnly ? " text-only" : "",
                  dirTag);
    // Always consume nav keys on a panel with a non-empty chain.
    consumed = true;
}

void HandleLeftRight(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputNavLeft && code != kInputNavRight) return;
    // Pazaak wager popup owns Left/Right via pazaak::Tick's polled hold-to-
    // repeat stepper. Consume here so the slider/cycle-arrow path can't also
    // act (FindAdjacentArrow would otherwise re-fire the masked less/more
    // button, double-stepping the wager).
    if (activePanel &&
        acc::engine::IdentifyPanel(activePanel) == acc::engine::PanelKind::PazaakWager) {
        consumed = true;
        return;
    }
    if (activePanel == nullptr || g_chainPanel != activePanel) return;
    if (g_chainCount <= 0 || g_chainIndex < 0 || g_chainIndex >= g_chainCount) return;

    void* focused = g_chain[g_chainIndex].control;
    bool toRight = (code == kInputNavRight);

    if (acc::engine::IsSlider(focused)) {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Menus.Slider", "%s: op already pending; ignoring",
                          toRight ? "right" : "left");
        } else {
            int sliderCode = toRight ? 500 : 501;
            acc::menus::pending::QueueSliderInput(focused, sliderCode);
            acclog::Write("Menus.Slider", "%s panel=%p focus=%p code=%d",
                          toRight ? "right" : "left",
                          activePanel, focused, sliderCode);
        }
    } else {
        // Panel-aware cycle override: in CSWGuiPortraitCharGen the chain
        // holds left_arrow as the lone anchor (right_arrow is filtered out
        // in RebindChain). FindAdjacentArrow can pick up the right_arrow
        // as a same-row neighbour when going right, but going left there's
        // nothing to the left of x=272 — so we resolve the targets directly
        // from the panel offsets:
        //   Left  → activate left_arrow (cycles -1)
        //   Right → activate right_arrow (cycles +1)
        // Engine's UpdatePortraitButton writes the new resref to
        // creature.portrait, the per-frame focus monitor re-reads, and
        // the diff fires the new "Porträt: …" announcement.
        void* portraitTarget = nullptr;
        {
            void** pVt = *reinterpret_cast<void***>(activePanel);
            if (reinterpret_cast<uintptr_t>(pVt) ==
                    kVtableCSWGuiPortraitCharGen) {
                auto* base = reinterpret_cast<unsigned char*>(activePanel);
                void* leftArrow = base + kPortraitLeftArrowOffset;
                if (focused == leftArrow) {
                    portraitTarget = toRight
                        ? (void*)(base + kPortraitRightArrowOffset)
                        : leftArrow;
                }
            }
        }
        void* neighbor = portraitTarget
            ? portraitTarget
            : FindAdjacentArrow(activePanel, focused, toRight);
        if (neighbor) {
            if (acc::menus::pending::IsPending()) {
                acclog::Write("Menus.Cycle", "%s: op already pending; ignoring",
                              toRight ? "right" : "left");
            } else {
                acc::menus::pending::QueueActivate(neighbor);
                acclog::Write("Menus.Cycle", "%s panel=%p focus=%p neighbor=%p%s",
                              toRight ? "right" : "left",
                              activePanel, focused, neighbor,
                              portraitTarget ? " (portrait-anchor)" : "");
            }
        } else {
            acclog::Write("Menus.Cycle", "%s: no adjacent arrow for focus=%p",
                          toRight ? "right" : "left", focused);
        }
    }
    consumed = true;
}

void HandleEsc(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputEsc1 && code != kInputEsc2) return;

    // Store-specific Esc: route to cancel_button (Schliess.) directly.
    // The store isn't in IsModalPopupPanel (it's the foreground modal,
    // not a popup on top), and the chain doesn't include the cancel
    // button anymore (we filter it out so it doesn't clutter Up/Down
    // nav), so without this Esc would no-op on the store.
    if (acc::menus::store::IsStorePanel(activePanel)) {
        if (acc::menus::store::CloseFromEsc()) consumed = true;
    }

    // Workbench upgrade panel Esc: route to BTN_BACK (id 28, "Abbrechen")
    // directly. Same shape as the store branch above — the upgrade.gui
    // panel is the foreground modal (not a popup on top), so the generic
    // Esc gate below (IsModalPopupPanel / g_tabbedPanel / escIsOptionsSub)
    // doesn't fire. We also can't rely on FindCancelButton landing on
    // BTN_BACK reliably here (see kWorkbenchUpgradeSpec comments).
    // While the picker is armed the spec's onEsc disarms; this branch
    // only catches Esc when the picker is NOT armed (user on a slot
    // button, BTN_ASSEMBLE, or BTN_BACK).
    if (!consumed && activePanel != nullptr &&
        acc::engine::IdentifyPanel(activePanel) ==
            acc::engine::PanelKind::WorkbenchUpgrade &&
        !acc::menus::listbox::IsWorkbenchUpgradePickerArmed())
    {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Esc", "WorkbenchUpgrade — op already pending; ignoring");
            consumed = true;
        } else {
            constexpr int kWorkbenchUpgradeBtnBack = 28;
            void* back = acc::menus::detail::FindControlById(
                activePanel, kWorkbenchUpgradeBtnBack);
            if (back) {
                acc::menus::pending::QueueActivate(back);
                acclog::Write("Esc",
                              "WorkbenchUpgrade -> BTN_BACK panel=%p target=%p",
                              activePanel, back);
                consumed = true;
            } else {
                acclog::Write("Esc",
                              "WorkbenchUpgrade -- BTN_BACK not found on panel=%p",
                              activePanel);
            }
        }
    }

    // InGameOptions sub-screen override: the parent strip's controls[0] is
    // a button (Spiel laden), not a listbox, so DetectTabsCluster never
    // latches and `g_tabbedPanel` stays null — the tabbed-parent arm above
    // misses every in-game Options sub-screen. The foreground may also be
    // a HUD layer rather than the sub-screen itself, depending on overlay
    // ordering, so activePanel isn't a reliable target either.
    //
    // `g_chainPanel` is the right discriminator: SetActiveControl re-binds
    // the chain to the heap-allocated sub-screen on entry, so it points at
    // Spieleinstellungen / Grafik / Sound / Auto-Pause / Feedback /
    // Tastenbelegung / Mauseinstellungen for the lifetime of that screen,
    // and FindCloseButton on it resolves Schliess. reliably. The
    // IsInGameOptionsSubScreen helper already excludes the parent strip.
    void* escTargetPanel = activePanel;
    bool  escIsOptionsSub = false;
    if (g_chainPanel != nullptr &&
        acc::engine::IsInGameOptionsSubScreen(g_chainPanel))
    {
        escTargetPanel = g_chainPanel;
        escIsOptionsSub = true;
    }

    if (escTargetPanel != nullptr &&
        ((g_tabbedPanel != nullptr && escTargetPanel != g_tabbedPanel) ||
         acc::engine::IsModalPopupPanel(
             acc::engine::IdentifyPanel(escTargetPanel)) ||
         escIsOptionsSub))
    {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Esc", "op already pending; ignoring");
            consumed = true;
        } else {
            // Probe order matters: confirm-style popups (OK + Abbrechen,
            // Yes + No, …) carry BOTH a cancel-intent button AND the
            // affirmative that FindCloseButton matches as "OK". Esc is a
            // back-out gesture, never a confirm — try Abbrechen/Cancel
            // first so the quit-confirm and save-overwrite-style dialogs
            // route Esc to the safe choice. Single-button info popups
            // (StatusSummary's lone Schliess, AreaTransition's Weiter)
            // have no cancel button, so the FindCloseButton fallback
            // handles them.
            void* cancelBtn = FindCancelButton(escTargetPanel);
            void* tgt = cancelBtn ? cancelBtn : FindCloseButton(escTargetPanel);
            if (tgt) {
                acc::menus::pending::QueueActivate(tgt);
                // InGameOptions sub-screens: the close fires a deferred
                // destroy — the engine keeps the panel in panels[] across
                // the FireActivate dispatch (ValidateChainPanel finds it,
                // chain stays), then frees the panel + children at end
                // of tick. Between those two ticks, MonitorFocusedControl
                // walks g_chain[g_chainIndex].control, dereferences a
                // freed button, and FromControl's SEH-caught AV interacts
                // with /GS to fastfail. Confirmed by crash dump TID 16116:
                // ESI matched the chain entry the user had last navigated
                // to before pressing Esc.
                //
                // ValidateChainPanel can't help (panel still in panels[]
                // when it runs), and chain[10] nulling only covers
                // Schliess. itself — the other 11 entries are equally
                // dead. Invalidate the whole chain here; the Schliess.
                // pointer is already captured by QueueActivate and the
                // next SetActiveControl rebuilds against whatever the
                // engine refocuses on.
                if (escIsOptionsSub) InvalidateChain();
                acclog::Write("Menus.Esc",
                              "%s panel=%p kind=%s target=%p%s",
                              cancelBtn ? "cancel" : "close",
                              escTargetPanel,
                              acc::engine::PanelKindName(
                                  acc::engine::IdentifyPanel(escTargetPanel)),
                              tgt,
                              escIsOptionsSub
                                  ? " (InGameOptions sub-screen)" : "");
                consumed = true;
            } else {
                acclog::Write("Menus.Esc",
                              "sub-dialog panel=%p kind=%s but no cancel/close "
                              "button found; passing through",
                              escTargetPanel,
                              acc::engine::PanelKindName(
                                  acc::engine::IdentifyPanel(escTargetPanel)));
            }
        }
    }
}

}  // namespace acc::menus::chain
