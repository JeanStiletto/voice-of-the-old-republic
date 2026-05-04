// KOTOR Accessibility — menu-side hook handlers (chain navigation, focus
// events, input dispatch, per-tick monitors).
//
// Layering:
//   log.{h,cpp}             file/debug logging primitives
//   tolk.{h,cpp}            screen-reader bridge (LoadLibrary'd lazily)
//   core_dllmain.cpp        DllMain + OnRulesInit + EnsureTolkInitialized
//   engine_input.{h,cpp}    InputIndices name table + manager translate
//   engine_offsets.h        engine struct/vtable offset constants + engine structs
//   engine_reads.{h,cpp}    SEH-guarded readers (CallDowncast, ReadGuiString, ...)
//   engine_panels.{h,cpp}   PanelKind enum + CGuiInGame slot classification
//   engine_manager.{h,cpp}  CSWGuiManager surface + cursor / click-sim PFNs
//   this file               the menu-accessibility hook handlers
//
// Phase 0 of the long-term nav plan extracted the foundation (core_dllmain
// + engine_*) out of the original monolithic Accessibility.cpp; the
// remaining menu-accessibility code is what lives here under the new name.
// Per plan, the menu-side logic is NOT decomposed further in Phase 0
// (incremental refactor discipline) — see docs/navsystem-longterm-plan.md.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "tolk.h"
#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_player.h"   // Phase 1 lay-off 4 (test fixture only)
#include "engine_reads.h"
#include "audio_bus.h"       // Phase 1 lay-off 4 (test fixture only)
#include "cycle_input.h"     // Phase 2 lay-off 3
#include "guidance_autowalk.h"  // Phase 2 lay-off 5 (progress watchdog)
#include "camera_announce.h"    // Phase 2 ad-hoc — camera-direction on A/D
#include "diag_engine_select.h" // Phase 2 diagnostic — Q/E/Tab observation
#include "interact_hotkey.h"    // Phase 2 lay-off 9b
#include "passive_narrate.h"    // Phase 2 lay-off 9a
#include "probe_world_hover.h"  // Phase 2 lay-off 9-probe (diagnostic)
#include "strings.h"            // Container loot panel announces
#include "turn_announce.h"      // Phase 2 ad-hoc — Pillar 2 sub-feature C

// Engine readers + offset constants moved to engine_reads.{h,cpp} +
// engine_offsets.h in Phase 0 lay-off 2. Pull the readers' names into the
// menu-side TU so callsites stay as they were.
using namespace acc::engine;

// Forward decl from core_dllmain.cpp. The first hook to fire calls this so
// Tolk is loaded the moment any focus / input event reaches us.
void EnsureTolkInitialized();

// Forward declarations: ExtractAnnounceableText decorates its output via
// FindSiblingLabel (slider category prefix) and LookupCycleCategory (cycle
// value-display prefix); both are defined later in the file alongside the
// chain machinery. g_currentPanel is the focused panel pointer maintained
// by OnSetActiveControl.
extern void* g_currentPanel;
static const char* FindSiblingLabel(void* panel, void* control,
                                    char* outBuf, size_t bufSize);
static const char* LookupCycleCategory(void* control);
static bool IsChainNavigable(void* control);

// Equipment screen control IDs from equip.gui (extracted via xoreos-tools
// gff2xml). The 9 BTN_INV_* slot buttons, the item-picker listbox, and the
// two action buttons. Used by the per-kind extraction fallback (so empty-
// text slot buttons announce as "Kopf" / "Implantat" / etc.) and the
// InGameEquip input handler that drives the modal item-picker zone.
//
// Defined here at file scope (rather than next to the Container IDs ~1000
// lines down) because ExtractAnnounceableText immediately below needs them.
constexpr int kEquipBtnHeadId    =  7;  // BTN_INV_HEAD     (TLK 31375)
constexpr int kEquipBtnImplantId =  9;  // BTN_INV_IMPLANT  (literal — no TLK)
constexpr int kEquipBtnBodyId    = 11;  // BTN_INV_BODY     (TLK 31380)
constexpr int kEquipBtnArmLId    = 13;  // BTN_INV_ARM_L    (TLK 31376)
constexpr int kEquipBtnWeapLId   = 15;  // BTN_INV_WEAP_L   (TLK 31378)
constexpr int kEquipBtnBeltId    = 17;  // BTN_INV_BELT     (TLK 31382)
constexpr int kEquipBtnWeapRId   = 19;  // BTN_INV_WEAP_R   (TLK 31379)
constexpr int kEquipBtnArmRId    = 21;  // BTN_INV_ARM_R    (TLK 31377)
constexpr int kEquipBtnHandsId   = 23;  // BTN_INV_HANDS    (TLK 31383)
constexpr int kEquipLbItemsId    =  5;  // LB_ITEMS
constexpr int kEquipBtnBackId    = 36;  // BTN_BACK         (TLK 1582 = Schliess.)
constexpr int kEquipBtnEquipId   = 37;  // BTN_EQUIP        (TLK 1580 = OK)

static const char* ExtractAnnounceableText(void* control,
                                           char* outBuf, size_t bufSize,
                                           void* ownerPanel = nullptr) {
    if (!control || bufSize < 2) return nullptr;

    const char* source = nullptr;

    // 1. Tooltip on the base class — works for any control that has one.
    //    SEH-wrapped: the field at +0x28 holds a `char*` that on a stale
    //    (freed-and-reused) control can be a bogus address; the memcpy
    //    would then fault reading the source. CallDowncast already SEH-
    //    protects steps 2-5; this covers the single read path that doesn't
    //    go through it.
    __try {
        const char* tip;
        uint32_t    tipLen;
        int         id;
        if (ReadControlNameFields(control, tip, tipLen, id) &&
            tipLen > 0 && tipLen < bufSize) {
            memcpy(outBuf, tip, tipLen);
            outBuf[tipLen] = '\0';
            source = "tooltip";
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        source = nullptr;
    }

    // 2. CSWGuiButton (most common — also covers CharButton, ActivatedButton,
    //    ButtonToggle since those embed Button at offset 0 AND the engine's
    //    AsButton override returns `this` for them). Tries inline CExoString,
    //    then TLK str_ref, then text_object indirection — the last covers
    //    classes whose text routes through CSWGuiText.text_params.text_object
    //    rather than the inline CExoString.
    if (!source) {
        if (void* btn = CallDowncast(control, kVtableAsButton)) {
            if (ExtractTextOrStrRefIndirect(btn,
                                            kButtonTextOffset,
                                            kButtonStrRefOffset,
                                            kButtonTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "button";
            }
        }
    }

    // 3. CSWGuiButtonToggle — defensive fallback if AsButton misses it.
    //    Same offsets because ButtonToggle.button is at offset 0.
    if (!source) {
        if (void* tgl = CallDowncast(control, kVtableAsButtonToggle)) {
            if (ExtractTextOrStrRefIndirect(tgl,
                                            kButtonTextOffset,
                                            kButtonStrRefOffset,
                                            kButtonTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "buttontoggle";
            }
        }
    }

    // 4. CSWGuiLabel.
    if (!source) {
        if (void* lbl = CallDowncast(control, kVtableAsLabel)) {
            if (ExtractTextOrStrRefIndirect(lbl,
                                            kLabelTextOffset,
                                            kLabelStrRefOffset,
                                            kLabelTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "label";
            }
        }
    }

    // 5. CSWGuiLabelHilight — same offsets (Label embedded at 0).
    if (!source) {
        if (void* hil = CallDowncast(control, kVtableAsLabelHilight)) {
            if (ExtractTextOrStrRefIndirect(hil,
                                            kLabelTextOffset,
                                            kLabelStrRefOffset,
                                            kLabelTextObjectOffset,
                                            outBuf, bufSize)) {
                source = "labelhilight";
            }
        }
    }

    // 6. CSWGuiSlider — no AsSlider downcast accessor exists; detect by
    //    vtable identity. cur_value / max_value are Lane-named uint32 fields.
    //    The slider widget itself has no inline category text (its CExoString
    //    is the rendered "X von Y"); the category name lives on a sibling
    //    CSWGuiLabel rendered to the left of the slider. Look it up via
    //    FindSiblingLabel and prepend.
    if (!source && IsSlider(control)) {
        uint32_t cur = ReadU32(control, kSliderCurValueOffset);
        uint32_t max = ReadU32(control, kSliderMaxValueOffset);
        char label[128];
        if (g_currentPanel &&
            FindSiblingLabel(g_currentPanel, control, label, sizeof(label))) {
            snprintf(outBuf, bufSize, "%s %u von %u", label, cur, max);
        } else {
            snprintf(outBuf, bufSize, "%u von %u", cur, max);
        }
        source = "slider";
    }

    // 7. CSWGuiListBox content. The listbox is a container; its "text" is
    //    the concatenation of its row controls' texts. Many in-game modals
    //    (CSWGuiMessageBox-style — including the recurring 07434E40 OK/Cancel
    //    in our log, and the quit-confirmation "Möchtest du wirklich
    //    aufhören?") put their message text in a single listbox row rather
    //    than directly in a panel label, so without this path the modal
    //    appears as src=none. Recursion is bounded to one level — listbox
    //    rows are not themselves listboxes in observed layouts, so we only
    //    try button/label extraction per row, never re-enter the listbox
    //    branch.
    //
    //    Capped at 8 rows to keep the announcement digestible (long save-
    //    game lists aren't candidates for this code path; they have rows
    //    that already announce individually via OnListBoxSetActiveControl).
    if (!source && IsListBox(control)) {
        auto* lb = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(control) + kListBoxControlsOffset);
        if (lb && lb->data && lb->size > 0) {
            int n = lb->size > 8 ? 8 : lb->size;
            outBuf[0] = '\0';
            size_t off = 0;
            for (int i = 0; i < n; ++i) {
                void* row = lb->data[i];
                if (!row) continue;
                char rowText[256];
                bool got = false;
                if (void* btn = CallDowncast(row, kVtableAsButton)) {
                    got = ExtractTextOrStrRef(btn, kButtonTextOffset,
                                              kButtonStrRefOffset,
                                              rowText, sizeof(rowText));
                }
                if (!got) {
                    if (void* lbl = CallDowncast(row, kVtableAsLabel)) {
                        got = ExtractTextOrStrRef(lbl, kLabelTextOffset,
                                                  kLabelStrRefOffset,
                                                  rowText, sizeof(rowText));
                    }
                }
                if (!got) {
                    if (void* hil = CallDowncast(row, kVtableAsLabelHilight)) {
                        got = ExtractTextOrStrRef(hil, kLabelTextOffset,
                                                  kLabelStrRefOffset,
                                                  rowText, sizeof(rowText));
                    }
                }
                if (!got) continue;
                size_t rowLen = strnlen(rowText, sizeof(rowText));
                if (rowLen == 0) continue;
                size_t needed = (off > 0 ? 2 : 0) + rowLen + 1;
                if (off + needed >= bufSize) break;
                if (off > 0) {
                    outBuf[off++] = ' ';
                    outBuf[off++] = ' ';
                }
                memcpy(outBuf + off, rowText, rowLen);
                off += rowLen;
                outBuf[off] = '\0';
            }
            if (off > 0) source = "listbox";
        }
    }

    // 8. Speculative text read for known label/button vtable overrides.
    //    Some classes override AsLabel/AsButton in their vtable so that
    //    CallDowncast returns null even though the class IS label-like or
    //    button-like at the field-offset level. The InGameMenu icons are
    //    the canonical case: 8 sibling labels at vtable=0x0073E8E8 and
    //    8 image-only buttons at vtable=0x0073E658, and our standard
    //    extraction returns nullptr for all of them (panel-walk shows
    //    src=none).
    //
    //    For each entry in kKnownVtableOverrides we try a direct read at
    //    the standard label/button text offsets, guarded by SEH so that
    //    reading at an offset that's NOT a CExoString doesn't crash the
    //    game. If the structure is different, we'll get a SEH exception
    //    and silently fall through to the placeholder path.
    //
    //    Allowlist gating keeps speculative reads off random unknown
    //    vtables — we only fire on classes we've observed needing this.
    if (!source) {
        struct VtableOverrideInfo {
            uintptr_t vtable;
            bool      tryLabel;
            bool      tryButton;
            const char* tag;
        };
        static const VtableOverrideInfo k_knownOverrides[] = {
            // Sibling labels of in-game-menu icons (Equipment, Inventory,
            // Character, ...) — observed children [0..7] of CSWGuiInGameMenu.
            // Also chargen wizard step-number decorations.
            { 0x0073E8E8, true,  false, "label-spec" },
            // Image-only buttons (in-game-menu icons children [8..15],
            // chargen class icons, portrait-picker arrows).
            { 0x0073E658, false, true,  "button-spec" },
        };
        void** vt = *reinterpret_cast<void***>(control);
        uintptr_t vta = reinterpret_cast<uintptr_t>(vt);
        for (const auto& ov : k_knownOverrides) {
            if (ov.vtable != vta) continue;
            char text[256];
            bool got = false;
            if (ov.tryLabel) {
                __try {
                    // Path A: inline CExoString at standard label offset.
                    if (ReadCExoString(control, kLabelTextOffset,
                                       text, sizeof(text))) {
                        got = true;
                    }
                    // Path B: strref at standard label offset → TLK.
                    if (!got) {
                        uint32_t strref = ReadU32(control, kLabelStrRefOffset);
                        got = LookupTlk(strref, text, sizeof(text));
                    }
                    // Path C: text_object indirection. CSWGuiLabel.text.
                    // text_params.text_object is a CSWGuiText* at +0x138; if
                    // non-null, read its text_params.text (CExoString @+0x18)
                    // or text_params.str_ref (@+0x20). Many labelhilights
                    // route their rendered text through this pointer rather
                    // than the inline CExoString.
                    if (!got) {
                        void* textObj = *reinterpret_cast<void**>(
                            reinterpret_cast<unsigned char*>(control)
                            + kLabelTextObjectOffset);
                        if (textObj) {
                            if (ReadCExoString(textObj, kTextObjectTextOffset,
                                               text, sizeof(text))) {
                                got = true;
                            } else {
                                uint32_t strref = ReadU32(textObj,
                                                          kTextObjectStrRefOffset);
                                got = LookupTlk(strref, text, sizeof(text));
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("Speculative label read SEH for vtable=0x%x "
                                  "control=%p", (unsigned)vta, control);
                    got = false;
                }
            }
            if (!got && ov.tryButton) {
                __try {
                    // Path A: inline CExoString at standard button offset.
                    if (ReadCExoString(control, kButtonTextOffset,
                                       text, sizeof(text))) {
                        got = true;
                    }
                    // Path B: strref at standard button offset → TLK.
                    if (!got) {
                        uint32_t strref = ReadU32(control, kButtonStrRefOffset);
                        got = LookupTlk(strref, text, sizeof(text));
                    }
                    // Path C: text_object indirection at +0x1BC (button-side).
                    if (!got) {
                        void* textObj = *reinterpret_cast<void**>(
                            reinterpret_cast<unsigned char*>(control)
                            + kButtonTextObjectOffset);
                        if (textObj) {
                            if (ReadCExoString(textObj, kTextObjectTextOffset,
                                               text, sizeof(text))) {
                                got = true;
                            } else {
                                uint32_t strref = ReadU32(textObj,
                                                          kTextObjectStrRefOffset);
                                got = LookupTlk(strref, text, sizeof(text));
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    acclog::Write("Speculative button read SEH for vtable=0x%x "
                                  "control=%p", (unsigned)vta, control);
                    got = false;
                }
            }
            if (got) {
                size_t tlen = strnlen(text, sizeof(text));
                if (tlen > 0 && tlen + 1 <= bufSize) {
                    memcpy(outBuf, text, tlen + 1);
                    source = ov.tag;
                    acclog::Write("Speculative read hit: vtable=0x%x control=%p "
                                  "text=\"%s\"", (unsigned)vta, control, outBuf);
                    break;
                }
                // got=true but text is empty/whitespace — log so we can see
                // the read path is wired but the field is genuinely empty.
                acclog::Write("Speculative read empty: vtable=0x%x control=%p "
                              "(read returned but text was empty)",
                              (unsigned)vta, control);
            } else {
                // Distinct from SEH: read paths returned cleanly, text just
                // wasn't there. Could mean the offset is wrong for this
                // class, the CExoString is genuinely empty, or the strref is
                // 0/0xFFFFFFFF (LookupTlk silently returns false). Logging
                // every miss helps confirm the speculative path runs and
                // that reads aren't faulting silently.
                acclog::Write("Speculative read miss: vtable=0x%x control=%p "
                              "tag=%s", (unsigned)vta, control, ov.tag);
            }
        }
    }

    // 9a. Per-kind hardcoded label fallback. Some panels have widgets whose
    //     text isn't extractable through any of the engine paths we know
    //     (inline CExoString, strref, text_object, gui_string, tooltip all
    //     verified empty for CSWGuiInGameMenu's icons in
    //     patch-20260502-192712.log — 8 labels + 8 buttons all empty).
    //     The text must be set via script-side or .gui-resource paths we
    //     haven't traced yet. For known panel structures with fixed layout
    //     (CSWGuiInGameMenu's struct definition pins the icon order), we
    //     can hardcode the names by index until the engine-side path is
    //     identified.
    //
    //     CSWGuiInGameMenu layout (per swkotor.exe.h:10145):
    //       controls[0..7]  = 8 CSWGuiLabelHilight (vtable=0x0073E8E8)
    //                          equipment, inventory, character, map,
    //                          abilities, journal, options, messages
    //       controls[8..15] = 8 CSWGuiButton       (vtable=0x0073E658)
    //                          same names, same order
    //
    //     The user-visible captions in German are different from the
    //     internal field names; the strings below are the rendered
    //     in-game captions for the German build (matches "M" / "I" / "C"
    //     hotkey conventions per the controls-and-input doc).
    // Resolve the owning panel for the perkind fallback. Caller-passed owner
    // wins (RebindChain, WalkChildren, BuildContentFingerprint, SetActiveControl
    // all know the panel). Otherwise scan panels[] — covers callers that don't
    // know the panel (AnnounceControl from chain-step, listbox-row helpers,
    // FindCloseButton from the input hook). g_currentPanel is the last-resort
    // fallback for early-attach windows where the manager isn't resolvable yet.
    void* ownerForPerkind = ownerPanel;
    if (!ownerForPerkind) ownerForPerkind = FindOwningPanel(control);
    if (!ownerForPerkind) ownerForPerkind = g_currentPanel;
    if (!source && ownerForPerkind && IsPanelKindInGameMenu(ownerForPerkind)) {
        // Localized names sourced from dialog.tlk strrefs where they exist;
        // literal fallback for the one strref we couldn't find. Strref values
        // verified by parsing the user's actual dialog.tlk (German build,
        // LangID=2 — `tlk_lookup.ps1` results pasted into git history). The
        // strrefs are stable across localizations: the engine looks up the
        // SAME entry index from the locale-specific TLK, so a French or
        // English install gets correctly translated names without any per-
        // language switch. Equipment has no standalone strref in any
        // observed TLK — the engine never asks for that text via TLK — so
        // we fall back to a literal that matches the German build.
        struct InGameMenuName {
            uint32_t    strref;   // 0xFFFFFFFF = no strref, use literal
            const char* literal;  // fallback if LookupTlk fails
        };
        static const InGameMenuName k_inGameMenuNames[8] = {
            { 0xFFFFFFFFu, "Ausr\xfcstung" },     // equipment
            { 48220u,      "Inventar"     },      // inventory
            { 48225u,      "Charakterblatt" },    // character_sheet
            { 48221u,      "Karte"        },      // map
            { 48224u,      "F\xe4higkeiten" },    // abilities
            { 48218u,      "Auftr\xe4ge" },       // journal (= "quests/orders")
            { 48222u,      "Optionen"     },      // options
            { 48223u,      "Nachrichten"  },      // messages
        };

        // Find the index of `control` within the owning panel's controls[].
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(ownerForPerkind) + kPanelControlsOffset);
        if (list && list->data && list->size > 0) {
            int n = list->size > 32 ? 32 : list->size;
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (list->data[i] == control) { idx = i; break; }
            }
            // Labels are at panel.controls[0..7]; buttons at [8..15].
            // Same name table, shifted index for buttons.
            int nameIdx = -1;
            if (idx >= 0 && idx <= 7)       nameIdx = idx;
            else if (idx >= 8 && idx <= 15) nameIdx = idx - 8;
            if (nameIdx >= 0) {
                const auto& spec = k_inGameMenuNames[nameIdx];
                bool gotTlk = false;
                if (spec.strref != 0xFFFFFFFFu) {
                    char tlkText[256];
                    if (LookupTlk(spec.strref, tlkText, sizeof(tlkText))) {
                        size_t tlen = strnlen(tlkText, sizeof(tlkText));
                        if (tlen > 0 && tlen + 1 <= bufSize) {
                            memcpy(outBuf, tlkText, tlen + 1);
                            source = "perkind-tlk";
                            gotTlk = true;
                            acclog::Write("Per-kind InGameMenu TLK: control=%p "
                                          "panelIdx=%d strref=%u -> \"%s\"",
                                          control, idx, spec.strref, outBuf);
                        }
                    }
                }
                if (!gotTlk) {
                    size_t nlen = strlen(spec.literal);
                    if (nlen + 1 <= bufSize) {
                        memcpy(outBuf, spec.literal, nlen + 1);
                        source = "perkind-literal";
                        acclog::Write("Per-kind InGameMenu literal: control=%p "
                                      "panelIdx=%d strref=%u -> \"%s\"",
                                      control, idx, spec.strref, outBuf);
                    }
                }
            }
        }
    }

    // 9b. Per-kind hardcoded label fallback for the equipment screen. The
    //     9 BTN_INV_* slot buttons (and the matching LBL_INV_* labels) have
    //     no inline text and no strref in equip.gui — same situation as the
    //     InGameMenu strip icons. Use the control's stable .gui ID (read at
    //     +0x50) to pick a slot name, prefer a dialog.tlk lookup so a non-
    //     German install reads the engine's own translations, fall back to
    //     a strings.h literal (which adapts to active language).
    //
    //     IDs come from equip.gui via xoreos-tools gff2xml; same .gui maps
    //     button ID → matching label ID (n+1) so we cover both with one
    //     spec table by listing both ids for each slot.
    if (!source && ownerForPerkind &&
        IdentifyPanel(ownerForPerkind) == PanelKind::InGameEquip) {
        struct EquipSlotName {
            int           btnId;
            int           lblId;
            uint32_t      strref;     // 0xFFFFFFFF = no TLK, use literal
            acc::strings::Id literalId;
        };
        static const EquipSlotName k_equipSlots[] = {
            { kEquipBtnHeadId,     kEquipBtnHeadId    + 1, 31375u,      acc::strings::Id::EquipSlotHead    },
            { kEquipBtnImplantId,  kEquipBtnImplantId + 1, 0xFFFFFFFFu, acc::strings::Id::EquipSlotImplant },
            { kEquipBtnBodyId,     kEquipBtnBodyId    + 1, 31380u,      acc::strings::Id::EquipSlotBody    },
            { kEquipBtnArmLId,     kEquipBtnArmLId    + 1, 31376u,      acc::strings::Id::EquipSlotArmL    },
            { kEquipBtnArmRId,     kEquipBtnArmRId    + 1, 31377u,      acc::strings::Id::EquipSlotArmR    },
            { kEquipBtnWeapLId,    kEquipBtnWeapLId   + 1, 31378u,      acc::strings::Id::EquipSlotWeapL   },
            { kEquipBtnWeapRId,    kEquipBtnWeapRId   + 1, 31379u,      acc::strings::Id::EquipSlotWeapR   },
            { kEquipBtnBeltId,     kEquipBtnBeltId    + 1, 31382u,      acc::strings::Id::EquipSlotBelt    },
            { kEquipBtnHandsId,    kEquipBtnHandsId   + 1, 31383u,      acc::strings::Id::EquipSlotHands   },
        };
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(control) + 0x50);
        for (const auto& s : k_equipSlots) {
            if (s.btnId != cid && s.lblId != cid) continue;
            bool gotTlk = false;
            if (s.strref != 0xFFFFFFFFu) {
                char tlkText[256];
                if (LookupTlk(s.strref, tlkText, sizeof(tlkText))) {
                    size_t tlen = strnlen(tlkText, sizeof(tlkText));
                    if (tlen > 0 && tlen + 1 <= bufSize) {
                        memcpy(outBuf, tlkText, tlen + 1);
                        source = "perkind-equip-tlk";
                        gotTlk = true;
                        acclog::Write("Per-kind InGameEquip TLK: control=%p "
                                      "id=%d strref=%u -> \"%s\"",
                                      control, cid, s.strref, outBuf);
                    }
                }
            }
            if (!gotTlk) {
                const char* lit = acc::strings::Get(s.literalId);
                size_t llen = strlen(lit);
                if (llen > 0 && llen + 1 <= bufSize) {
                    memcpy(outBuf, lit, llen + 1);
                    source = "perkind-equip-literal";
                    acclog::Write("Per-kind InGameEquip literal: control=%p "
                                  "id=%d strref=%u -> \"%s\"",
                                  control, cid, s.strref, outBuf);
                }
            }
            break;
        }
    }

    // 9. Sibling-label fallback for chain-navigable controls with no text.
    //    Image-only icon buttons (vtable=0x0073E658 in CSWGuiInGameMenu —
    //    Equipment / Inventory / Character / Map / Abilities / Journal /
    //    Options / Messages icons) genuinely have no inline text. Their
    //    visible name lives on a separately-allocated CSWGuiLabelHilight
    //    sibling at the same x-coord. FindSiblingLabel locates that sibling
    //    spatially; we then announce its text as if it were our own. Same
    //    pattern the slider extraction (step 6) uses for category labels.
    //
    //    Gated on IsChainNavigable(control) so we only fire for buttons
    //    the user can actually focus — a label on its own (already
    //    extractable elsewhere) wouldn't hit this path.
    if (!source && g_currentPanel && IsChainNavigable(control)) {
        char label[256];
        if (FindSiblingLabel(g_currentPanel, control,
                             label, sizeof(label))) {
            size_t llen = strnlen(label, sizeof(label));
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, label, llen + 1);
                source = "siblinglabel-fallback";
                acclog::Write("Sibling-label fallback hit: control=%p label=\"%s\"",
                              control, outBuf);
            }
        }
    }

    // CSWGuiEditbox — the engine doesn't expose an AsEditbox accessor in
    // GuiControlMethods, and we don't yet know its struct layout well
    // enough to read fields by speculative offsets. OnSetActiveControl
    // logs the vtable pointer for any control we can't extract; map via
    // SARIF + add per-class extraction here.

    // Cycle value-display prefix. Cycle widgets render as `[◀] value [▶]`
    // and the engine rewrites the middle button's CExoString to the new
    // value on each activate, losing the category name. We capture the
    // category at panel-walk time (in OnSetActiveControl, before any
    // activation has run); here we just look it up. Skipped for toggles
    // (whose own text already reads as "{label}, {state}") and for
    // non-cycle buttons (LookupCycleCategory returns null when control
    // isn't in the cycle map). Redundancy guard: if the captured
    // "category" is byte-identical to the current rendered value we
    // suppress the prefix — that's the failure mode where capture caught
    // the value rather than the category (timing-dependent; see
    // OnSetActiveControl), and "Normal, Normal" is worse than just "Normal".
    if (source && !IsToggle(control)) {
        const char* category = LookupCycleCategory(control);
        if (category && strcmp(category, outBuf) != 0) {
            char value[256];
            strncpy_s(value, outBuf, _TRUNCATE);
            snprintf(outBuf, bufSize, "%s, %s", category, value);
        }
    }

    // Append element-state suffix for toggles. Detected via the same downcast
    // we'd use for text extraction, so works regardless of which path
    // returned the label (most toggles are caught by AsButton at step 2).
    if (source && IsToggle(control)) {
        bool on = ReadToggleState(control);
        size_t len = strnlen(outBuf, bufSize);
        const char* suffix = on ? ", ein" : ", aus";
        size_t suffixLen = strlen(suffix);
        if (len + suffixLen + 1 <= bufSize) {
            memcpy(outBuf + len, suffix, suffixLen + 1);
        }
    }

    return source;
}

// Multi-line "blob" listbox readout. The Options-Gameplay settings list is
// the canonical case: CSWGuiListBox.controls.size == 1, the single child is a
// CSWGuiLabel whose CExoString contains all visible setting names joined by
// Speak `text` only if it differs from what we last spoke on this channel.
// Dedup is the only filter: in the first session we used interrupt=true and
// NVDA went fully silent in chargen (every utterance got cut off mid-word
// because focus events fire ~10/sec while panels initialize). Switching to
// interrupt=false (queued) lets NVDA finish each line at its own pace; the
// user can still skip forward with NVDA's own ctrl-key shortcut.
//
// Channels keep dedup state independent so a listbox row update doesn't
// silence the parent panel's announcement and vice-versa:
//   0 = panel SetActiveControl
//   1 = listbox row SetActiveControl
static void SpeakIfChanged(int channel, const char* text) {
    static char s_last[2][256] = {{0}, {0}};
    if (channel < 0 || channel >= 2 || !text) return;
    if (strncmp(s_last[channel], text, sizeof(s_last[channel])) == 0) return;
    strncpy_s(s_last[channel], text, _TRUNCATE);
    tolk::Speak(text, /*interrupt=*/false);
}

// ============================================================================
// Unified-cursor menu navigation (Phase 1+2 — see docs/menu-nav-design.md).
// ============================================================================

// CSWGuiManager / cursor / click-sim surfaces moved to engine_manager.{h,cpp}
// in Phase 0 lay-off 4: kAddrGuiManagerPtr, kMgr*Offset, MoveMouseToPosition
// + click-sim PFN typedefs and addresses, FindOwningPanel, GetForegroundPanel,
// LogManagerStack.

// CSWGuiPanel::SetActiveControl @ 0x40a630 — committing selection to a panel.
// MoveMouseToPosition only updates hover state; panel.activeControl lags
// behind the cursor unless we explicitly set it. Enter / F1 activates
// panel.activeControl, so without this call the engine activates the
// previously-clicked button instead of the cursor target.
constexpr uintptr_t kAddrPanelSetActiveControl = 0x0040a630;
typedef void (__thiscall* PFN_PanelSetActiveControl)(void* panel, void* control);

// CSWGuiControl::HandleInputEvent — vtable slot 15 (offset 0x3C) per the
// GuiControlMethods struct in docs/llm-docs/re/swkotor.exe.h. Direct fire of
// event 0x27 (KEYBOARD_F1 / "activate") on a control invokes its onClick
// pipeline without going through the click-sim path. Needed when the button
// we want to activate is rendered behind/under a CSWGuiListBox whose extent
// covers the button's hit area: the engine's hit-test resolves the cursor
// to the listbox instead of the button, and click-sim ends up clicking the
// wrong control. By firing 0x27 directly on the button we bypass hit-test
// entirely. Confirmed safe for non-tab buttons (Schliess, OK, Standard,
// chain targets in sub-dialogs).
//
// kInputActivate is exported via engine_input.h.
constexpr int kVtableHandleInputEvent = 15;
typedef void (__thiscall* PFN_ControlHandleInputEvent)(void* this_, int code, int state);

static void FireActivate(void* control) {
    if (!control) return;
    void** vtable = *reinterpret_cast<void***>(control);
    if (!vtable) return;
    auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
        vtable[kVtableHandleInputEvent]);
    if (!fn) return;
    fn(control, kInputActivate, 1);
}

// Logical input codes (kInputNav*, kInputEnter1/2, kInputEsc1/2,
// kInputActivate) are defined in engine_input.h. They're the codes
// CSWGuiManager::HandleInputEvent receives pre-translation; see
// ManagerTranslateCode for what each maps to post-translation.
//
// Up/Down (0xb6/0xb7) and Left/Right (0xb8/0xb9) are the engine's nav-prev/
// nav-next and horizontal-axis equivalents — consuming Up/Down prevents the
// engine's broken `.gui` focus-cycle from running. Left/Right are consumed
// selectively (slider passes through, otherwise dispatched to a cycle-arrow
// neighbour). Enter (0xb5/0xbb → KEYBOARD_F1) and Esc (0xb4/0xdf → KEYBOARD_F2)
// route to our chain-target activation and Schliess-button fallback paths.

// Chain state. g_currentPanel is updated in OnSetActiveControl. g_chainPanel
// is rebound lazily per arrow press if the focused panel has changed since the
// last navigation. A null current panel disables chain nav (fall through to
// the engine for unsupported screens — e.g. the title-screen K/L cycle still
// works, per Decision 7).
//
// The chain is FLAT: each entry is one navigable button, even if that button
// lives inside a CSWGuiListBox (Options-style sub-dialogs put their settings
// as button children of a listbox at panel.controls[N]). Building the chain
// recurses one level into listboxes when their controls.size > 1 and their
// children are buttons — sub-dialogs in KOTOR don't nest deeper than that.
// Entries are sorted by extent.top ascending so arrow-down walks visually
// top-to-bottom (panel.controls order doesn't always match visual order:
// in Feedback-Optionen, Schliess+Standard come before the settings listbox
// in panel.controls but render below it).
struct ChainEntry {
    void* control;
    int   cx;
    int   cy;
};
constexpr int kMaxChainEntries = 64;
static ChainEntry g_chain[kMaxChainEntries];
// Default-linkage so ExtractAnnounceableText can forward-declare it via
// extern (function is defined earlier in the file but needs the panel
// pointer for slider sibling-label / cycle-category lookups).
void* g_currentPanel = nullptr;
static void* g_chainPanel   = nullptr;
static int   g_chainIndex   = 0;
static int   g_chainCount   = 0;

// Sub-screen drill state. The InGameMenu icon strip is kept in foreground by
// the engine: each icon's onClick (OnInvButtonPressed @0x624d10 etc.) jumps
// into CGuiInGame::SwitchToSWInGameGui @0x62cf10, which calls AddPanel for the
// new sub-screen and then SendPanelToBack on it — the strip stays on top
// (verified via SARIF xref trace). Without intervention our chain therefore
// keeps targeting the strip's 8 icons and the user can never reach the
// sub-screen's content (item rows, quest rows, settings buttons).
//
// Drill model: Enter on a strip icon arms this flag. The chain-target router
// in OnHandleInputEvent then prefers FindActiveSubScreenPanel() over the
// engine's foreground when fg is the strip — so arrows step through the
// sub-screen instead. Esc clears the flag (returns to strip nav). The flag
// also self-clears when the sub-screen leaves panels[].
//
// Override is gated on fg-is-the-strip: while a tutorial modal or an
// Options sub-tab is on top, fg is something else and we route to that
// directly (no double-override). Once the modal/sub-tab closes and fg
// returns to the strip, the override re-engages.
static bool g_drilledIntoSubScreen = false;

// Equipment picker zone arming. The InGameEquip screen has two interaction
// zones in one panel: the 9-slot paper-doll grid (default) and the LB_ITEMS
// item list, entered by pressing Enter on a slot. Arrow keys mean different
// things in each zone, so OnHandleInputEvent gates routing on this flag.
//
// Set when Enter activates a BTN_INV_* slot. Cleared by Esc, by Enter on
// BTN_EQUIP (after dispatch), by panel close, or by the panel falling out
// of panels[] (handled in MonitorEquipPickerSelection). The panel pointer
// guards against stale arming across a close/reopen.
static bool  g_equipPickerActive = false;
static void* g_equipPickerPanel  = nullptr;

// Forward decl: find an InGame{X} sub-screen panel currently in panels[].
// Defined near the sub-screen spec table to share the kind set with
// AnnounceNewSubScreens. Returns the lowest-index match, or nullptr if no
// sub-screen is currently pushed.
static void* FindActiveSubScreenPanel();

// Forward decl matching the InGameSubScreenSpec table defined alongside the
// content-monitor whitelist. Used by the Esc-drill handler to test
// "is this panel one of the in-game sub-screens we drill into?" without
// depending on the spec struct's definition order.
struct InGameSubScreenSpec;
static const InGameSubScreenSpec* FindInGameSubScreenSpec(PanelKind k);

// Deferred MoveMouseToPosition. Called from OnHandleInputEvent would recurse
// through HandleMouseMove → UpdateMouseOverControl mid-input-dispatch — same
// class of toxicity as the listbox-entry hooks from session 4. Defer to the
// next CSWGuiManager::Update tick (~16ms at 60fps; inaudible). Tolk speech
// still fires synchronously from the input hook so the audible response feels
// instantaneous.
static bool  g_pendingCursorMove = false;
static int   g_pendingX = 0;
static int   g_pendingY = 0;
static void* g_pendingTarget = nullptr;   // for self-dedup in OnSetActiveControl

// Speech-suppression budget for OnSetActiveControl. After a voluntary nav
// action (chain step / Enter activate), set to a small N. Each subsequent
// OnSetActiveControl call decrements and suppresses speech regardless of
// which control the event targets. Covers two distinct echoes per nav:
//
//   1. The engine's own focus handler firing on the keypress (lands on a
//      DIFFERENT control than our chain target on Options-style sub-dialogs
//      and InGameEquip — engine's nav order ≠ visual layout).
//   2. The cursor-warp echo, which lands on our actual target. The existing
//      g_pendingTarget self-dedup already catches this one cleanly.
//
// (1) was the source of the "afterthought" double-speak: chain-step speaks
// the right thing, then engine SetActiveControl fires for a sibling and
// speaks it as a second utterance. Match-only dedup couldn't catch (1)
// because newControl wasn't the pendingTarget. Budget=2 catches both echoes
// without over-suppressing legitimate later focus changes (mouse hover,
// next user action), since by the time the next user input arrives the
// budget has decremented to 0.
static int g_navSpeechSuppressBudget = 0;

// Deferred click-sim. When set, OnUpdate dispatches click directly to
// g_pendingClickTarget via its vtable[6] (HandleLMouseDown) and vtable[7]
// (HandleLMouseUp). We bypass the manager's HandleLMouseDown wrapper because
// its UpdateMouseOverControl misidentifies the cursor's hit target on tabbed
// panels (consistent 45-px shift in Options panel — see chat investigation
// in patch-20260502-114830.log). The button's own HandleLMouseDown still
// runs CaptureMouse + state setup, and HandleLMouseUp fires the actual
// onClick — that's what the manager's wrapper would have called once it
// resolved the right button. We just provide the target ourselves.
//
// Distinct from g_pendingActivate (vtable[15] FireActivate path): tabs gate
// on `is_active` which only HandleLMouseDown sets, so direct activate no-ops
// for tabs (see comment block below).
static bool  g_pendingClick       = false;
static void* g_pendingClickTarget = nullptr;

// Deferred direct-activate via vtable[15].HandleInputEvent(0x27, 1). Used for
// targets whose click hit-area is covered by an overlapping CSWGuiListBox
// (Schliess. and Standard inside Options sub-dialogs, chain-navigated
// settings buttons, etc.) — click-sim would land on the listbox instead of
// the button. Direct activate bypasses hit-test entirely.
static bool  g_pendingActivate       = false;
static void* g_pendingActivateTarget = nullptr;

// Deferred slider value adjustment. Slider's HandleInputEvent at 0x0041adf0
// recognises logical codes 500 (increment) and 501 (decrement) — both run
// the full pipeline: SetCurValue + bounds clamp + the slider's gui_object
// callback (which is what actually changes the audio system's volume for
// Music/Voice/SFX/Movie sliders) + PlayGuiSound feedback. We dispatch via
// vtable[15] from OnUpdate rather than synchronously from OnHandleInputEvent
// to stay clear of mid-input-dispatch re-entrancy (same reason
// MoveMouseToPosition is deferred). Per-frame focus monitor catches the
// resulting cur_value change on the next tick and re-announces.
static bool  g_pendingSliderInput       = false;
static void* g_pendingSliderTarget      = nullptr;
static int   g_pendingSliderCode        = 0;

// Tracks the last panel for which we spoke the title (AnnouncePanelTitle).
// Re-entering the same panel pointer must not re-announce. A distinct static
// from the s_lastPanel inside OnSetActiveControl — that one drives the
// diagnostic WalkChildren logging.
static void* g_lastTitledPanel = nullptr;

// Per-frame focus state monitor. Snapshots the announceable text of the
// focused chain entry on each focus change; on every OnUpdate tick re-extracts
// and re-announces if the text has changed since the snapshot. This is the
// generic mechanism that catches every state mutation visible through
// ExtractAnnounceableText: toggle on/off flips, cycle-button value changes
// (engine rewrites the value-display button's CExoString in place when the
// user activates a flanking arrow), slider cur_value adjustments, etc. New
// widget types get the behaviour for free as soon as their text/state
// extraction lands in ExtractAnnounceableText.
static void* g_focusMonitorControl = nullptr;
static char  g_focusMonitorText[256] = {0};

// Cycle-button category cache. KOTOR cycle widgets (Difficulty etc.) are a
// CSWGuiButton flanked by two empty-text arrow buttons. The middle button's
// CExoString starts as the localized category name (e.g. "Schwierigkeitsgrad"
// in German) on first panel render — but the engine REPLACES it with the
// current value text ("Normal", "Leicht") the moment any cycle activation
// runs, including ours via FireActivate(arrow). To preserve the category for
// subsequent value-change announcements we capture it during chain rebind,
// before any activation has rewritten the field. Map is invalidated on
// every RebindChain.
struct CycleCategoryEntry {
    void* control;
    char  category[128];
};
constexpr int kMaxCycleCategoryEntries = 16;
static CycleCategoryEntry g_cycleCategories[kMaxCycleCategoryEntries];
static int g_cycleCategoryCount = 0;

static const char* LookupCycleCategory(void* control) {
    for (int i = 0; i < g_cycleCategoryCount; ++i) {
        if (g_cycleCategories[i].control == control) {
            return g_cycleCategories[i].category;
        }
    }
    return nullptr;
}

// Tabbed-panel detection state (Options-menu style: a CSWGuiListBox at
// controls[0] holds the current tab's content, button cluster after [0] = tabs).
// Detection runs in OnListBoxSetActiveControl and identifies the layout so the
// per-line virtual-cursor mode can engage on arrow keys. Tab cycling itself is
// no longer an explicit handler — the click-sim primitive (Phase 3) replaces
// the SetActiveControl-based path that crashed at mgr+5 (see
// docs/tab-crash-investigation.md).
//
// Detection deliberately keys off "controls[0] is a non-empty CSWGuiListBox"
// rather than "panel has buttons" — the main menu also has buttons but no
// active listbox, and arrow-keys must continue to drive chain navigation
// there. kVtableListBox itself is declared earlier alongside kVtableSlider
// because the listbox-content extraction step in ExtractAnnounceableText
// needs it; future builds will need updating if Lane's SARIF reports a
// different value.

static void*      g_tabbedPanel       = nullptr;  // panel currently in tabbed mode
static int        g_tabsStart         = -1;       // first tab-button index in panel.controls
static int        g_tabsCount         = 0;        // number of contiguous tab buttons

// Cursor-y offset to compensate for the engine's hit-test shift in tab-cluster
// panels. MoveMouseToPosition(x, y) on the Options panel hit-tests at the
// button whose center is at (y - tabSpacing) — consistently 45 px on Steam
// 1.0.3 (matches the tab pitch). Cause is unverified (best guess: cursor
// hotspot coord-system mismatch — see chat investigation in
// patch-20260502-122734.log line 91 where before=NULL after=Gameplay confirms
// MoveMouseToPosition itself produces the shifted hit-test result, not stale
// engine state). Real mouse usage is unaffected, so the engine ships fine for
// sighted players. We compensate by adding this offset to y when warping the
// cursor to a tab button. Computed in RebindChain from the chain's tab-cluster
// spacing; 0 for non-tabbed panels (main menu, popups, sub-dialogs) — those
// panels' MoveMouseToPosition already hits where it should.
static int        g_tabClickOffsetY   = 0;
// Hit-test row-shift compensation for InGameEquip slot buttons. Same shape as
// `g_tabClickOffsetY`: clicking at the chain entry's (cx, cy) hit-tests to a
// control one row above (the LBL_INV_* label of the slot above), so cursor
// must be biased down by one row's pitch to actually land on the slot button.
// Computed at chain rebind time as the y-spacing between two slot buttons in
// the chain. 0 outside InGameEquip panels.
static int        g_equipSlotClickOffsetY = 0;

// Virtual-line cursor over the listbox's multi-line blob. The Options listbox
// has controls.size == 1 with all settings concatenated by '\n' into a single
// CSWGuiLabel row. We can't activate individual lines (engine has no per-line
// click target — see project_listbox_click_flow.md) but we can present them as
// readable navigable items.
constexpr int kMaxVirtualLines  = 32;
constexpr int kMaxVirtualLineLen = 256;
static char g_virtualLines[kMaxVirtualLines][kMaxVirtualLineLen];
static int  g_virtualLineCount = 0;
static int  g_virtualLineIdx   = -1;  // -1 = not yet entered (cursor at tab level)

// Read panel.activeControl @ +0x1c. Used to anchor the chain index when we
// rebind to a panel — start at the engine's current selection so the first
// arrow press doesn't snap the cursor away from where the user was looking.
static void* ReadPanelActiveControl(void* panel) {
    if (!panel) return nullptr;
    return *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(panel) + kPanelActiveControlOffset);
}

// Center pixel of a control's hit area. Returns false on null control or
// degenerate extent (zero/negative width/height — sometimes seen on hidden
// panels and templated control prototypes).
static bool GetControlCenter(void* control, int& outCx, int& outCy) {
    if (!control) return false;
    auto* ext = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + kControlExtentOffset);
    int width  = ext[2];
    int height = ext[3];
    if (width <= 0 || height <= 0) return false;
    outCx = ext[0] + width  / 2;
    outCy = ext[1] + height / 2;
    return true;
}

// Screen-absolute center of a CSWGuiListBox row. Listbox children's extents
// are listbox-local (origin at the listbox's top-left, not the screen) so
// click-sim at row.extent alone lands on dead space. Add the listbox's own
// extent origin to translate. Listboxes themselves are panel-direct children
// whose extents are already screen-absolute (panels render at fixed
// positions), so one accumulation step is sufficient for the InGameEquip
// LB_ITEMS case. If we ever need to click rows in a deeper-nested listbox,
// generalise this into a parent-chain walk.
static bool GetListBoxRowScreenCenter(void* lb, void* row, int& outCx, int& outCy) {
    if (!lb || !row) return false;
    auto* lbExt  = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(lb)  + kControlExtentOffset);
    auto* rowExt = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(row) + kControlExtentOffset);
    int rowW = rowExt[2];
    int rowH = rowExt[3];
    if (rowW <= 0 || rowH <= 0) return false;
    outCx = lbExt[0] + rowExt[0] + rowW / 2;
    outCy = lbExt[1] + rowExt[1] + rowH / 2;
    return true;
}

// True if the control is button-like (CSWGuiButton or its subclasses
// CharButton / ActivatedButton / ButtonToggle) OR a CSWGuiSlider.
// MoveMouseToPosition's hover→active promotion path is safe for buttons but
// crashes when the active control is a label (verified: navigating onto the
// main-menu "Neue Inhalte verfügbar…" label froze the game). Sliders are
// included because Sound's Music/Voice/SFX/Movie controls are real sliders
// and we want chain navigation to land on them so we can announce their
// numeric value.
//
// Long-term: replace with a proper CSWGuiControl::GetIsSelectable call
// (vtable lookup at 0x4189d0) to also include editbox / listbox / etc.
static bool IsChainNavigable(void* control) {
    if (!control) return false;
    if (CallDowncast(control, kVtableAsButton)        != nullptr) return true;
    if (CallDowncast(control, kVtableAsButtonToggle)  != nullptr) return true;
    if (IsSlider(control))                                        return true;
    return false;
}

// Linear scan g_chain for `control`. Returns the chain index or -1.
// Used to anchor the chain at the engine's currently active control on
// rebind so the first arrow press doesn't snap the cursor to chain[0].
static int FindChainEntry(void* control) {
    if (!control) return -1;
    for (int i = 0; i < g_chainCount; ++i) {
        if (g_chain[i].control == control) return i;
    }
    return -1;
}

// Pull the announceable text of a control (tooltip → button → label → ...);
// fall back to "control N" using the control's id field. Never silently
// drops — per feedback_never_silence_fallback_announcement.md.
static void AnnounceControl(void* control) {
    if (!control) return;
    char text[256];
    const char* source = ExtractAnnounceableText(control, text, sizeof(text));
    if (source) {
        tolk::Speak(text, /*interrupt=*/false);
        return;
    }
    int id = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(control) + 0x50);
    char placeholder[64];
    snprintf(placeholder, sizeof(placeholder), "control %d", id);
    tolk::Speak(placeholder, /*interrupt=*/false);
}

// First focus into a panel speaks the panel's "title" — the first label-like
// child we can find — so the user knows which menu they're in. Subsequent
// per-control announcements still fire from OnSetActiveControl as the user
// navigates, so this is just the entry banner, not a layout dump.
//
// Heuristic: walk panel.controls in order, return the text of the first
// CSWGuiLabel / CSWGuiLabelHilight child with announceable text. Buttons and
// other interactive controls are skipped — they get announced through the
// regular focus path. If no label exists we stay silent and rely on the
// SetActiveControl announcement of the focused child to orient the user.
static void AnnouncePanelTitle(void* panel) {
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* child = list->data[i];
        if (!child) continue;
        if (CallDowncast(child, kVtableAsLabel) == nullptr &&
            CallDowncast(child, kVtableAsLabelHilight) == nullptr) {
            continue;
        }
        char text[256];
        if (ExtractAnnounceableText(child, text, sizeof(text), panel)) {
            acclog::Write("Panel title parent=%p label=%p text=\"%s\"",
                          panel, child, text);
            tolk::Speak(text, /*interrupt=*/false);
            return;
        }
    }
}

// Detect whether `panel` is laid out as Options-style: a CSWGuiListBox at
// controls[0] (currently displayed tab content) followed by a contiguous
// cluster of buttons (the tab strip). Returns true and fills outStart/outCount
// with the cluster's first index and length on success.
//
// Refusing the detection when the listbox is empty keeps main-menu-style
// panels (which also have a CSWGuiListBox at [0], for the news scroller, but
// no tab content) on the existing chain-navigation path.
static bool DetectTabsCluster(void* panel, int& outStart, int& outCount) {
    outStart = -1;
    outCount = 0;
    if (!panel) return false;

    auto* panelList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!panelList->data || panelList->size < 2) return false;

    void* lb = panelList->data[0];
    if (!lb) return false;
    void** vt = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(vt) != kVtableListBox) return false;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList->data || lbList->size <= 0) return false;

    int n = panelList->size > 256 ? 256 : panelList->size;
    int start = -1, end = -1;
    for (int i = 1; i < n; ++i) {
        void* c = panelList->data[i];
        if (c && IsChainNavigable(c)) {
            if (start < 0) start = i;
            end = i;
        } else if (start >= 0) {
            break;  // first non-navigable after the cluster ends it
        }
    }
    if (start < 0 || (end - start + 1) < 2) return false;
    outStart = start;
    outCount = end - start + 1;
    return true;
}

// Reset all tabbed-mode state. Called when the focused panel changes to a
// different one — the new panel may or may not be tabbed; OnListBoxSetActive-
// Control re-runs detection lazily on its first listbox event.
static void ResetTabbedState() {
    g_tabbedPanel      = nullptr;
    g_tabsStart        = -1;
    g_tabsCount        = 0;
    g_virtualLineCount = 0;
    g_virtualLineIdx   = -1;
}

// Split `text` on '\n' into g_virtualLines[]. Truncates oversize lines to fit;
// caps total line count at kMaxVirtualLines (Options Gameplay tab has 8, no
// observed listbox blob > 16 in any KOTOR menu — 32 is comfortable headroom).
static void ParseVirtualLines(const char* text) {
    g_virtualLineCount = 0;
    g_virtualLineIdx   = -1;
    if (!text) return;
    const char* p = text;
    while (*p && g_virtualLineCount < kMaxVirtualLines) {
        const char* end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= kMaxVirtualLineLen) len = kMaxVirtualLineLen - 1;
        memcpy(g_virtualLines[g_virtualLineCount], p, len);
        g_virtualLines[g_virtualLineCount][len] = '\0';
        ++g_virtualLineCount;
        if (!end) break;
        p = end + 1;
    }
}

// Container loot panel control IDs from container.gui (extracted via
// xoreos-tools from data/gui.bif). Stable per panel kind across patch versions.
// Used by the Container input handler in OnHandleInputEvent and the per-row
// monitor MonitorContainerSelection further down. (Equipment IDs live near
// the top of the file because ExtractAnnounceableText needs them; container
// IDs aren't referenced until the input handler ~800 lines below so they
// stay co-located here with the Container helpers.)
constexpr int kContainerLbItemsId   = 2;
constexpr int kContainerBtnOkId     = 3;
constexpr int kContainerBtnGiveId   = 4;
constexpr int kContainerBtnCancelId = 5;

// Forward declaration — body lives next to MonitorDialogReplies (which is
// the long-standing first-and-only caller). Container input handler in
// OnHandleInputEvent now also uses it for arrow-key selection_index drive.
static void* FindListBoxChild(void* panel);

// Locate a child control on `panel` by its +0x50 ID field. The .gui-time IDs
// are stable per panel kind, so this is the canonical way to address a known
// control in a known panel without text-matching (which breaks across
// localizations) or relying on panel.controls index (which can shift).
static void* FindControlById(void* panel, int id) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 64 ? 64 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(c) + 0x50);
        if (cid == id) return c;
    }
    return nullptr;
}

// Find the "close" button on a panel — the back/Schliess button we'd click
// to dismiss the panel. Used by the Esc handler to route Esc to the same
// FireActivate primitive the user triggers manually by Enter-ing Schliess.
//
// Match heuristic: scan panel.controls for a navigable button whose text
// starts with "Schliess" (German "Schließen"), "Close" (English), or "OK"
// (universal "accept and dismiss" in confirmation dialogs). KOTOR ships
// German + English localizations; both texts present here cover the cases
// we care about. Returns the control pointer or nullptr if none found.
static void* FindCloseButton(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!IsChainNavigable(c)) continue;
        char text[256];
        if (!ExtractAnnounceableText(c, text, sizeof(text), panel)) continue;
        if (strncmp(text, "Schliess", 8) == 0 ||
            strncmp(text, "Close",    5) == 0 ||
            strncmp(text, "OK",       2) == 0) {
            return c;
        }
    }
    return nullptr;
}

// Find the closest navigable empty-text neighbour of `focused` in
// `panel.controls`, on the visual left or right at the same y-row. Used to
// dispatch Left/Right arrow presses to cycle-button flanker arrows: the
// Difficulty cycle (and similar) renders as `[◀] Normal [▶]` — three plain
// CSWGuiButtons, where the middle one carries the value text and the flanks
// carry an image overlay only. We want Left/Right on the value-display button
// to fire the corresponding flanker so the engine cycles the value.
//
// Heuristic:
//   - Same-row: |cy_neighbour - cy_focused| <= 5 px (allows for off-by-one
//     baseline alignment; KOTOR cycle layouts visually match much tighter).
//   - Empty-text: ExtractAnnounceableText returns nullptr. Real labels and
//     toggles are excluded so the "neighbour" is unambiguously a flanker.
//   - Closest by signed dx: smaller |dx| wins among candidates strictly to
//     the right (toRight=true) or left (toRight=false) of the focused control.
//
// Returns nullptr if no flanker found — caller falls back to "consume the
// keypress with no action" so we don't trigger surprising native behaviour.
static void* FindAdjacentArrow(void* panel, void* focused, bool toRight) {
    if (!panel || !focused) return nullptr;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;

    int focusCx, focusCy;
    if (!GetControlCenter(focused, focusCx, focusCy)) return nullptr;

    void* best = nullptr;
    int bestDx = 0x7fffffff;

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == focused) continue;
        if (!IsChainNavigable(c)) continue;

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;
        if (cy - focusCy > 5 || focusCy - cy > 5) continue;

        int dx = toRight ? (cx - focusCx) : (focusCx - cx);
        if (dx <= 0) continue;

        // Only consider empty-text neighbours — real labels / toggles are not
        // cycle flankers.
        char tmp[64];
        if (ExtractAnnounceableText(c, tmp, sizeof(tmp), panel)) continue;

        if (dx < bestDx) {
            bestDx = dx;
            best   = c;
        }
    }
    return best;
}

// Find the closest label-like sibling of `control` on the panel. Two
// candidate positions:
//
//   1. Same y-row, to the visual LEFT (horizontal layouts, e.g. labelled
//      buttons on a config row — `[Schliess]   Standard`).
//   2. Directly ABOVE the control (vertical layouts, e.g. KOTOR's Sound
//      panel where each slider has a label rendered on the line above it
//      rather than to its left).
//
// Picks the candidate with the smallest scoring distance: Manhattan
// distance for "above" candidates so a label slightly off-axis but close
// in y wins over a same-row label that's far to the left. Strict
// thresholds keep us from matching the panel title (which is far above
// any individual widget) or unrelated controls in adjacent rows.
//
// Returns the source tag ("siblinglabel") and writes the label's text
// into outBuf, or nullptr if no suitable label was found. Pure read;
// doesn't recurse through ExtractAnnounceableText so it's safe to call
// from inside extraction code.
static const char* FindSiblingLabel(void* panel, void* control,
                                    char* outBuf, size_t bufSize) {
    if (!panel || !control || bufSize < 2) return nullptr;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;

    int targetCx, targetCy;
    if (!GetControlCenter(control, targetCx, targetCy)) return nullptr;

    void* best = nullptr;
    int bestScore = 0x7fffffff;

    // Tolerances. Same-row dy<=5; vertical offset (above OR below) up to
    // 50 px (typical KOTOR row height is ~30-55 px) with dx tolerance 80
    // px (label can be slightly offset from the widget center, e.g.
    // left-aligned label vs centered slider). Search expanded vs the
    // original same-row-left + above-only set so InGameMenu-style
    // captioned icons (label below the button) also match.
    constexpr int kSameRowDyTol  = 5;
    constexpr int kVertDyMax     = 50;
    constexpr int kVertDxMax     = 80;

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == control) continue;

        if (CallDowncast(c, kVtableAsLabel) == nullptr &&
            CallDowncast(c, kVtableAsLabelHilight) == nullptr) {
            continue;
        }

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;

        int dy    = cy - targetCy;          // negative = label is above
        int adx   = cx - targetCx;          // negative = label is to the left
        int absDx = adx < 0 ? -adx : adx;
        int absDy = dy  < 0 ? -dy  : dy;

        int score = 0x7fffffff;
        // Same row — favour LEFT siblings (existing behaviour for slider
        // labels) but also accept right-of-target as a fallback so we
        // don't reject all icon-with-trailing-caption layouts.
        if (absDy <= kSameRowDyTol) {
            // Left has score = absDx (lower is better);
            // right gets a small penalty so left wins on ties.
            score = (cx < targetCx) ? absDx : (absDx + 8);
        }
        // Vertically displaced (above OR below) with similar x. Below-target
        // captioned-icon layouts (label below the icon button) need the
        // dy>0 case too. Manhattan distance scoring keeps the closest
        // candidate winning when multiple labels could pair.
        else if (absDy <= kVertDyMax && absDx <= kVertDxMax) {
            score = absDx + absDy;
        }
        else {
            continue;
        }

        if (score < bestScore) {
            bestScore = score;
            best      = c;
        }
    }

    if (!best) return nullptr;
    if (ExtractTextOrStrRefIndirect(best,
                                    kLabelTextOffset,
                                    kLabelStrRefOffset,
                                    kLabelTextObjectOffset,
                                    outBuf, bufSize)) {
        return "siblinglabel";
    }
    return nullptr;
}

// True if `control` is one of the current panel's tab-cluster buttons.
// Used to gate the cursor-y offset (g_tabClickOffsetY) — only tab buttons
// suffer the engine's hit-test shift; close/standard buttons in the same
// panel are in a different x column or row layout and don't need the offset.
static bool IsTabButton(void* control) {
    if (!control || !g_tabbedPanel || g_tabsCount < 2) return false;
    auto* tlist = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
    if (!tlist || !tlist->data) return false;
    for (int i = g_tabsStart;
         i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
        if (tlist->data[i] == control) return true;
    }
    return false;
}

// Append a navigable control to the chain (skipping null/degenerate-extent).
// Internal helper for RebindChain.
static void AppendChainEntry(void* control) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!IsChainNavigable(control))       return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    g_chain[g_chainCount++] = { control, cx, cy };
}

// (Re)bind the chain to the currently focused panel.
//
// Walks panel.controls; for each entry:
//   - direct navigable button → append.
//   - CSWGuiListBox with controls.size > 1 → recurse one level into its
//     children, appending any navigable buttons found there.
// Then sorts entries by extent.top ascending (visual top-to-bottom order)
// because panel.controls order doesn't match visual order in sub-dialogs.
//
// Finally anchors g_chainIndex on the engine's current activeControl when
// present, so the first arrow press moves one step from where the user
// was, not from chain[0].
static void RebindChain(void* panel) {
    g_chainPanel  = panel;
    g_chainIndex  = 0;
    g_chainCount  = 0;
    if (!panel) return;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;

    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;

        // Direct navigable button — typical case (tabs, OK/Cancel, menu items).
        if (IsChainNavigable(c)) {
            AppendChainEntry(c);
            continue;
        }

        // Listbox with size > 1 — sub-dialogs put their settings here as
        // button children. Recurse one level. Listboxes with size == 1
        // are descriptive multi-line label blobs (the inline tab preview
        // or the per-setting hint pane); their single child is a label,
        // not a button, so AppendChainEntry would skip it anyway.
        void** vt = *reinterpret_cast<void***>(c);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableListBox) {
            auto* lbList = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(c) + kListBoxControlsOffset);
            if (lbList && lbList->data && lbList->size > 1) {
                int lbN = lbList->size > 256 ? 256 : lbList->size;
                for (int j = 0; j < lbN; ++j) {
                    AppendChainEntry(lbList->data[j]);
                }
            }
        }
    }

    // Insertion sort by cy ascending. Stable; n^2 is fine for n<=64.
    for (int i = 1; i < g_chainCount; ++i) {
        for (int j = i; j > 0 && g_chain[j].cy < g_chain[j-1].cy; --j) {
            ChainEntry tmp = g_chain[j];
            g_chain[j]   = g_chain[j-1];
            g_chain[j-1] = tmp;
        }
    }

    // Squash cycle-arrow flankers from the chain. An empty-text navigable
    // entry that shares a y-row with a text-bearing entry is a cycle arrow
    // (left/right of a value-display button: `[◀] Normal [▶]`). The user
    // reaches them via Left/Right cycle dispatch on the value-display
    // entry — having them in the chain just produces "control N"
    // placeholders Up/Down would land on. Lone empty-text entries are
    // kept (we can't say what they are, but the user might want to reach
    // them for Enter activation). Same-row threshold matches
    // FindAdjacentArrow's tolerance.
    {
        int writeIdx = 0;
        for (int i = 0; i < g_chainCount; ++i) {
            char tmp[64];
            bool hasText = ExtractAnnounceableText(g_chain[i].control,
                                                   tmp, sizeof(tmp),
                                                   panel) != nullptr;
            if (hasText) {
                g_chain[writeIdx++] = g_chain[i];
                continue;
            }
            bool sameRowWithText = false;
            for (int j = 0; j < g_chainCount; ++j) {
                if (j == i) continue;
                int dy = g_chain[j].cy - g_chain[i].cy;
                if (dy < 0) dy = -dy;
                if (dy > 5) continue;
                char tmp2[64];
                if (ExtractAnnounceableText(g_chain[j].control,
                                            tmp2, sizeof(tmp2),
                                            panel) != nullptr) {
                    sameRowWithText = true;
                    break;
                }
            }
            if (!sameRowWithText) {
                g_chain[writeIdx++] = g_chain[i];
            }
        }
        g_chainCount = writeIdx;
    }

    // Cycle category capture lives in OnSetActiveControl's panel-walk path,
    // not here — RebindChain runs only on the first arrow press in a panel
    // (typically several seconds after panel open), and by then the engine
    // has already replaced cycle buttons' .gui-default category text with
    // the persisted value (e.g. "Difficulty" -> "Normal"). Capture has to
    // happen at panel-walk time to catch the .gui state before the
    // .ini-driven update.

    // Compute g_tabClickOffsetY from adjacent tab entries' visual spacing.
    // The chain is now sorted top-to-bottom, so the first two consecutive
    // tab-cluster entries give us the pitch directly. Non-tabbed panels (no
    // g_tabbedPanel set, or only one tab) keep offset=0 — their hit-test
    // works without compensation.
    g_tabClickOffsetY = 0;
    if (g_tabbedPanel == panel && g_tabsCount >= 2) {
        int firstTabIdx = -1;
        for (int i = 0; i < g_chainCount; ++i) {
            if (!IsTabButton(g_chain[i].control)) continue;
            if (firstTabIdx < 0) {
                firstTabIdx = i;
            } else {
                int spacing = g_chain[i].cy - g_chain[firstTabIdx].cy;
                if (spacing > 0) g_tabClickOffsetY = spacing;
                break;
            }
        }
    }

    // Compute g_equipSlotClickOffsetY for InGameEquip panels. Walks the chain
    // looking for two slot buttons (matching the BTN_INV_* id set) at different
    // y rows; their pitch gives the click-sim row-shift compensation. The grid
    // is 3x3 so two consecutive chain entries normally share a row — keep
    // scanning until we find a y-jump.
    g_equipSlotClickOffsetY = 0;
    if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
        int firstSlotIdx = -1;
        int firstSlotY   = 0;
        for (int i = 0; i < g_chainCount; ++i) {
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x50);
            bool isSlot =
                cid == kEquipBtnHeadId    || cid == kEquipBtnImplantId ||
                cid == kEquipBtnBodyId    || cid == kEquipBtnArmLId    ||
                cid == kEquipBtnArmRId    || cid == kEquipBtnWeapLId   ||
                cid == kEquipBtnWeapRId   || cid == kEquipBtnBeltId    ||
                cid == kEquipBtnHandsId;
            if (!isSlot) continue;
            if (firstSlotIdx < 0) {
                firstSlotIdx = i;
                firstSlotY   = g_chain[i].cy;
            } else if (g_chain[i].cy != firstSlotY) {
                int spacing = g_chain[i].cy - firstSlotY;
                if (spacing < 0) spacing = -spacing;
                g_equipSlotClickOffsetY = spacing;
                break;
            }
        }
    }

    // Anchor at active. ReadPanelActiveControl reads panel.activeControl
    // (only direct panel children); listbox-internal selection isn't
    // exposed there, so when the user enters a sub-dialog with focus on
    // a listbox child the anchor falls through to chain[0].
    void* active = ReadPanelActiveControl(panel);
    int   idx    = FindChainEntry(active);
    g_chainIndex = (idx >= 0) ? idx : 0;

    acclog::Write("Chain rebind panel=%p count=%d index=%d active=%p "
                  "tabOffsetY=%d equipSlotOffsetY=%d",
                  panel, g_chainCount, g_chainIndex, active,
                  g_tabClickOffsetY, g_equipSlotClickOffsetY);
    for (int i = 0; i < g_chainCount; ++i) {
        char text[256];
        const char* src = ExtractAnnounceableText(g_chain[i].control,
                                                  text, sizeof(text),
                                                  panel);
        // Read CSWGuiControl.is_active (+0x4c) and bit_flags (+0x44) per
        // SARIF struct layout. Hypothesis for chargen wizard buttons that
        // silently no-op on Enter (Talente/Name/Spielen): step-gated, with
        // is_active==0 until prereqs met. We already know `is_active` is
        // what blocks vtable[15] FireActivate on tabs (set only by
        // HandleLMouseDown's CaptureMouse path) — same flag, different
        // gate. If Name shows is_active=0 alongside Fähigkeiten=1, we
        // have our answer.
        unsigned int isActive =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x4c);
        unsigned int bitFlags =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x44);
        acclog::Write("Chain   [%d] %p (%d,%d) %s text=\"%s\" is_active=%u bit_flags=0x%x",
                      i, g_chain[i].control, g_chain[i].cx, g_chain[i].cy,
                      src ? src : "?", src ? text : "", isActive, bitFlags);
    }
}

// Walk a CExoArrayList<CSWGuiControl*> embedded at parent+offset and log every
// child. Used as a diagnostic when the focused panel/listbox changes — gives us
// the full set of widgets on the screen, not just whatever arrow keys reach.
//
// `label` is a short tag that prefixes every line (e.g. "Panel", "ListBox").
// Iteration is capped at 256 entries to limit damage from a corrupt size field
// (defensive: the SARIF datatypes are authoritative but a struct-layout
// regression on a future engine version would otherwise spin forever).
static void WalkChildren(const char* label, void* parent, size_t offset) {
    if (!parent) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(parent) + offset);
    if (!list->data || list->size <= 0) {
        acclog::Write("%s walk parent=%p children=0", label, parent);
        return;
    }
    int count = list->size;
    if (count > 256) {
        acclog::Write("%s walk parent=%p size_oob=%d (capped)", label, parent, count);
        count = 256;
    }
    acclog::Write("%s walk parent=%p children=%d", label, parent, list->size);
    for (int i = 0; i < count; ++i) {
        void* child = list->data[i];
        if (!child) {
            acclog::Write("%s   [%d]=NULL", label, i);
            continue;
        }
        int id = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(child) + 0x50);
        char text[256];
        // Pass `parent` so the perkind fallback resolves correctly when
        // walking InGameMenu's children — the icon labels/buttons have empty
        // CExoString/strref/text_object/gui_string and only resolve via the
        // panel-keyed perkind table.
        const char* source = ExtractAnnounceableText(child, text, sizeof(text),
                                                     parent);
        if (source) {
            acclog::Write("%s   [%d] %p id=%d src=%s text=\"%s\"",
                          label, i, child, id, source, text);
        } else {
            char vtbl[160];
            DumpControlVtable(child, vtbl, sizeof(vtbl));
            acclog::Write("%s   [%d] %p id=%d src=none %s",
                          label, i, child, id, vtbl);
        }
    }
}

// CSWGuiPanel::SetActiveControl — hooked mid-function at 0x0040a638.
// At hook entry: EDI = this (the panel), ESI = param_1 (the new active
// control, possibly null when the panel is deactivating selection).
//
// This is the canonical focus-change signal: fires once per actual change,
// covers arrow-key nav + mouse + programmatic. Speaks the new control's
// tooltip text or, as a placeholder, "control <id>" while we work out how
// to extract subclass-specific labels.
//
// Logging policy:
//   * Resolved events (text extracted) are throttled — they're noisy when
//     the user is just navigating.
//   * Unresolved events (src=none) are ALWAYS logged with the control's
//     vtable pointer, because that's the data we need to identify which
//     subclasses fall through (Slider, Editbox, ListBox row, etc.).
//   * NULL newControl events are also throttled.
extern "C" void __cdecl OnSetActiveControl(void* panel, void* newControl) {
    EnsureTolkInitialized();

    static int n = 0;
    ++n;

    // Track the currently-focused panel for the chain-navigation handler.
    // Even NULL newControl events update this — what matters is which panel
    // the manager is dispatching focus on.
    void* prevPanel = g_currentPanel;
    g_currentPanel = panel;

    // Tabbed-mode state survives transitions into sub-dialogs of the tabbed
    // panel. The tab strip lives on the PARENT panel (e.g. Options) and is
    // still the right thing for Tab/Shift+Tab to operate on while the user
    // is inside one of its sub-dialogs (Spieleinstellungen, Feedback-Optionen,
    // etc.); clicking a different tab from inside a sub-dialog is the
    // engine's normal "switch tabs" gesture for mouse users. So we only
    // clear the per-event virtual-line cursor on panel change; g_tabbedPanel/
    // g_tabsStart/g_tabsCount persist until DetectTabsCluster overwrites
    // them on a different tabbed panel, or a long-running session re-arms
    // them naturally.
    if (panel != prevPanel) {
        g_virtualLineCount = 0;
        g_virtualLineIdx   = -1;
    }

    // First focus event into a previously-unseen panel: dump every child
    // control on it. Lets us see widgets the user can't reach with arrow
    // keys (mouse-only labels, hidden tabs, off-cursor inputs, etc.).
    //
    // Also captures cycle-button category text. Cycle widgets (Difficulty
    // etc.) carry their localized category in their CExoString at panel
    // construction time (e.g. "Schwierigkeitsgrad" / "Difficulty"); the
    // engine replaces it with the persisted value (e.g. "Normal") shortly
    // after, and our FireActivate calls overwrite it again on each cycle.
    // SetActiveControl's first fire on a new panel happens before any of
    // those updates, so this is the earliest reachable capture point.
    static void* s_lastPanel = nullptr;
    if (panel && panel != s_lastPanel) {
        s_lastPanel = panel;
        // Dump manager-level panels + modal_stack alongside the per-panel
        // walk. Lets us correlate "the engine just walked panel X" with
        // "panels[] and modal_stack[] currently look like this", which is
        // what we need to validate GetForegroundPanel against the actual
        // visible foreground (especially in flows like character creation
        // where multiple panels are walked in the same frame).
        LogManagerStack(*reinterpret_cast<void**>(kAddrGuiManagerPtr),
                        "panel-walk");
        // Identify the panel via the in-game registry. First-sight log
        // happens inside IdentifyPanel. The kind here is purely diagnostic
        // — actual per-kind handling lives in MonitorPanelContents on each
        // OnUpdate tick.
        PanelKind kind = IdentifyPanel(panel);
        acclog::Write("Panel walk panel=%p kind=%s",
                      panel, PanelKindName(kind));
        WalkChildren("Panel", panel, kPanelControlsOffset);

        g_cycleCategoryCount = 0;
        auto* plist = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (plist && plist->data && plist->size > 0) {
            int pn = plist->size > 256 ? 256 : plist->size;
            for (int i = 0;
                 i < pn && g_cycleCategoryCount < kMaxCycleCategoryEntries;
                 ++i) {
                void* c = plist->data[i];
                if (!c) continue;
                if (CallDowncast(c, kVtableAsButton) == nullptr) continue;
                if (IsToggle(c)) continue;

                void* leftN  = FindAdjacentArrow(panel, c, /*toRight=*/false);
                void* rightN = FindAdjacentArrow(panel, c, /*toRight=*/true);
                if (!leftN && !rightN) continue;

                char text[128];
                bool gotText = false;
                uint32_t strref = ReadU32(c, kButtonStrRefOffset);
                if (LookupTlk(strref, text, sizeof(text))) {
                    gotText = true;
                } else if (ReadCExoString(c, kButtonTextOffset,
                                          text, sizeof(text))) {
                    gotText = true;
                }
                if (gotText) {
                    g_cycleCategories[g_cycleCategoryCount].control = c;
                    strncpy_s(g_cycleCategories[g_cycleCategoryCount].category,
                              text, _TRUNCATE);
                    ++g_cycleCategoryCount;
                    acclog::Write("Cycle category captured: control=%p text=\"%s\" strref=%u",
                                  c, text, strref);
                }
            }
        }
    }

    // First focus into a new panel: speak its title (label child) once.
    // The focused control's announcement below still fires, so the user
    // hears "<panel title>, <focused control>" on entry.
    if (panel && panel != g_lastTitledPanel) {
        g_lastTitledPanel = panel;
        AnnouncePanelTitle(panel);
    }

    if (!newControl) {
        acclog::Write("SetActiveControl #%d panel=%p newControl=NULL", n, panel);
        return;
    }

    // Self-dedup: if this SetActiveControl was caused by our deferred
    // MoveMouseToPosition, we already announced the target from the input
    // hook. Skip the Tolk path here and clear the pending marker. This is
    // self-suppression, not engine suppression — the engine wouldn't fire
    // SetActiveControl on consumed arrow keys at all (the wrapper JMPs past
    // dispatch). It only fires here when our own move triggered the engine
    // to reselect.
    if (newControl == g_pendingTarget) {
        acclog::Write("SetActiveControl #%d panel=%p new=%p (self-dedup; cursor sync)",
                      n, panel, newControl);
        g_pendingTarget = nullptr;
        // Cursor-warp echo arrived: our voluntary nav has fully settled.
        // Drain any remaining budget so legit focus changes after this
        // resume normal announcement behavior.
        g_navSpeechSuppressBudget = 0;
        return;
    }

    // Voluntary-nav speech-suppression. The chain-step / Enter-activate
    // handlers set this to a small N; we decrement here on any focus event
    // and skip speech while > 0. Covers engine-side focus echoes that don't
    // match the cursor-warp target (e.g. engine's UP handler picking a
    // sibling on AutoPause / equip panels). See g_navSpeechSuppressBudget
    // doc above the declaration.
    if (g_navSpeechSuppressBudget > 0) {
        int wasBudget = g_navSpeechSuppressBudget;
        --g_navSpeechSuppressBudget;
        int sid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(newControl) + 0x50);
        acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d "
                      "(nav-suppress; budget %d->%d)",
                      n, panel, newControl, sid, wasBudget,
                      g_navSpeechSuppressBudget);
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newControl) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newControl, text, sizeof(text),
                                                 panel);

    if (source) {
        acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d src=%s text=\"%s\"",
                      n, panel, newControl, id, source, text);
        // Container loot panel: the engine's listbox text concatenates every
        // row into one giant utterance ("Tarnfeldgen. Computersonde ..."),
        // which is overwhelming on panel open and drowns the count + per-row
        // navigation announces from MonitorContainerSelection. Log only.
        bool suppressForContainer =
            IsListBox(newControl) &&
            IdentifyPanel(panel) == PanelKind::Container;
        if (!suppressForContainer) {
            SpeakIfChanged(/*channel=*/0, text);
        }
    } else {
        // Always log unknowns — these are the events we need to debug.
        char vtbl[160];
        DumpControlVtable(newControl, vtbl, sizeof(vtbl));
        acclog::Write("SetActiveControl #%d panel=%p new=%p id=%d src=none %s",
                      n, panel, newControl, id, vtbl);
        // Container loot panel: skip the placeholder for the listbox child
        // too — when the chest is empty the listbox has no row text and
        // ExtractAnnounceableText returns null, which would land here and
        // speak "control 2" on top of the panel's "Der Beh\xE4lter ist leer"
        // title that AnnouncePanelTitle / MonitorPanelContents already
        // surfaced. Symmetrical to the suppression in the source-present
        // branch above.
        bool suppressForContainer =
            IsListBox(newControl) &&
            IdentifyPanel(panel) == PanelKind::Container;
        if (!suppressForContainer) {
            // Bypass SpeakIfChanged dedup deliberately: a non-readable focus
            // change deserves *some* announcement every time, even if it's
            // nonsense. Better to hear "control 11" repeated than to silently
            // skip a focus event the user can't otherwise perceive.
            char placeholder[64];
            snprintf(placeholder, sizeof(placeholder), "control %d", id);
            tolk::Speak(placeholder, /*interrupt=*/false);
        }
    }
}

// CSWGuiListBox::SetActiveControl — hooked mid-function at 0x0041c16b.
// Function entry per Lane's SARIF:
//   void __thiscall CSWGuiListBox::SetActiveControl(CSWGuiControl* param_1, int param_2)
//
// Bytes from 0x0041c160 (DumpBytes.java):
//   8b 44 24 08          MOV EAX, [ESP+8]   ; param_2 (int) before push
//   56                   PUSH ESI
//   8b f1                MOV ESI, ECX       ; this → ESI
//   8b 4c 24 08          MOV ECX, [ESP+8]   ; param_1 (post-push, was [ESP+4])
//   50 51 8d 8e 9c 02 00 00     ← hook here, all three args in registers
//   50                   PUSH EAX           ; param_2
//   51                   PUSH ECX           ; param_1
//   8d 8e 9c 02 00 00    LEA  ECX, [ESI+0x29c]  ; embedded sub-object
//
// Cut covers PUSH EAX (1) + PUSH ECX (1) + complete LEA (6) = 8 bytes. All
// three instructions are position-independent → safe to relocate.
//
// Listbox row navigation does NOT bubble up to CSWGuiPanel::SetActiveControl,
// so without this hook we miss every per-row focus event inside listboxes
// (race / class / portrait pickers in chargen, save-game list, etc.).
extern "C" void __cdecl OnListBoxSetActiveControl(void* listBox, void* newRow,
                                                  int param2) {
    EnsureTolkInitialized();

    static int n = 0;
    ++n;

    // First event for a previously-unseen listbox: dump every row control.
    // Tells us whether the listbox holds N separate child widgets (one per
    // visible line) or aggregates everything into a single multi-line label
    // — the central question for the Options Gameplay panel.
    static void* s_lastListBox = nullptr;
    if (listBox && listBox != s_lastListBox) {
        s_lastListBox = listBox;
        WalkChildren("ListBox", listBox, kListBoxControlsOffset);
    }

    // Always log the listbox's internal cursor + flags state. selection_index
    // distinguishes scroll-mode (-1, set when bit_flags & 0x200) from
    // selection-mode (>=0). controls_size tells us how many real rows exist:
    // for the multi-line-blob settings listbox this is 1 even though the
    // user sees 8 visual lines.
    if (listBox) {
        auto* base = reinterpret_cast<unsigned char*>(listBox);
        short itemsPerPage = *reinterpret_cast<short*>(
            base + kListBoxItemsPerPageOffset);
        short selIdx       = *reinterpret_cast<short*>(
            base + kListBoxSelectionIndexOffset);
        short topVisible   = *reinterpret_cast<short*>(
            base + kListBoxTopVisibleIndexOffset);
        uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(
            base + kListBoxBitFlagsOffset);
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            base + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : -1;
        acclog::Write("ListBox::cursor list=%p sel=%d top=%d perPage=%d "
                      "size=%d flags=0x%x",
                      listBox, selIdx, topVisible, itemsPerPage,
                      ctrlsSize, bitFlags);
    }

    if (!newRow) {
        acclog::Write("ListBox::SetActiveControl #%d list=%p newRow=NULL p2=%d",
                      n, listBox, param2);
        return;
    }

    int id = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(newRow) + 0x50);

    char text[256];
    const char* source = ExtractAnnounceableText(newRow, text, sizeof(text));

    if (source) {
        acclog::Write("ListBox::SetActiveControl #%d list=%p row=%p id=%d "
                      "p2=%d src=%s text=\"%s\"",
                      n, listBox, newRow, id, param2, source, text);

        // Lazy tabbed-mode detection: first listbox event after a panel
        // change probes whether the focused panel has the Options-style
        // listbox-at-[0] + button-cluster layout. If so we arm the tab/
        // virtual-line nav path and silence blob speech (the user navigates
        // lines explicitly with arrow keys instead).
        if (g_currentPanel && g_tabbedPanel != g_currentPanel) {
            int tabsStart = -1, tabsCount = 0;
            if (DetectTabsCluster(g_currentPanel, tabsStart, tabsCount)) {
                g_tabbedPanel = g_currentPanel;
                g_tabsStart   = tabsStart;
                g_tabsCount   = tabsCount;
                acclog::Write("Tabbed panel detected: panel=%p tabsStart=%d tabsCount=%d",
                              g_currentPanel, tabsStart, tabsCount);
            }
        }

        if (strchr(text, '\n')) {
            // Multi-line listbox blob (Options-style: all settings concatenated
            // by '\n' into a single CSWGuiLabel row). In tabbed mode we parse
            // the lines into a virtual cursor and speak them one-at-a-time on
            // arrow keys. In non-tabbed mode we silence them too — bulk
            // enumeration is too noisy. If a non-tabbed multi-line listbox
            // ever needs per-line nav, that's a future feature.
            if (g_tabbedPanel == g_currentPanel) {
                ParseVirtualLines(text);
                acclog::Write("ListBox blob silenced (tabbed mode); %d virtual lines parsed",
                              g_virtualLineCount);
            } else {
                int lines = 1;
                for (const char* p = text; *p; ++p) if (*p == '\n') ++lines;
                acclog::Write("ListBox blob silenced (non-tabbed); lines=%d",
                              lines);
            }
        } else {
            SpeakIfChanged(/*channel=*/1, text);
        }
    } else {
        char vtbl[160];
        DumpControlVtable(newRow, vtbl, sizeof(vtbl));
        acclog::Write("ListBox::SetActiveControl #%d list=%p row=%p id=%d "
                      "p2=%d src=none %s",
                      n, listBox, newRow, id, param2, vtbl);
        // Suppress placeholder for single-row listboxes (description blobs
        // adjacent to a chain panel — the engine fires SetActiveControl on
        // them as the user navigates the chain, alternating between
        // src=label with text and src=none when the description is
        // momentarily empty). The user isn't navigating these listboxes;
        // "row 0" repeated 5+ times per chain step is just noise.
        //
        // Real multi-row listboxes (save-game list, chargen pickers) keep
        // the fallback so an extraction failure on one row still announces.
        auto* ctrls = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
        int ctrlsSize = ctrls ? ctrls->size : 0;
        if (ctrlsSize > 1) {
            char placeholder[64];
            snprintf(placeholder, sizeof(placeholder), "row %d", id);
            tolk::Speak(placeholder, /*interrupt=*/false);
        }
    }
}

// CSWGuiControl::HandleFocusChange — hooked mid-function at 0x41896b.
// Demoted to log-only. The panel-level SetActiveControl hook above is the
// real announcement signal; HandleFocusChange fires twice per navigation
// (old loses focus + new gains focus) so speaking from here would echo.
extern "C" void __cdecl OnHandleFocusChange(void* thisPtr, int param_1) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    const char* tip; uint32_t tipLen; int id;
    ReadControlNameFields(thisPtr, tip, tipLen, id);
    acclog::Write("HandleFocusChange #%d this=%p p1=%d id=%d tip[%u]=\"%s\"",
                  n, thisPtr, param_1, id, tipLen,
                  (tip && tipLen > 0) ? tip : "");
}

// CSWGuiManager::HandleInputEvent — hooked mid-function at 0x0040c907.
// This is the GUI manager's central input dispatcher: every key / mouse event
// the engine routes to any GUI surface passes through here before being
// virtual-dispatched to the active panel's per-class override. One hook
// covers every screen (title, Options, chargen, in-game menus, dialog,
// save/load) — replaces the old CSWGuiMainMenu-only hook at 0x67b395.
//
// We hook BEFORE the param_2 == 0 early-out, so we see press AND release
// edges. param_2 is logged as `val=` (0 = release, non-zero = press).
//
// At hook entry: ECX = this, EBX = param_1 (InputIndices key/button code),
// EAX = param_2 (state).
extern "C" int __cdecl OnHandleInputEvent(void* thisPtr, int param_1, int param_2) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;

    // Resolve the foreground panel via the manager's modal_stack / panels[].
    // g_currentPanel tracks "last panel that received SetActiveControl" — fine
    // for per-instance state (sibling-label lookup, cycle-category capture)
    // but UNRELIABLE for routing, because flows that pre-instantiate multiple
    // panels in one frame (character creation: modal + 2 wizards) leave
    // g_currentPanel pointing at the last-walked panel, which is NOT the
    // visible foreground. Verified from patch-20260502-164320.log: in that
    // flow modal_stack.size goes 0→4 with the user-visible Standardcharakter
    // modal correctly at modal[top], while g_currentPanel had latched onto
    // a backgrounded wizard. See ManagerStack diagnostic and report.
    //
    // Fallback to g_currentPanel only when the manager pointer or the
    // foreground resolves to null (early-init frames before any panel
    // exists, or screens we don't yet understand).
    void* activePanel = nullptr;
    {
        void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
        void* fg = GetForegroundPanel(mgr);
        activePanel = fg ? fg : g_currentPanel;
        // First-fire-per-pair divergence log: when fg != g_currentPanel we
        // want to see it in the log, but only once per (fg, g_currentPanel)
        // tuple to avoid spamming during steady-state (every keypress in a
        // multi-panel flow would otherwise emit a line).
        if (fg && fg != g_currentPanel) {
            static void* s_lastFg = nullptr;
            static void* s_lastCp = nullptr;
            if (fg != s_lastFg || g_currentPanel != s_lastCp) {
                acclog::Write("Routing: fg=%p current=%p (using fg)",
                              fg, g_currentPanel);
                s_lastFg = fg;
                s_lastCp = g_currentPanel;
            }
        }

        // Drill override: when the user has Entered into a sub-screen, retarget
        // the chain from the strip (kept in fg by SendPanelToBack) to the
        // sub-screen panel. Only fires when fg actually IS the strip — leaves
        // tutorial modals and Options sub-tabs (which become fg in their own
        // right) routing through fg directly.
        if (g_drilledIntoSubScreen) {
            if (IdentifyPanel(activePanel) == PanelKind::InGameMenu) {
                void* sub = FindActiveSubScreenPanel();
                if (sub) {
                    activePanel = sub;
                } else {
                    g_drilledIntoSubScreen = false;
                    acclog::Write("Drill: sub-screen gone from panels[]; "
                                  "returning to strip");
                }
            }
        }
    }

    // Chain navigation: consume nav-up / nav-down on key-down. We only handle
    // press edges (param_2 != 0) so key-up events still pass through cleanly.
    // Other keys (Tab, Enter, mouse, F-keys) always pass through; activation
    // comes free from the engine via the normal click pipeline once the
    // cursor is over the chain target.
    bool consumed = false;

    // Container loot panel — has its own input semantics that don't fit the
    // chain-navigation model used elsewhere.
    //
    //   * Up/Down — we drive listbox.selection_index ourselves. The engine's
    //     CSWGuiListBox does NOT bind arrow keys for the Container loot list
    //     (verified empirically: arrows reach the manager but selection_index
    //     stays at -1 with no OnListBoxSetActiveControl event). Without this
    //     write, BTN_OK fires with sel == -1 and the engine's onClick takes
    //     EVERY row in the chest (a fallback "take all" semantic).
    //     MonitorContainerSelection picks up the change next tick and speaks
    //     "<row>, i von N".
    //   * Enter — FireActivate BTN_OK (id=3). With selection_index now driven,
    //     the engine takes just the highlighted row.
    //   * Esc — FireActivate BTN_CANCEL (id=5).
    //   * Tab — DOES NOT REACH this hook. The engine's player-control layer
    //     (Change-Leader) consumes Tab before the menu manager dispatches.
    //     The give-mode toggle (BTN_GIVEITEMS) is bound to G via Win32 poll
    //     in PollContainerGiveModeKey() instead.
    //
    // Returns early to bypass the generic chain handlers below.
    if (activePanel != nullptr &&
        IdentifyPanel(activePanel) == PanelKind::Container)
    {
        if (param_2 != 0) {
            // Arrow up/down: drive listbox.selection_index.
            if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
                void* lb = FindListBoxChild(activePanel);
                if (lb) {
                    auto* lbBase = reinterpret_cast<unsigned char*>(lb);
                    auto* lbList = reinterpret_cast<CExoArrayList*>(
                        lbBase + kListBoxControlsOffset);
                    int rowCount = (lbList && lbList->data) ? lbList->size : 0;
                    if (rowCount > 0) {
                        short* selPtr = reinterpret_cast<short*>(
                            lbBase + kListBoxSelectionIndexOffset);
                        short* topPtr = reinterpret_cast<short*>(
                            lbBase + kListBoxTopVisibleIndexOffset);
                        short* ippPtr = reinterpret_cast<short*>(
                            lbBase + kListBoxItemsPerPageOffset);
                        short oldSel = *selPtr;
                        short newSel;
                        if (oldSel < 0) {
                            // From "no selection" land on first row regardless
                            // of direction (closer to user expectation than
                            // wrapping or staying silent).
                            newSel = 0;
                        } else if (param_1 == kInputNavDown) {
                            newSel = (short)(oldSel + 1);
                            if (newSel >= rowCount) newSel = (short)(rowCount - 1);
                        } else {
                            newSel = (short)(oldSel - 1);
                            if (newSel < 0) newSel = 0;
                        }
                        if (newSel != oldSel) {
                            *selPtr = newSel;
                            // Keep the focused row in the visible window.
                            short ipp = *ippPtr;
                            short top = *topPtr;
                            if (ipp <= 0) ipp = 1;
                            if (newSel < top) {
                                *topPtr = newSel;
                            } else if (newSel >= top + ipp) {
                                *topPtr = (short)(newSel - ipp + 1);
                            }
                        }
                        acclog::Write("Container: %s lb=%p sel=%d->%d (rows=%d)",
                                      param_1 == kInputNavDown ? "Down" : "Up",
                                      lb, oldSel, newSel, rowCount);
                    } else {
                        acclog::Write("Container: %s lb=%p empty; nav ignored",
                                      param_1 == kInputNavDown ? "Down" : "Up", lb);
                    }
                }
                consumed = true;  // never let the engine see arrow keys here
            } else {
                int targetId = -1;
                const char* what = nullptr;
                void* preselected = nullptr;  // takes precedence over targetId

                // Container per-item take is currently UNRESOLVED. Both
                // tested primitives fail:
                //   * FireActivate(row_ptr) → engine's vtable[15] runs but
                //     doesn't translate to "take this row" — rowCount stays
                //     unchanged. Listbox protoitems don't carry the take
                //     logic at vtable[15].
                //   * Click-sim at row.GetControlCenter() coords → cursor
                //     hits dead space (Down=0, Up=0). Row extents are
                //     listbox-local, not screen-absolute; we'd need parent
                //     offset accumulation we don't currently do.
                //
                // Until we identify the engine's row-take primitive (likely
                // embedded in CSWGuiContainer::HandleInputEvent at 0x006b92f0
                // or the protoitem's onClick), Enter dispatches BTN_OK
                // unconditionally. That's the working "take-all" gesture.
                // Per-item take = lost feature, deferred. See
                // docs/equip-flow-investigation.md for the parallel
                // investigation that landed the same shape on equip.
                if (param_1 == kInputEnter1 || param_1 == kInputEnter2) {
                    targetId = kContainerBtnOkId;
                    what = "Enter -> BTN_OK (take-all; per-item take deferred)";
                } else if (param_1 == kInputEsc1 || param_1 == kInputEsc2) {
                    targetId = kContainerBtnCancelId; what = "Esc -> BTN_CANCEL";
                }

                if (what) {
                    if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
                        acclog::Write("Container: %s -- op already pending; ignoring", what);
                        consumed = true;
                    } else {
                        void* tgt = preselected;
                        if (!tgt && targetId >= 0) tgt = FindControlById(activePanel, targetId);
                        if (tgt) {
                            g_pendingActivate       = true;
                            g_pendingActivateTarget = tgt;
                            acclog::Write("Container: %s panel=%p target=%p",
                                          what, activePanel, tgt);
                            consumed = true;
                        } else {
                            acclog::Write("Container: %s -- target not resolved on panel=%p "
                                          "(targetId=%d)", what, activePanel, targetId);
                        }
                    }
                }
                // NavLeft/NavRight + everything else: pass through unchanged.
            }
        }

        // Log + return to skip the generic chain handlers below.
        int translated = acc::engine::ManagerTranslateCode(param_1);
        const char* tag = consumed ? " CONSUMED" : "";
        if (translated != param_1) {
            acclog::Write("HandleInputEvent #%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                          n, thisPtr, param_1,
                          acc::engine::InputIndexName(translated), translated, param_2, tag);
        } else {
            acclog::Write("HandleInputEvent #%d this=%p key=%s(%d) val=%d%s",
                          n, thisPtr, acc::engine::InputIndexName(param_1), param_1, param_2, tag);
        }
        return consumed ? 1 : 0;
    }

    // Equipment screen — modal item-picker zone. Two zones in one panel:
    //
    //   * Slot zone (default): chain navigates the 9 BTN_INV_* slot grid +
    //     BTN_BACK + BTN_EQUIP. Arrow keys + Enter handled by the generic
    //     handlers below. When Enter activates a BTN_INV_* slot the engine
    //     repopulates LB_ITEMS for that slot — we then arm the picker so
    //     subsequent arrows go to the items list, not the slot grid.
    //
    //   * Picker zone (g_equipPickerActive): Up/Down drive
    //     LB_ITEMS.selection_index directly (the engine doesn't bind arrow
    //     keys to that listbox — same situation as the Container loot list).
    //     Enter dispatches BTN_EQUIP, then disarms. Esc disarms without
    //     equipping; chain focus is unchanged so the next arrow press
    //     resumes slot navigation.
    //
    // Arming + disarming runs unconditionally regardless of zone so a panel
    // close / reopen always resets — see g_equipPickerPanel staleness check.
    if (activePanel != nullptr &&
        IdentifyPanel(activePanel) == PanelKind::InGameEquip)
    {
        // Self-disarm if the panel pointer drifted (re-open, panel kind
        // matches but address differs). Picker state is per-panel.
        if (g_equipPickerActive && g_equipPickerPanel != activePanel) {
            acclog::Write("EquipPicker: disarm — panel changed (%p -> %p)",
                          g_equipPickerPanel, activePanel);
            g_equipPickerActive = false;
            g_equipPickerPanel  = nullptr;
        }

        if (g_equipPickerActive && param_2 != 0) {
            void* lb = FindControlById(activePanel, kEquipLbItemsId);
            if (lb && (param_1 == kInputNavUp || param_1 == kInputNavDown)) {
                auto* lbBase = reinterpret_cast<unsigned char*>(lb);
                auto* lbList = reinterpret_cast<CExoArrayList*>(
                    lbBase + kListBoxControlsOffset);
                int rowCount = (lbList && lbList->data) ? lbList->size : 0;
                // Equip listbox row 0 is the PROTOITEM template (text "•",
                // verified empirically — see investigation 2026-05-04). Real
                // items live at [1..rowCount-1]. Clamp navigation to that
                // range so arrow keys never land on the template.
                int realRows = rowCount - 1;  // count of selectable items
                if (realRows > 0) {
                    short* selPtr = reinterpret_cast<short*>(
                        lbBase + kListBoxSelectionIndexOffset);
                    short* topPtr = reinterpret_cast<short*>(
                        lbBase + kListBoxTopVisibleIndexOffset);
                    short* ippPtr = reinterpret_cast<short*>(
                        lbBase + kListBoxItemsPerPageOffset);
                    short oldSel = *selPtr;
                    short newSel;
                    if (oldSel < 1) {
                        // -1 (no selection) or 0 (template) → land on first
                        // real item.
                        newSel = 1;
                    } else if (param_1 == kInputNavDown) {
                        newSel = (short)(oldSel + 1);
                        if (newSel >= rowCount) newSel = (short)(rowCount - 1);
                    } else {
                        newSel = (short)(oldSel - 1);
                        if (newSel < 1) newSel = 1;  // clamp to first real row
                    }
                    if (newSel != oldSel) {
                        *selPtr = newSel;
                        short ipp = *ippPtr;
                        short top = *topPtr;
                        if (ipp <= 0) ipp = 1;
                        if (newSel < top) {
                            *topPtr = newSel;
                        } else if (newSel >= top + ipp) {
                            *topPtr = (short)(newSel - ipp + 1);
                        }
                    }
                    acclog::Write("EquipPicker: %s lb=%p sel=%d->%d (rows=%d, real=%d)",
                                  param_1 == kInputNavDown ? "Down" : "Up",
                                  lb, oldSel, newSel, rowCount, realRows);
                } else {
                    acclog::Write("EquipPicker: %s lb=%p empty; nav ignored "
                                  "(rows=%d, real=0)",
                                  param_1 == kInputNavDown ? "Down" : "Up",
                                  lb, rowCount);
                }
                consumed = true;
            } else if (param_1 == kInputEnter1 || param_1 == kInputEnter2) {
                if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
                    acclog::Write("EquipPicker: Enter — op already pending; ignoring");
                    consumed = true;
                } else {
                    // Drive the engine's own equip path by replaying the
                    // mouse sequence sighted players use:
                    //   1. Click on the selected listbox row → engine's
                    //      OnItemSelected (@0x006b7920) raises BTN_EQUIP's
                    //      is_active bit (+0x4c) AND the panel's "ready
                    //      to equip" flag (this->[+0x4270] |= 1).
                    //   2. FireActivate(BTN_EQUIP) → OnOKPressed
                    //      (@0x006b9160) — both gates now satisfied,
                    //      equip commits via the engine's normal path
                    //      (slot-fit / prerequisite checks intact;
                    //      ShowCantEquipMessage on rejection is the
                    //      engine's own).
                    // Update() processes pendingCursorMove → pendingClick
                    // → pendingActivate sequentially in a single tick, so
                    // by the time FireActivate runs OnItemSelected has
                    // already raised the gates synchronously.
                    //
                    // Earlier failed primitives (kept here as cautionary
                    // notes — see docs/equip-flow-investigation.md):
                    //   * FireActivate(BTN_EQUIP) cold — gate not raised,
                    //     vtable[15] silently no-ops.
                    //   * Click-sim at BTN_EQUIP center (320,424) — hits
                    //     LBL_TOHIT instead.
                    //   * Click-sim at row.extent alone — listbox-local
                    //     coords land on dead space. Resolved here by
                    //     GetListBoxRowScreenCenter accumulating the
                    //     listbox's screen-absolute origin.
                    void* lb  = FindControlById(activePanel, kEquipLbItemsId);
                    void* btn = FindControlById(activePanel, kEquipBtnEquipId);
                    void* row = nullptr;
                    short selIdx = -1;
                    int   rowCount = 0;
                    if (lb) {
                        auto* lbBase = reinterpret_cast<unsigned char*>(lb);
                        auto* lbList = reinterpret_cast<CExoArrayList*>(
                            lbBase + kListBoxControlsOffset);
                        rowCount = (lbList && lbList->data) ? lbList->size : 0;
                        selIdx = *reinterpret_cast<short*>(
                            lbBase + kListBoxSelectionIndexOffset);
                        if (lbList && lbList->data &&
                            selIdx >= 1 && selIdx < rowCount) {
                            row = lbList->data[selIdx];
                        }
                    }
                    int rowCx = 0, rowCy = 0;
                    if (lb && row && btn &&
                        GetListBoxRowScreenCenter(lb, row, rowCx, rowCy))
                    {
                        g_pendingX                = rowCx;
                        g_pendingY                = rowCy;
                        g_pendingTarget           = row;
                        g_pendingClickTarget      = row;
                        g_pendingCursorMove       = true;
                        g_pendingClick            = true;
                        g_pendingActivate         = true;
                        g_pendingActivateTarget   = btn;
                        g_navSpeechSuppressBudget = 2;
                        acclog::Write("EquipPicker: Enter -> click(row sel=%d %p "
                                      "at %d,%d) + activate(BTN_EQUIP %p) panel=%p",
                                      selIdx, row, rowCx, rowCy, btn, activePanel);
                    } else {
                        acclog::Write("EquipPicker: Enter -- can't equip "
                                      "(lb=%p row=%p btn=%p sel=%d rows=%d) panel=%p",
                                      lb, row, btn, selIdx, rowCount, activePanel);
                    }
                    g_equipPickerActive = false;
                    g_equipPickerPanel  = nullptr;
                    consumed = true;
                }
            } else if (param_1 == kInputEsc1 || param_1 == kInputEsc2) {
                acclog::Write("EquipPicker: Esc -> disarm (panel=%p)", activePanel);
                g_equipPickerActive = false;
                g_equipPickerPanel  = nullptr;
                consumed = true;
            }
        }

        // If picker handled the event, log + return early (mirrors Container).
        if (consumed) {
            int translated = acc::engine::ManagerTranslateCode(param_1);
            const char* tag = " CONSUMED";
            if (translated != param_1) {
                acclog::Write("HandleInputEvent #%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                              n, thisPtr, param_1,
                              acc::engine::InputIndexName(translated), translated, param_2, tag);
            } else {
                acclog::Write("HandleInputEvent #%d this=%p key=%s(%d) val=%d%s",
                              n, thisPtr, acc::engine::InputIndexName(param_1), param_1, param_2, tag);
            }
            return 1;
        }
        // Not consumed — fall through to generic handlers (slot zone).
        // Watch for a slot-button Enter to arm the picker; done after the
        // generic Enter activate block writes g_pendingActivateTarget so we
        // can see what got selected.
    }

    // Pillar 4 cycle keys (`,` `.` `Shift+,` `Shift+.` `-` `Shift+-`) — Phase 2
    // lay-off 3. Routed first because cycle is in-game-only and the handler
    // self-gates on GetPlayerPosition; in menus / chargen / dialog it returns
    // false and the key falls through to the normal menu logic below.
    if (acc::cycle_input::TryHandleEvent(param_1, param_2)) {
        consumed = true;
    }

    // Enter-press activation. Two activation primitives, picked per-target:
    //
    //   * **Tab buttons** in the tabbed parent panel (Options:
    //     Gameplay/Auto-Pause/Grafik/Sound/Feedback) require the full
    //     click pipeline because their handler (CSWGuiInGameOptions::OnGraphics
    //     @0x006aad90, etc.) gates on `param_1->is_active != 0`. That flag is
    //     set by HandleLMouseDown but NOT by direct vtable[15] dispatch — so
    //     FireActivate on a tab silently no-ops. Verified via Ghidra
    //     decompilation. Click-sim (cursor warp + Down + Up) is the only way
    //     in.
    //
    //   * **Everything else** (sub-dialog setting buttons, OK/Cancel popups,
    //     main menu) uses direct vtable[15] activate. It bypasses hit-test, so
    //     buttons covered by overlapping listbox extents (chain targets in
    //     sub-dialogs) still fire — click-sim there resolves to the listbox
    //     instead of the button (Up=0, no dispatch).
    //
    // Debounce: refuse to queue another op if one is already pending.
    // Stops Enter typematic from queuing back-to-back activations on adjacent
    // OnUpdate frames — the only re-entry path left after deleting the
    // Tab-cycle two-step. Single user-paced presses always go through.
    //
    // Consume Enter either way so the engine doesn't ALSO fire F1 against
    // panel.activeControl (which can be stale or wrong).
    if (param_2 != 0 &&
        (param_1 == kInputEnter1 || param_1 == kInputEnter2) &&
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
        g_chainCount > 0 &&
        g_chainIndex < g_chainCount)
    {
        ChainEntry& e = g_chain[g_chainIndex];

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
        // pipeline (cursor warp + LMouseDown/Up) to fire the engine's
        // OnSelectSlot — which is what populates LB_ITEMS with items matching
        // the slot. Direct vtable[15] activate on a slot button routes to a
        // different handler (likely OnEnterSlot, the keyboard shortcut path)
        // that pops a "no items" modal instead of populating the picker. Same
        // gate-mismatch shape as Options tab buttons: the mouse path is the
        // only one that triggers the populate.
        bool isEquipSlot = false;
        int equipSlotCid = 0;
        if (IdentifyPanel(g_chainPanel) == PanelKind::InGameEquip) {
            equipSlotCid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(e.control) + 0x50);
            isEquipSlot =
                equipSlotCid == kEquipBtnHeadId    || equipSlotCid == kEquipBtnImplantId ||
                equipSlotCid == kEquipBtnBodyId    || equipSlotCid == kEquipBtnArmLId    ||
                equipSlotCid == kEquipBtnArmRId    || equipSlotCid == kEquipBtnWeapLId   ||
                equipSlotCid == kEquipBtnWeapRId   || equipSlotCid == kEquipBtnBeltId    ||
                equipSlotCid == kEquipBtnHandsId;
        }

        if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
            acclog::Write("Enter: op already pending; ignoring (target=%p)", e.control);
            consumed = true;
        } else if (isTabButton) {
            int cursorY = e.cy;
            if (g_tabClickOffsetY > 0) cursorY += g_tabClickOffsetY;
            g_pendingX           = e.cx;
            g_pendingY           = cursorY;
            g_pendingTarget      = e.control;
            g_pendingClickTarget = e.control;
            g_pendingCursorMove  = true;
            g_pendingClick       = true;
            g_navSpeechSuppressBudget = 2;  // see chain-step doc above
            acclog::Write("Enter click-sim panel=%p index=%d target=%p cursorY=%d (tab)",
                          activePanel, g_chainIndex, e.control, cursorY);
            consumed = true;
        } else if (isEquipSlot) {
            int cursorY = e.cy;
            if (g_equipSlotClickOffsetY > 0) cursorY += g_equipSlotClickOffsetY;
            g_pendingX           = e.cx;
            g_pendingY           = cursorY;
            g_pendingTarget      = e.control;
            g_pendingClickTarget = e.control;
            g_pendingCursorMove  = true;
            g_pendingClick       = true;
            g_navSpeechSuppressBudget = 2;
            // Arm the picker zone now: once OnSelectSlot fires (synchronously
            // inside HandleLMouseUp during Update()), LB_ITEMS will be
            // populated by the next event the user generates. Self-clears on
            // panel close, picker Esc, or BTN_EQUIP dispatch.
            g_equipPickerActive = true;
            g_equipPickerPanel  = g_chainPanel;
            acclog::Write("EquipPicker: armed via click-sim (Enter on slot id=%d "
                          "target=%p panel=%p at %d,%d cursorY=%d offset=%d)",
                          equipSlotCid, e.control, g_chainPanel,
                          e.cx, e.cy, cursorY, g_equipSlotClickOffsetY);
            consumed = true;
        } else {
            g_pendingActivate       = true;
            g_pendingActivateTarget = e.control;
            g_navSpeechSuppressBudget = 2;  // see chain-step doc above

            // Arm the drill flag when Enter activates an icon on the InGameMenu
            // strip. The engine's activation path (FireActivate → button
            // onClick → SwitchToSWInGameGui) pushes the sub-screen to back; on
            // the next arrow press, the drill router will retarget the chain
            // to it. Set EAGERLY: if the engine no-ops the activation (e.g.
            // re-press of the same icon, GUI_id unchanged), the override
            // self-clears via FindActiveSubScreenPanel-returns-null on the
            // next route.
            if (IdentifyPanel(g_chainPanel) == PanelKind::InGameMenu) {
                g_drilledIntoSubScreen = true;
                acclog::Write("Drill: armed (Enter on InGameMenu icon target=%p)",
                              e.control);
            }

            acclog::Write("Enter activate panel=%p index=%d target=%p",
                          activePanel, g_chainIndex, e.control);
            consumed = true;
        }
    }

    // Tab key falls through to the engine. We previously implemented a
    // Tab-cycle two-step here (FireActivate(Schliess) → schedule click-sim on
    // next tab), but it crashed: compressing close-old-subdialog + open-new
    // into a single ~16ms window pressured the GL driver (Grafik specifically
    // re-runs gamma-ramp via OnMoveGammaSlider in its constructor) until the
    // NVIDIA driver fast-failed mid-frame. See docs/tab-crash-investigation.md.
    //
    // Replacement UX: arrow keys walk tab buttons via the chain (they're
    // already in panel.controls of the tabbed parent), Enter opens a tab via
    // click-sim (above), Esc closes a sub-dialog via FireActivate(Schliess)
    // (handler below) — keeping every action user-paced and never collapsing
    // close+open into the same frame.

    // Arrow keys: flat chain navigation. Chain is built from panel.controls
    // + listbox children (one level) sorted by extent.top, so arrow-down
    // walks visually top-to-bottom through every navigable button — including
    // tab buttons on the parent Options panel and settings that live as
    // button children of a CSWGuiListBox in sub-dialogs.
    if (param_2 != 0 &&
        (param_1 == kInputNavUp || param_1 == kInputNavDown) &&
        activePanel != nullptr)
    {
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
            PanelKind emptyKind = IdentifyPanel(activePanel);
            acclog::Write("Chain empty: panel=%p kind=%s has no navigable "
                          "controls; input not consumed",
                          activePanel, PanelKindName(emptyKind));

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
                acclog::Write("EmptyChainPanel walk panel=%p kind=%s",
                              activePanel, PanelKindName(emptyKind));
                WalkChildren("EmptyChainPanel", activePanel,
                             kPanelControlsOffset);
            }
        }
        if (g_chainCount > 0) {
            int delta = (param_1 == kInputNavDown) ? +1 : -1;
            int newIndex = g_chainIndex + delta;
            if (newIndex < 0)              newIndex = 0;
            if (newIndex >= g_chainCount)  newIndex = g_chainCount - 1;
            g_chainIndex = newIndex;

            ChainEntry& e = g_chain[g_chainIndex];
            AnnounceControl(e.control);
            int cursorY = e.cy;
            if (IsTabButton(e.control) && g_tabClickOffsetY > 0) {
                cursorY += g_tabClickOffsetY;
            }
            g_pendingX          = e.cx;
            g_pendingY          = cursorY;
            g_pendingTarget     = e.control;
            g_pendingCursorMove = true;
            // Suppress the next two SetActiveControl announces — engine-side
            // focus echoes that fire after the keypress + cursor warp would
            // otherwise read out the wrong sibling control as an "afterthought"
            // after we already announced the chain target above.
            g_navSpeechSuppressBudget = 2;
            acclog::Write("Chain step panel=%p index=%d/%d target=%p center=(%d,%d) cursorY=%d %s",
                          g_chainPanel, g_chainIndex, g_chainCount,
                          e.control, e.cx, e.cy, cursorY,
                          param_1 == kInputNavDown ? "DOWN" : "UP");
            // Always consume nav-up/nav-down on a panel with a non-empty chain.
            consumed = true;
        }
    }

    // Left/Right dispatch. Two cases:
    //
    //   1. Focused control is a slider — queue a slider HandleInputEvent
    //      with logical inc/dec code (500 / 501). Engine's slider runs the
    //      full pipeline: SetCurValue + bounds clamp + gui_object callback
    //      (audio volume change for Music/Voice/SFX/Movie) + PlayGuiSound.
    //      Letting the keypress pass through to the engine doesn't work
    //      because panel.activeControl isn't set to the slider (chain
    //      navigation only updates mouseOverControl); the engine's natural
    //      dispatch would route Left/Right to whichever previous control was
    //      activeControl, not the slider the user navigated to.
    //
    //   2. Focused control is anything else — find an empty-text navigable
    //      neighbour at the same y-row in panel.controls and fire-activate
    //      it (cycle-arrow flanker). Engine rewrites the value-display
    //      button's CExoString in place. Per-frame monitor catches both
    //      cases on the next tick and re-announces.
    //
    // Both cases consume the keypress so we don't surface unspecified
    // native behaviour from Left/Right on widgets where it has no
    // user-meaningful effect.
    if (param_2 != 0 &&
        (param_1 == kInputNavLeft || param_1 == kInputNavRight) &&
        activePanel != nullptr &&
        g_chainPanel == activePanel &&
        g_chainCount > 0 &&
        g_chainIndex >= 0 &&
        g_chainIndex < g_chainCount)
    {
        void* focused = g_chain[g_chainIndex].control;
        bool toRight = (param_1 == kInputNavRight);

        if (IsSlider(focused)) {
            if (g_pendingClick || g_pendingActivate || g_pendingCursorMove ||
                g_pendingSliderInput) {
                acclog::Write("Slider %s: op already pending; ignoring",
                              toRight ? "right" : "left");
            } else {
                g_pendingSliderInput  = true;
                g_pendingSliderTarget = focused;
                g_pendingSliderCode   = toRight ? 500 : 501;
                acclog::Write("Slider %s panel=%p focus=%p code=%d",
                              toRight ? "right" : "left",
                              activePanel, focused, g_pendingSliderCode);
            }
        } else {
            void* neighbor = FindAdjacentArrow(activePanel, focused, toRight);
            if (neighbor) {
                if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
                    acclog::Write("Cycle %s: op already pending; ignoring",
                                  toRight ? "right" : "left");
                } else {
                    g_pendingActivate       = true;
                    g_pendingActivateTarget = neighbor;
                    acclog::Write("Cycle %s panel=%p focus=%p neighbor=%p",
                                  toRight ? "right" : "left",
                                  activePanel, focused, neighbor);
                }
            } else {
                acclog::Write("Cycle %s: no adjacent arrow for focus=%p",
                              toRight ? "right" : "left", focused);
            }
        }
        consumed = true;
    }

    // Esc in drill mode: return chain to the strip without closing the
    // sub-screen. User flow:
    //   1. On strip, Enter on Inventory → engine pushes InGameInventory,
    //      strip stays fg, drill arms, chain retargets to inventory listbox.
    //   2. User navigates inventory items.
    //   3. Esc → drill clears, next arrow press rebinds chain to strip.
    //   4. From strip, Right-arrow + Enter to switch to a different sub-screen.
    //
    // We deliberately don't fire the sub-screen's exit_button here — leaving
    // the sub-screen alive in panels[] means re-pressing Enter on the same
    // icon is a cheap re-drill (no tutorial replay, no engine-side teardown).
    // Closing the screen is what the sub-screen's own exit_button is for; the
    // user can navigate to it explicitly while drilled.
    //
    // Routes BEFORE the tabbed-panel Esc handler below: drilled mode is the
    // outer state, sub-tab close is the inner. If both could match (drilled
    // into Options with a sub-tab open), close the sub-tab first via the
    // existing handler — drill stays armed because activePanel is still
    // a sub-tab modal at that point, not the strip.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        g_drilledIntoSubScreen &&
        activePanel != nullptr &&
        IdentifyPanel(activePanel) != PanelKind::InGameMenu)
    {
        // Only fire when activePanel is the sub-screen itself, not a sub-tab
        // or modal sitting on top of it. Tabbed-Esc handler below will close
        // sub-tabs first; once activePanel resolves back to the sub-screen,
        // the next Esc lands here and clears the drill.
        PanelKind apk = IdentifyPanel(activePanel);
        if (FindInGameSubScreenSpec(apk)) {
            g_drilledIntoSubScreen = false;
            acclog::Write("Drill: Esc -> back to strip (sub-screen panel=%p "
                          "kind=%s left in panels[])",
                          activePanel, PanelKindName(apk));
            consumed = true;
        }
    }

    // Esc / Backspace (when bound to "back/cancel" via the in-game Key Mapping
    // screen): close the current sub-dialog by FireActivate-ing its Schliess
    // button. The engine's natural Esc → CSWGuiOptionsXxx::HandleInputEvent(0x28)
    // → PopModalPanel path is silently failing in our environment (Esc reaches
    // the manager and translates correctly, but no close fires — verified in
    // patch-20260502-102803.log lines 311-312). FireActivate(Schliess) is the
    // same primitive that already works when the user manually navigates to
    // Schliess and presses Enter, so routing Esc through it gives deterministic
    // close behavior.
    //
    // Gated on activePanel != g_tabbedPanel: only fires inside a sub-dialog
    // of a tabbed parent. On the parent Options panel itself, Esc passes
    // through to the engine (which opens the "Möchtest du wirklich aufhören?"
    // quit confirmation — desired existing behavior).
    //
    // We use activePanel (resolved from the manager's modal_stack/panels[]
    // at the top of this function) rather than g_currentPanel. The latter is
    // set by SetActiveControl and never cleared on panel pop, so once a
    // sub-dialog closes, g_currentPanel keeps pointing at the dead panel
    // until a new one takes focus — and Esc would keep firing FireActivate
    // against the popped panel. activePanel always reflects the current
    // foreground per the manager.
    if (param_2 != 0 &&
        (param_1 == kInputEsc1 || param_1 == kInputEsc2) &&
        activePanel != nullptr &&
        g_tabbedPanel != nullptr &&
        activePanel != g_tabbedPanel)
    {
        if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
            acclog::Write("Esc: op already pending; ignoring");
            consumed = true;
        } else {
            void* closeBtn = FindCloseButton(activePanel);
            if (closeBtn) {
                g_pendingActivate       = true;
                g_pendingActivateTarget = closeBtn;
                acclog::Write("Esc close panel=%p kind=%s Schliess=%p",
                              activePanel, PanelKindName(IdentifyPanel(activePanel)),
                              closeBtn);
                consumed = true;
            } else {
                acclog::Write("Esc on sub-dialog panel=%p kind=%s but no "
                              "close button found; passing through",
                              activePanel, PanelKindName(IdentifyPanel(activePanel)));
            }
        }
    }

    int translated = acc::engine::ManagerTranslateCode(param_1);
    const char* tag = consumed ? " CONSUMED" : "";
    if (translated != param_1) {
        acclog::Write("HandleInputEvent #%d this=%p key=logical(%d) -> %s(%d) val=%d%s",
                      n, thisPtr, param_1,
                      acc::engine::InputIndexName(translated), translated, param_2, tag);
    } else {
        acclog::Write("HandleInputEvent #%d this=%p key=%s(%d) val=%d%s",
                      n, thisPtr, acc::engine::InputIndexName(param_1), param_1, param_2, tag);
    }
    return consumed ? 1 : 0;
}

// Per-frame focus state monitor. Re-extracts the focused chain entry's
// announceable text and re-announces if it has changed since the last
// snapshot. Generic mechanism for state-change announcements — toggle
// on/off, cycle button value, slider position, and any future widget whose
// state shows up through ExtractAnnounceableText all flow through here, no
// per-widget code needed.
//
// On focus moving to a different control we only update the snapshot. The
// initial announcement is handled by the chain step path (OnHandleInputEvent)
// or OnSetActiveControl; this monitor's job is strictly "same control, text
// changed since last tick". That's precisely the "state mutated under our
// focus" case — Enter on a toggle flips +0x1c8 bit 0 synchronously inside
// FireActivate, the engine's slider HandleInputEvent rewrites cur_value
// synchronously when Left/Right reach the slider, and a cycle activation
// rewrites the value-display button's CExoString in place. All three
// produce a different ExtractAnnounceableText output on the very next tick.
//
// Empty-text controls (cycle arrows, controls we don't yet know how to
// extract) bypass the snapshot entirely so we don't accidentally announce
// transient placeholders. The chain step path already announced "control N"
// for them when focus arrived.
static void MonitorFocusedControl() {
    if (g_chainCount <= 0 ||
        g_chainIndex < 0 ||
        g_chainIndex >= g_chainCount) {
        return;
    }
    if (g_chainPanel != g_currentPanel) {
        // Chain stale (panel transition mid-flight); skip until rebind.
        return;
    }
    void* focused = g_chain[g_chainIndex].control;
    if (!focused) return;

    char text[256];
    const char* source = ExtractAnnounceableText(focused, text, sizeof(text),
                                                 g_chainPanel);
    if (!source) return;

    if (focused == g_focusMonitorControl) {
        if (strncmp(g_focusMonitorText, text,
                    sizeof(g_focusMonitorText)) != 0) {
            tolk::Speak(text, /*interrupt=*/false);
            strncpy_s(g_focusMonitorText, text, _TRUNCATE);
            acclog::Write("Monitor: focused=%p text changed -> \"%s\"",
                          focused, text);
        }
    } else {
        g_focusMonitorControl = focused;
        strncpy_s(g_focusMonitorText, text, _TRUNCATE);
    }
}

// =============================================================================
// Per-panel content-change monitor.
//
// MonitorFocusedControl above watches the focused chain entry's text for
// state-mutation announcements (toggle flip, cycle value, slider position).
// That's the right thing for INTERACTIVE focus targets, but blind to two
// classes of events:
//
//   1. Panels that have no focused control (newControl=NULL throughout).
//      The tutorial popup is the canonical case — panel 12B04010 has a
//      label child carrying the hint text but no focusable child; the
//      engine never fires SetActiveControl with a non-null target. The
//      label text is also late-bound: the panel appears with " " in the
//      label, then the engine writes the actual hint string seconds later.
//      Our pointer-keyed gates in OnSetActiveControl (s_lastPanel,
//      g_lastTitledPanel) only fire once per panel address, so we miss
//      the late text binding entirely.
//
//   2. Always-on panels at the bottom of panels[] (the HUD, persistent
//      overlays). These never receive SetActiveControl, so they're never
//      walked, never titled, never monitored.
//
// MonitorPanelContents fills both gaps generically: every OnUpdate tick,
// walk the manager's panels[], identify each by IdentifyPanel, and for
// the ones flagged as content-monitored compute a fingerprint of their
// label-bearing children (concatenation of every label and listbox text).
// Diff against last snapshot, announce changes.
//
// Per-kind whitelist (IsContentMonitored) keeps the cost down — we don't
// fingerprint every panel every frame, only the ones whose content actually
// changes meaningfully (tutorials, dialogue text, transition text, modal
// messages). MainInterface (HUD vitals, queue, combat-mode) deserves a
// dedicated polling layer with named-offset reads instead of full-panel
// fingerprinting; deferred to a follow-up.
// =============================================================================

struct ContentSnapshot {
    void* panel;
    char  text[512];
};
constexpr int kMaxContentSnapshots = 8;
static ContentSnapshot g_contentSnapshots[kMaxContentSnapshots];
static int g_contentSnapshotCount = 0;

static bool IsContentMonitored(PanelKind k) {
    switch (k) {
    case PanelKind::TutorialBox:
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
    case PanelKind::BarkBubble:
    case PanelKind::MessageBoxModal:
    case PanelKind::AreaTransition:
    // In-game sub-screens reached via the icon strip. The icon strip
    // (CSWGuiInGameMenu) stays foreground after activation, so the sub-screen
    // never becomes the chain target — without content monitoring the user
    // hears the strip but nothing about what's INSIDE the screen they just
    // opened. Buttons are filtered by BuildContentFingerprint so the strip's
    // own buttons don't pollute the fingerprint.
    case PanelKind::InGameInventory:
    case PanelKind::InGameMap:
    case PanelKind::InGameJournal:
    case PanelKind::InGameCharacter:
    case PanelKind::InGameAbilities:
    case PanelKind::InGameMessages:
    case PanelKind::InGameEquip:
    // Container loot panel — Tab toggles between take-mode and give-mode,
    // which swaps LBL_MESSAGE's strref + LB_ITEMS source. Without content
    // monitoring the user only hears the per-row navigation (via Monitor-
    // ContainerSelection) and never learns the mode flipped.
    case PanelKind::Container:
        return true;
    default:
        return false;
    }
}

// Localized name of an in-game sub-screen, indexed by PanelKind. Reuses
// the same dialog.tlk strrefs as the perkind icon-label table in
// ExtractAnnounceableText (verified by parsing the user's dialog.tlk —
// memory/reference_dialog_tlk_menu_strrefs.md). Returns spec on hit, nullptr
// if the kind isn't a tracked sub-screen.
struct InGameSubScreenSpec {
    PanelKind   kind;
    uint32_t    strref;     // 0xFFFFFFFF = no strref, use literal
    const char* literal;
};
static const InGameSubScreenSpec k_inGameSubScreens[] = {
    { PanelKind::InGameEquip,     0xFFFFFFFFu, "Ausr\xfcstung" },
    { PanelKind::InGameInventory, 48220u,      "Inventar" },
    { PanelKind::InGameCharacter, 48225u,      "Charakterblatt" },
    { PanelKind::InGameMap,       48221u,      "Karte" },
    { PanelKind::InGameAbilities, 48224u,      "F\xe4higkeiten" },
    { PanelKind::InGameJournal,   48218u,      "Auftr\xe4ge" },
    { PanelKind::InGameOptions,   48222u,      "Optionen" },
    { PanelKind::InGameMessages,  48223u,      "Nachrichten" },
};
static const InGameSubScreenSpec* FindInGameSubScreenSpec(PanelKind k) {
    for (const auto& s : k_inGameSubScreens) {
        if (s.kind == k) return &s;
    }
    return nullptr;
}

// Walk the manager's panels[] for any in-game sub-screen panel
// (CSWGuiInGameInventory / Map / Journal / …). Used by the drill router to
// retarget the chain when g_drilledIntoSubScreen is set and the strip is fg.
//
// Returns the lowest-index match. CSWGuiManager::SendPanelToBack inserts at
// front of panels[], so the most recently opened sub-screen typically lives
// at index 0 — which is also what the user expects to navigate. Multiple
// sub-screens shouldn't normally coexist (SwitchToSWInGameGui pops the
// previous one before adding the new one), but if it ever happens we pick
// the first match deterministically.
static void* FindActiveSubScreenPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return nullptr;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (FindInGameSubScreenSpec(k)) return p;
    }
    return nullptr;
}

// Tracks which sub-screen panel pointers are currently in the manager's
// panels[]. Panels added since last tick → speak the screen's localized
// name once. Removed → drop from the tracked set so a re-open re-announces.
// Panels[] is small (≤16 in our cap) and turnover is human-paced, so a flat
// array is fine.
static void* g_visibleSubScreens[16];
static int   g_visibleSubScreenCount = 0;

static bool IsSubScreenTracked(void* p) {
    for (int i = 0; i < g_visibleSubScreenCount; ++i) {
        if (g_visibleSubScreens[i] == p) return true;
    }
    return false;
}

// Walk current panels[], speak on additions of any tracked sub-screen kind,
// drop removals from the tracked set. Called from MonitorPanelContents per
// tick, before the per-panel content fingerprint pass — the kind name lands
// first, then the fingerprint diff fills in the actual labels/items.
static void AnnounceNewSubScreens(void** panels, int count) {
    void* nowVisible[16];
    int   nowCount = 0;
    for (int i = 0; i < count && nowCount < 16; ++i) {
        void* p = panels[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        const InGameSubScreenSpec* spec = FindInGameSubScreenSpec(k);
        if (!spec) continue;
        nowVisible[nowCount++] = p;
        if (IsSubScreenTracked(p)) continue;
        // First sight in panels[] for this address+kind — speak the screen's
        // localized name. The user already heard the icon's name on focus
        // before activating; this is the "you are now in this screen"
        // confirmation.
        char text[128];
        bool spoke = false;
        if (spec->strref != 0xFFFFFFFFu &&
            LookupTlk(spec->strref, text, sizeof(text))) {
            acclog::Write("SubScreen open: panel=%p kind=%s strref=%u text=\"%s\"",
                          p, PanelKindName(k), spec->strref, text);
            tolk::Speak(text, /*interrupt=*/false);
            spoke = true;
        }
        if (!spoke) {
            acclog::Write("SubScreen open: panel=%p kind=%s text=\"%s\" (literal)",
                          p, PanelKindName(k), spec->literal);
            tolk::Speak(spec->literal, /*interrupt=*/false);
        }
    }
    memcpy(g_visibleSubScreens, nowVisible, sizeof(nowVisible));
    g_visibleSubScreenCount = nowCount;
}

// Build a fingerprint of the panel's label/listbox content. Buttons are
// skipped because their text mutates on hover (rendered border changes
// would create false-positive content changes). Whitespace-only fields
// are skipped (engine uses " " as a "not-yet-bound" placeholder).
//
// Output is the concatenation of contents separated by ' | ', truncated
// at outSize.
static void BuildContentFingerprint(void* panel, char* out, size_t outSize) {
    if (outSize == 0) return;
    out[0] = '\0';
    if (!panel) return;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 32 ? 32 : list->size;
    PanelKind kind = IdentifyPanel(panel);
    size_t off = 0;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        // Skip buttons — hover state mutates their border-rendered text.
        if (CallDowncast(c, kVtableAsButton) != nullptr) continue;
        if (CallDowncast(c, kVtableAsButtonToggle) != nullptr) continue;
        // Skip listboxes for panels that have a dedicated per-row monitor.
        // Their listbox text is the full concatenated row content, which the
        // per-row monitors (MonitorContainerSelection, MonitorEquipPickerSelection)
        // already announce as the user navigates. Including them in the
        // fingerprint causes duplicate speech AND makes the fingerprint diff
        // fire constantly — the engine flickers LB_ITEMS state on every
        // mouseOver change, which on InGameEquip happens every chain step.
        if ((kind == PanelKind::Container ||
             kind == PanelKind::InGameEquip) && IsListBox(c)) continue;

        char text[256];
        const char* src = ExtractAnnounceableText(c, text, sizeof(text), panel);
        if (!src) continue;
        size_t tlen = strnlen(text, sizeof(text));
        if (tlen == 0) continue;

        // Skip whitespace-only.
        bool allWs = true;
        for (size_t k = 0; k < tlen; ++k) {
            char ch = text[k];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                allWs = false; break;
            }
        }
        if (allWs) continue;

        size_t needed = (off > 0 ? 3 : 0) + tlen;
        if (off + needed + 1 >= outSize) break;
        if (off > 0) {
            out[off++] = ' ';
            out[off++] = '|';
            out[off++] = ' ';
        }
        memcpy(out + off, text, tlen);
        off += tlen;
        out[off] = '\0';
    }
}

// Check whether `seg` (length `segLen`) appears as a delimited segment
// in `hay`. Segments in our fingerprint are joined by " | " (sep, len 3),
// and may be at the start / end of `hay`. Used by SpeakNewSegments to
// avoid re-speaking content that's already in the previous fingerprint.
static bool FingerprintContainsSegment(const char* hay, size_t hayLen,
                                       const char* seg, size_t segLen) {
    if (segLen == 0 || segLen > hayLen) return false;
    const char* sep = " | ";
    const size_t sepLen = 3;
    size_t i = 0;
    while (i + segLen <= hayLen) {
        if (memcmp(hay + i, seg, segLen) == 0) {
            bool startOk = (i == 0) ||
                (i >= sepLen && memcmp(hay + i - sepLen, sep, sepLen) == 0);
            bool endOk = (i + segLen == hayLen) ||
                (i + segLen + sepLen <= hayLen &&
                 memcmp(hay + i + segLen, sep, sepLen) == 0);
            if (startOk && endOk) return true;
        }
        ++i;
    }
    return false;
}

// Speak each segment of `curr` that isn't already a segment of `prev`.
// Segments in the fingerprint are delimited by " | " (BuildContentFingerprint).
// Order is preserved so the speech matches the panel's physical layout.
//
// This replaces the previous "speak the whole concatenated fingerprint on any
// change" behavior, which caused redundant blob announcements every time any
// single label in a monitored panel mutated — see LB_ITEMS flicker on the
// equipment screen as the canonical case (every arrow keystroke triggered
// a full re-read of every label and listbox item in the panel).
static void SpeakNewSegments(const char* prev, const char* curr) {
    const char* sep = " | ";
    const size_t sepLen = 3;
    size_t prevLen = strlen(prev);
    const char* p = curr;
    while (*p) {
        const char* end = strstr(p, sep);
        size_t segLen = end ? (size_t)(end - p) : strlen(p);
        if (segLen > 0 &&
            !FingerprintContainsSegment(prev, prevLen, p, segLen)) {
            char seg[256];
            size_t cp = segLen < sizeof(seg) - 1 ? segLen : sizeof(seg) - 1;
            memcpy(seg, p, cp);
            seg[cp] = '\0';
            tolk::Speak(seg, /*interrupt=*/false);
            acclog::Write("ContentChange:   spoke \"%s\"", seg);
        }
        if (!end) break;
        p = end + sepLen;
    }
}

// Get-or-create the snapshot slot for a panel. FIFO-evicts when full.
static char* GetContentSnapshot(void* panel) {
    for (int i = 0; i < g_contentSnapshotCount; ++i) {
        if (g_contentSnapshots[i].panel == panel) {
            return g_contentSnapshots[i].text;
        }
    }
    if (g_contentSnapshotCount >= kMaxContentSnapshots) {
        memmove(g_contentSnapshots, g_contentSnapshots + 1,
                sizeof(g_contentSnapshots[0]) * (kMaxContentSnapshots - 1));
        g_contentSnapshotCount = kMaxContentSnapshots - 1;
    }
    int idx = g_contentSnapshotCount++;
    g_contentSnapshots[idx].panel = panel;
    g_contentSnapshots[idx].text[0] = '\0';
    return g_contentSnapshots[idx].text;
}

// Per-tick content scan. Walks the manager's panels[] (top to bottom),
// finds any panel of an interesting kind, snapshots its content
// fingerprint, announces diffs. Persistent panels with stable content
// (dialog-letterbox borders, etc.) settle to a fingerprint that matches
// the snapshot and stay quiet; only changes speak.
static void MonitorPanelContents() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return;
    if (panelCount > 16) panelCount = 16;

    // Speak on first sight of an in-game sub-screen (Inventory, Map, …).
    // Runs before the content-fingerprint loop so the kind name lands
    // before the per-panel label dump for the same panel.
    AnnounceNewSubScreens(panelData, panelCount);

    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        PanelKind k = IdentifyPanel(p);
        if (!IsContentMonitored(k)) continue;

        char fingerprint[512];
        BuildContentFingerprint(p, fingerprint, sizeof(fingerprint));

        char* last = GetContentSnapshot(p);
        if (strncmp(last, fingerprint, sizeof(g_contentSnapshots[0].text)) == 0) {
            continue;  // unchanged
        }

        // First-sight suppression. For panels whose appearance is already
        // announced by another path (Container's LBL_MESSAGE via Announce-
        // PanelTitle, every InGame{X} sub-screen via AnnounceNewSubScreens),
        // snapshot without speaking — the user already heard the kind name,
        // and per-row / per-control monitors take over from there. Subsequent
        // mutations still drive the diff path below.
        //
        // Non-suppressed kinds (TutorialBox, BarkBubble, AreaTransition,
        // MessageBoxModal, dialog cinematics) DO speak on first sight: their
        // content IS the announcement signal — no separate kind name path
        // already covers them.
        bool firstSight = (last[0] == '\0');
        bool suppressFirstSight =
            firstSight &&
            (k == PanelKind::Container || FindInGameSubScreenSpec(k) != nullptr);
        if (suppressFirstSight) {
            strncpy_s(last, sizeof(g_contentSnapshots[0].text),
                      fingerprint, _TRUNCATE);
            acclog::Write("ContentChange: panel=%p kind=%s first-sight snapshot "
                          "(deferring to kind-name path): \"%.200s\"",
                          p, PanelKindName(k), fingerprint);
            continue;
        }

        if (fingerprint[0] != '\0') {
            acclog::Write("ContentChange: panel=%p kind=%s",
                          p, PanelKindName(k));
            acclog::Write("ContentChange:   prev=\"%.300s\"", last);
            acclog::Write("ContentChange:   curr=\"%.300s\"", fingerprint);
            // Diff-based speech: only segments present in curr but absent in
            // prev are spoken. Eliminates the "speak the whole blob on any
            // change" pattern that surfaced as overlapping afterthought
            // announcements after every chain step on panels with mutating
            // labels (stat preview, listbox flicker, etc.).
            SpeakNewSegments(last, fingerprint);
        } else {
            acclog::Write("ContentChange: panel=%p kind=%s fingerprint cleared "
                          "(prev=\"%.100s\")", p, PanelKindName(k), last);
        }
        strncpy_s(last, sizeof(g_contentSnapshots[0].text),
                  fingerprint, _TRUNCATE);
    }
}

// =============================================================================
// Dialog-reply selection monitor.
//
// During an in-game conversation, the foreground panel is a CSWGuiDialog
// subclass (CSWGuiDialogCinematic, DialogComputer, etc.) whose child[1] is
// a CSWGuiListBox holding the player's reply choices. The engine's per-row
// arrow-key navigation mutates listbox.selection_index in place WITHOUT
// firing either CSWGuiPanel::SetActiveControl or CSWGuiListBox::
// SetActiveControl — so without a poll we never hear which reply is
// currently highlighted. The user sees the visual highlight move but we
// stay silent.
//
// MonitorDialogReplies snapshots selection_index per-listbox, announces
// the row's extracted text on change. State resets when we leave a dialog
// (so re-entering a new one announces from the new initial state). The
// content monitor (Layer 3) still handles the one-shot announcement of the
// full reply list when it first appears; this monitor is purely for
// per-row navigation announcements.
// =============================================================================

struct DialogReplyState {
    void* listBox;
    short lastSelection;
};
static DialogReplyState g_dialogReplyState = { nullptr, -1 };

static bool IsDialogPanelKind(PanelKind k) {
    switch (k) {
    case PanelKind::DialogCinematic:
    case PanelKind::DialogCinematicCopy:
    case PanelKind::DialogComputer:
    case PanelKind::DialogComputerCamera:
        return true;
    default:
        return false;
    }
}

// Find the first CSWGuiListBox child in a panel's controls. Returns
// nullptr if none. CSWGuiDialog::replies_listbox is at child[1] in
// observed panels (preceded by the message_label at child[0]); first-
// match on IsListBox is robust enough for the dialog case.
static void* FindListBoxChild(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 32 ? 32 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (c && IsListBox(c)) return c;
    }
    return nullptr;
}

static void MonitorDialogReplies() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;

    // Scan ALL panels in the manager's panels[] for a dialog-kind panel.
    // Was previously gating on fg, which fails because during arrow-key
    // navigation in a dialog the foreground panel switches to a separate
    // auxiliary panel (Unknown kind) — the actual dialog-cinematic panel
    // stays in panels[] but isn't fg, so the old fg-only check rejected
    // the dialog and reset the monitor state on every keystroke. Verified
    // in patch-20260502-192712.log: the same listbox 0FE2D434 stayed
    // allocated through all 8 reply turns; selection_index successfully
    // changed (initialSel went from -1 → 1 → 0 across turns) but every
    // change was missed because the monitor reset between them.
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* dialogPanel = nullptr;
    PanelKind dialogKind = PanelKind::Unknown;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            PanelKind pk = IdentifyPanel(p);
            if (IsDialogPanelKind(pk)) {
                dialogPanel = p;
                dialogKind  = pk;
                break;
            }
        }
    }

    if (!dialogPanel) {
        if (g_dialogReplyState.listBox) {
            acclog::Write("Dialog reply monitor disarmed: no dialog panel in stack");
            g_dialogReplyState.listBox = nullptr;
            g_dialogReplyState.lastSelection = -1;
        }
        return;
    }

    void* lb = FindListBoxChild(dialogPanel);
    if (!lb) return;
    PanelKind k = dialogKind;
    void* fg = dialogPanel;  // for log-line compatibility below

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    // First sight of this listbox: snapshot only (don't announce — the
    // content monitor already spoke the full reply list when the dialog
    // entered the reply state).
    if (g_dialogReplyState.listBox != lb) {
        g_dialogReplyState.listBox = lb;
        g_dialogReplyState.lastSelection = selIdx;
        acclog::Write("Dialog reply monitor armed: panel=%p kind=%s listbox=%p "
                      "initialSel=%d", fg, PanelKindName(k), lb, selIdx);
        return;
    }

    if (selIdx == g_dialogReplyState.lastSelection) return;
    short prev = g_dialogReplyState.lastSelection;
    g_dialogReplyState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Dialog reply selection cleared: listbox=%p prev=%d",
                      lb, prev);
        return;
    }

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Dialog reply selection out of range: listbox=%p sel=%d "
                      "size=%d", lb, selIdx,
                      (lbList ? lbList->size : -1));
        return;
    }

    void* row = lbList->data[selIdx];
    if (!row) return;

    char text[256];
    const char* src = ExtractAnnounceableText(row, text, sizeof(text));
    if (src) {
        acclog::Write("Dialog reply selected: panel=%p kind=%s listbox=%p "
                      "sel=%d (was %d) src=%s text=\"%s\"",
                      fg, PanelKindName(k), lb, selIdx, prev, src, text);
        tolk::Speak(text, /*interrupt=*/false);
    } else {
        char vtbl[160];
        DumpControlVtable(row, vtbl, sizeof(vtbl));
        acclog::Write("Dialog reply selected (src=none): panel=%p listbox=%p "
                      "sel=%d row=%p %s", fg, lb, selIdx, row, vtbl);
    }
}

// =============================================================================
// Container loot panel — input + per-row navigation.
//
// container.gui (extracted from data/gui.bif via xoreos-tools) defines a
// CSWGuiPanel with these stable child IDs:
//
//   id 0  LBL_MESSAGE     label   — "Inhalt des Beh\xE4lters" (take-mode) or
//                                   "F\xFCr diesen Beh\xE4lter verf\xFCgbare
//                                   Gegenst\xE4nde" (give-mode)
//   id 2  LB_ITEMS        listbox — chest contents OR player inventory
//                                   depending on mode
//   id 3  BTN_OK          button  — "Nehmen" (take-mode) / "Ablegen" (give-mode)
//   id 4  BTN_GIVEITEMS   button  — toggles take \xE2\x86\x94 give mode
//   id 5  BTN_CANCEL      button  — "Schliess." (close panel)
//
// Mode toggle is engine-native: clicking BTN_GIVEITEMS swaps LBL_MESSAGE's
// strref + LB_ITEMS source + BTN_OK label without our involvement. Our
// content monitor (Container in IsContentMonitored) catches the title swap.
//
// Input model in this panel:
//   * Up/Down       — pass through; engine's CSWGuiListBox handler mutates
//                     selection_index in place (no SetActiveControl callback,
//                     same as dialog-reply listbox — so we poll).
//   * Enter         — FireActivate BTN_OK (id=3).
//   * Tab           — FireActivate BTN_GIVEITEMS (id=4).
//   * Esc           — FireActivate BTN_CANCEL (id=5). Done explicitly here
//                     (rather than via the generic Esc \xE2\x86\x92 close-button
//                     heuristic below) so it doesn't depend on g_tabbedPanel
//                     being set, which only happens after the user has visited
//                     Options.
// =============================================================================
//
// Constants (kContainerBtnOkId, etc.) and FindControlById live earlier in the
// file (next to FindCloseButton) — they're shared with the Container input
// handler in OnHandleInputEvent.

struct ContainerSelState {
    void* listBox;
    short lastSelection;
};
static ContainerSelState g_containerSelState = { nullptr, -1 };

// Equipment picker selection-tracking state — declared next to
// g_containerSelState because MonitorEquipPickerSelection mirrors
// MonitorContainerSelection. The arming flags g_equipPickerActive /
// g_equipPickerPanel live earlier in the file (next to the other input-
// pipeline globals like g_drilledIntoSubScreen) because the input handler
// reads them before this declaration is reached.
struct EquipSelState {
    void* listBox;
    short lastSelection;
};
static EquipSelState g_equipSelState = { nullptr, -1 };

// Per-tick poll of the Container panel's listbox selection_index. First sight
// of a new listbox: announce item count (or "leer"), snapshot without reading
// the current row. Subsequent change: announce "<row text>, <i+1> von <N>".
//
// Mirrors MonitorDialogReplies in structure — both monitor a CSWGuiListBox
// whose selection_index is mutated in place by the engine's arrow-key handler
// without firing CSWGuiListBox::SetActiveControl.
static void MonitorContainerSelection() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* containerPanel = nullptr;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            if (IdentifyPanel(p) == PanelKind::Container) {
                containerPanel = p;
                break;
            }
        }
    }

    if (!containerPanel) {
        if (g_containerSelState.listBox) {
            acclog::Write("Container monitor disarmed: no Container panel in stack");
            g_containerSelState.listBox = nullptr;
            g_containerSelState.lastSelection = -1;
        }
        return;
    }

    void* lb = FindListBoxChild(containerPanel);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    // First sight of this listbox — speak count, snapshot, return. The user
    // already heard the panel title via AnnouncePanelTitle; the count tells
    // them there is something to navigate. They press Down to start walking.
    if (g_containerSelState.listBox != lb) {
        g_containerSelState.listBox       = lb;
        g_containerSelState.lastSelection = selIdx;
        if (rowCount <= 0) {
            tolk::Speak(acc::strings::Get(acc::strings::Id::ContainerEmpty),
                        /*interrupt=*/false);
            acclog::Write("Container monitor armed: panel=%p lb=%p empty initialSel=%d",
                          containerPanel, lb, selIdx);
        } else if (rowCount == 1) {
            tolk::Speak(acc::strings::Get(acc::strings::Id::ContainerOneItem),
                        /*interrupt=*/false);
            acclog::Write("Container monitor armed: panel=%p lb=%p count=1 initialSel=%d",
                          containerPanel, lb, selIdx);
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     acc::strings::Get(acc::strings::Id::FmtContainerItems),
                     rowCount);
            tolk::Speak(msg, /*interrupt=*/false);
            acclog::Write("Container monitor armed: panel=%p lb=%p count=%d initialSel=%d",
                          containerPanel, lb, rowCount, selIdx);
        }
        return;
    }

    if (selIdx == g_containerSelState.lastSelection) return;
    short prev = g_containerSelState.lastSelection;
    g_containerSelState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Container selection cleared: lb=%p prev=%d", lb, prev);
        return;
    }
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Container selection out of range: lb=%p sel=%d size=%d",
                      lb, selIdx, lbList ? lbList->size : -1);
        return;
    }
    void* row = lbList->data[selIdx];
    if (!row) return;

    char rowText[256];
    const char* src = ExtractAnnounceableText(row, rowText, sizeof(rowText));
    if (!src) {
        acclog::Write("Container row %d (lb=%p): no announceable text", selIdx, lb);
        return;
    }

    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, selIdx + 1, rowCount);
    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("Container row: lb=%p sel=%d (was %d) text=\"%s\"",
                  lb, selIdx, prev, rowText);
}

// Per-tick poll of the equipment-screen LB_ITEMS selection_index. Mirrors
// MonitorContainerSelection — same pattern: first sight after a panel
// transition snapshots without speaking, subsequent index changes speak the
// new row's text. Wakes up on panel arming (we set selection_index when the
// engine repopulates LB_ITEMS) and on user arrow-key driven changes.
//
// Auto-disarms when the InGameEquip panel falls out of panels[] so a
// reopen starts fresh.
static void MonitorEquipPickerSelection() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);

    void* equipPanel = nullptr;
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            void* p = panelData[i];
            if (!p) continue;
            if (IdentifyPanel(p) == PanelKind::InGameEquip) {
                equipPanel = p;
                break;
            }
        }
    }

    if (!equipPanel) {
        if (g_equipSelState.listBox) {
            acclog::Write("EquipPicker monitor disarmed: no InGameEquip panel in stack");
            g_equipSelState.listBox       = nullptr;
            g_equipSelState.lastSelection = -1;
        }
        if (g_equipPickerActive) {
            acclog::Write("EquipPicker: disarm — panel gone from panels[]");
            g_equipPickerActive = false;
            g_equipPickerPanel  = nullptr;
        }
        return;
    }

    void* lb = FindControlById(equipPanel, kEquipLbItemsId);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    // First sight of this listbox — snapshot without speaking. Don't speak
    // a count line on the equip screen: at panel-open the listbox is empty
    // (the engine fills it after the user activates a slot), and users have
    // already heard the panel name + tutorial. Re-speak only on changes.
    if (g_equipSelState.listBox != lb) {
        g_equipSelState.listBox       = lb;
        g_equipSelState.lastSelection = selIdx;
        acclog::Write("EquipPicker monitor armed: panel=%p lb=%p rows=%d initialSel=%d",
                      equipPanel, lb, rowCount, selIdx);
        return;
    }

    if (selIdx == g_equipSelState.lastSelection) return;
    short prev = g_equipSelState.lastSelection;
    g_equipSelState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("EquipPicker selection cleared: lb=%p prev=%d", lb, prev);
        return;
    }
    if (selIdx == 0) {
        // Row 0 is the protoitem template — never an item the user can equip.
        // Should be unreachable now that the picker handler clamps to >=1, but
        // log if the engine somehow lands here so we notice.
        acclog::Write("EquipPicker selection on protoitem (sel=0): lb=%p", lb);
        return;
    }
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("EquipPicker selection out of range: lb=%p sel=%d size=%d",
                      lb, selIdx, lbList ? lbList->size : -1);
        return;
    }
    void* row = lbList->data[selIdx];
    if (!row) return;

    char rowText[256];
    const char* src = ExtractAnnounceableText(row, rowText, sizeof(rowText));
    if (!src) {
        acclog::Write("EquipPicker row %d (lb=%p): no announceable text", selIdx, lb);
        return;
    }

    // Reuse the container "X, i von N" format. selIdx is offset by 1 because
    // row 0 is the protoitem template — user-facing position is selIdx (so
    // sel=1 reads as "1 of N") and the user-facing total is rowCount-1.
    int userPos   = selIdx;
    int userTotal = rowCount - 1;
    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, userPos, userTotal);
    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("EquipPicker row: lb=%p sel=%d (was %d) text=\"%s\"",
                  lb, selIdx, prev, rowText);
}

// Container give-mode toggle key — Win32 poll for G. The natural key (Tab)
// never reaches CSWGuiManager::HandleInputEvent because the engine's player-
// control / Change-Leader layer consumes Tab before menu-input dispatch
// (verified empirically: three Tab presses logged only by DiagSelect; zero
// LOGICAL_TAB events at the manager hook in patch-20260504-103242.log lines
// 1380-1382). Win32 GetAsyncKeyState bypasses the engine's input pipeline
// entirely, so we always see the press regardless of what the engine is
// doing with it.
//
// G is "Stealth Mode" in-world but is a harmless no-op while a menu panel
// is foreground, so claiming it for give-mode toggle inside the Container
// panel doesn't fight the existing keymap. Gated to Container-panel-fg so
// the binding is scoped — outside the loot UI G still triggers its in-world
// stealth behaviour.
static void PollContainerGiveModeKey() {
    static bool s_prevG = false;
    bool g = (GetAsyncKeyState('G') & 0x8000) != 0;
    bool risingG = g && !s_prevG;
    s_prevG = g;
    if (!risingG) return;

    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fgWnd, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    void* fgPanel = acc::engine::GetForegroundPanel(mgr);
    if (!fgPanel || IdentifyPanel(fgPanel) != PanelKind::Container) return;

    if (g_pendingClick || g_pendingActivate || g_pendingCursorMove) {
        acclog::Write("Container: G (give-mode) -- op already pending; ignoring");
        return;
    }
    void* btn = FindControlById(fgPanel, kContainerBtnGiveId);
    if (!btn) {
        acclog::Write("Container: G (give-mode) -- BTN_GIVEITEMS not found on panel=%p",
                      fgPanel);
        return;
    }
    g_pendingActivate       = true;
    g_pendingActivateTarget = btn;
    acclog::Write("Container: G (give-mode) -> FireActivate BTN_GIVEITEMS panel=%p target=%p",
                  fgPanel, btn);
}

// CSWGuiManager::Update — hooked mid-function at 0x40ce76. Per-frame tick run
// once after input dispatch by CClientExoAppInternal::MainLoop. Used as a safe
// callback site for the deferred MoveMouseToPosition triggered by chain
// navigation: the engine's input pipeline is NOT mid-flight here, so cursor
// updates can recurse through HandleMouseMove without re-entrancy.
//
// The cut byte is `mov eax, [ebp+0x8c]` (a panel-list field load); EBP is the
// manager pointer (the engine's `mov ebp, ecx` at 0x40ce74 happens before our
// hook). We pass EBP as the parameter for clarity even though we also have
// the global at 0x7A39F4 — both resolve to the same singleton.
extern "C" void __cdecl OnUpdate(void* /*gmFromEbp*/) {
    MonitorFocusedControl();
    MonitorPanelContents();
    MonitorDialogReplies();
    MonitorContainerSelection();
    MonitorEquipPickerSelection();
    PollContainerGiveModeKey();

    // Pillar 4 cycle keys via Win32 polling. Stock kotor.ini doesn't bind
    // `,/./-`, so OnHandleInputEvent never sees them in-world (the engine's
    // keymap drops unbound scancodes before our manager-side hook).
    // GetAsyncKeyState reads OS-level keyboard state directly, edge-detects
    // rising edges, and self-gates on GetPlayerPosition. Verified
    // 2026-05-04 from patch-20260503-215023.log: 86 events captured at the
    // manager hook, zero with codes 103/104/105.
    acc::cycle_input::PollWin32();

    // Autowalk progress watchdog. No-op when no recent WalkTo dispatch;
    // emits at most two log lines (t+1s, t+3s) per dispatch to detect
    // "engine accepted but didn't move" — the canonical autowalk failure
    // mode (e.g. tutorial-locked sections, queue blocked by higher-priority
    // action). Permanent instrumentation; reused by every guidance caller.
    acc::guidance::TickProgressWatchdog();

    // Auto-restore player input ~3s after a guidance / interact dispatch
    // disabled it. Idle when no disable session is active. See
    // engine_player.h SetPlayerInputEnabled doc + memory entry
    // project_player_control_toggle.md for the why.
    acc::engine::TickPlayerInputRestore();

    // Phase 2 lay-off 9a — passive-selection narration loop. Reads engine
    // LastTarget per tick; on change to a nav-relevant target, speaks the
    // localised name + plays the per-category 3D cue at the object's
    // position. Independent of the Pillar 4 cycle channel — both can fire
    // on the same object; recency-suppress to be added if double-narration
    // proves disruptive. Self-gates on player-loaded.
    acc::passive_narrate::Tick();

    // Phase 2 ad-hoc — octagonal direction-on-turn announcement (Pillar 2
    // sub-feature C, pulled forward to give the user feedback that A/D /
    // Q/E are turning the character vs. only the camera). Speaks "north" /
    // "north-east" etc. on sector change with 5° hysteresis.
    acc::turn_announce::Tick();

    // Phase 2 ad-hoc — camera-direction announcement on A/D. KOTOR 1's
    // verified default control scheme: A/D rotate the *camera* around the
    // character (NOT character facing), W moves the character in the
    // camera's forward direction. Without camera feedback the user has
    // no idea where the camera is pointing. Dead-reckons camera yaw from
    // observed A/D held state + 200°/s default DPS; resyncs to character
    // yaw on each character-yaw change (every W press snaps the character
    // to face camera, anchoring the estimate).
    acc::camera_announce::Tick();

    // Phase 2 diagnostic — Q/E/Tab logging. Engine has its own
    // target-cycle on Q/E (CClientExoAppInternal::SelectNearestObject
    // @0x005fb050) per investigation Q6 + verified web sources. Logs
    // keypresses with current LastTarget so we can correlate against
    // passive_narrate's `LastTarget changed` lines and decide whether to
    // delegate our `,`/`.` cycle to the engine's primitive or keep our
    // own filter. Removable in one commit once decided.
    acc::diag::engine_select::Tick();

    // Phase 2 lay-off 9b — combined autowalk+interact hotkey (Enter).
    // Resolves cycle focus first / engine LastTarget fallback, speaks
    // localised pre-roll ("Sprich mit X" / "Öffne X" / "Hebe X auf"),
    // then routes through the engine's native click pipeline:
    // SetLastClickedOnTarget(handle) + HandleMouseClickInWorld. Engine
    // walks the player + dispatches kind-appropriate interaction.
    // Side-channel test of the parked autowalk blocker — if this path
    // moves the player when raw AddMoveToPointAction doesn't, the engine
    // click pipeline is the missing layer.
    acc::interact::PollHotkey();

    // Phase 2 lay-off 9-probe — in-world cursor-warp / passive-selection
    // monitor. Probe RESOLVED 2026-05-04 — see investigation Q6 §"RE —
    // does MoveMouseToPosition trigger world-hover?". Layer A viable
    // (LastTarget populates organically); Layer C dropped. Probe stays in
    // tree until 9a's narration loop is verified working in production
    // against the same handle stream the probe logged; deletable in a
    // single commit thereafter.
    acc::probe::world_hover::TickMonitor();
    acc::probe::world_hover::PollHotkey();

    if (!g_pendingCursorMove && !g_pendingClick && !g_pendingActivate &&
        !g_pendingSliderInput) return;

    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) {
        acclog::Write("Update: pending op but GuiManager singleton is NULL");
        g_pendingCursorMove     = false;
        g_pendingClick          = false;
        g_pendingActivate       = false;
        g_pendingSliderInput    = false;
        g_pendingTarget         = nullptr;
        g_pendingActivateTarget = nullptr;
        g_pendingSliderTarget   = nullptr;
        return;
    }

    // CSWGuiManager mouseOverControl pointer at +0x8 (per the decompilation
    // in docs/menu-nav-design.md). Reading it directly lets us verify what
    // the engine's hit-test resolved the cursor to — the difference between
    // "click landed on tab T" and "click landed on the inline listbox" is
    // invisible from cursor coords alone.
    auto getMouseOver = [&]() -> void* {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gm) + 0x8);
    };

    if (g_pendingCursorMove) {
        g_pendingCursorMove = false;
        auto move = reinterpret_cast<PFN_MoveMouseToPosition>(kAddrMoveMouseToPosition);
        // Capture mouseOver BEFORE the move. Disambiguates whether
        // MoveMouseToPosition itself produces the 45-px-shifted hit-test
        // result (before==something-else, after==shifted-button) vs. the
        // engine pre-setting mouseOver during panel init (before==after,
        // never refreshed by our move at all). See chat.
        void* moBefore = getMouseOver();
        move(gm, g_pendingX, g_pendingY);
        acclog::Write("Update: MoveMouseToPosition(%d, %d) target=%p mouseOver before=%p after=%p",
                      g_pendingX, g_pendingY, g_pendingTarget,
                      moBefore, getMouseOver());
    }

    // Click-sim via manager's HandleLMouseDown/Up. Dispatches against
    // mouseOverControl, which on Options-style tabbed panels resolves to
    // the button one step above the chain target (consistent 45-px hit-test
    // shift — see chat investigation). That activates the wrong tab.
    //
    // Direct vtable[6]/[7] on the chain target was tried as a workaround
    // (commit reverted) — it crashes the game on the second tab+ click.
    // The button's own HandleLMouseDown/Up depend on manager-side state
    // (probably the +0x1c mouse_held bit and/or other setup we'd skip).
    // Need a different approach for the off-by-1; for now keep the original
    // pipeline so behavior is at least stable.
    if (g_pendingClick) {
        g_pendingClick = false;
        g_pendingClickTarget = nullptr;
        auto down = reinterpret_cast<PFN_ManagerLMouseDown>(kAddrManagerLMouseDown);
        auto up   = reinterpret_cast<PFN_ManagerLMouseUp>(kAddrManagerLMouseUp);
        void* moBefore = getMouseOver();
        int dResult = down(gm, /*press=*/1);
        void* moAfterDown = getMouseOver();
        int uResult = up(gm);
        void* moAfterUp = getMouseOver();
        acclog::Write("Update: click-sim Down=%d Up=%d at (%d,%d) target=%p "
                      "mouseOver before=%p afterDown=%p afterUp=%p",
                      dResult, uResult, g_pendingX, g_pendingY, g_pendingTarget,
                      moBefore, moAfterDown, moAfterUp);
    }

    // Direct activate via vtable[15].HandleInputEvent(0x27, 1). Bypasses
    // hit-test, so a button covered by a listbox extent (e.g. Schliess.
    // in an Options sub-dialog) still fires its onClick when we target it.
    //
    // Post-activation re-announce is handled generically by
    // MonitorFocusedControl on the next tick: toggles flip +0x1c8 bit 0
    // synchronously inside FireActivate, cycles rewrite the value-display
    // button's CExoString in place, sliders mutate cur_value at +0x74. All
    // three produce a different ExtractAnnounceableText on next entry, and
    // the monitor speaks the diff.
    if (g_pendingActivate) {
        g_pendingActivate = false;
        void* tgt = g_pendingActivateTarget;
        g_pendingActivateTarget = nullptr;
        acclog::Write("Update: FireActivate target=%p", tgt);
        FireActivate(tgt);
    }

    // Slider value adjustment via vtable[15].HandleInputEvent(500/501, 1).
    // The slider's HandleInputEvent runs SetCurValue (clamped to
    // [0, max_value]) and the gui_object callback that propagates to the
    // audio system, then plays the click feedback sound. Per-frame focus
    // monitor catches the cur_value change at +0x74 on the next tick.
    if (g_pendingSliderInput) {
        g_pendingSliderInput = false;
        void* tgt  = g_pendingSliderTarget;
        int   code = g_pendingSliderCode;
        g_pendingSliderTarget = nullptr;
        g_pendingSliderCode   = 0;
        if (tgt) {
            void** vtable = *reinterpret_cast<void***>(tgt);
            if (vtable) {
                auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
                    vtable[kVtableHandleInputEvent]);
                if (fn) {
                    acclog::Write("Update: slider HandleInputEvent target=%p code=%d",
                                  tgt, code);
                    fn(tgt, code, 1);
                }
            }
        }
    }

}

// Read a snapshot of the listbox's cursor / flags / size into a string. Shared
// between the click and key handlers so all listbox events log the same fields.
static void DumpListBoxState(void* listBox, char* out, size_t outSize) {
    if (!listBox) {
        snprintf(out, outSize, "list=NULL");
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(listBox);
    short selIdx       = *reinterpret_cast<short*>(base + kListBoxSelectionIndexOffset);
    short topVisible   = *reinterpret_cast<short*>(base + kListBoxTopVisibleIndexOffset);
    short itemsPerPage = *reinterpret_cast<short*>(base + kListBoxItemsPerPageOffset);
    uint32_t bitFlags  = *reinterpret_cast<uint32_t*>(base + kListBoxBitFlagsOffset);
    auto* ctrls        = reinterpret_cast<CExoArrayList*>(base + kListBoxControlsOffset);
    int ctrlsSize      = ctrls ? ctrls->size : -1;
    snprintf(out, outSize,
             "list=%p sel=%d top=%d perPage=%d size=%d flags=0x%x",
             listBox, selIdx, topVisible, itemsPerPage, ctrlsSize, bitFlags);
}

// CSWGuiListBox::HandleLMouseDown — entry hook @0x0041c4a0. Click press.
extern "C" void __cdecl OnListBoxLMouseDown(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::LMouseDown #%d %s", n, state);
}

// CSWGuiListBox::HandleLMouseUp — entry hook @0x0041a700. Click release; this
// is where the click action commits and the row's callback fires. Pair with
// the next OnListBoxSetSelectedControl / OnListBoxSetActiveControl events to
// see the full chain.
extern "C" void __cdecl OnListBoxLMouseUp(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::LMouseUp #%d %s", n, state);
}

// CSWGuiListBox::HandleInputEvent — entry hook @0x0041ce20. Per-listbox key
// dispatch. Fires only when the listbox is the focused control AND the
// engine routes the key down to it. We don't extract param_1/param_2 here
// (would need stack-source path which is broken upstream) — correlate by
// timestamp with the manager-level HandleInputEvent log line that fired
// just before.
extern "C" void __cdecl OnListBoxHandleInput(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::HandleInputEvent #%d %s", n, state);
}

// CSWGuiListBox::SetSelectedControl — entry hook @0x0041c040. Fires whenever
// the listbox's selection index changes, regardless of source (keyboard, mouse,
// programmatic). Reads the OLD selection_index pre-update; the next
// OnListBoxSetActiveControl event will reveal the new value.
extern "C" void __cdecl OnListBoxSetSelectedControl(void* listBox) {
    EnsureTolkInitialized();
    static int n = 0;
    ++n;
    char state[160];
    DumpListBoxState(listBox, state, sizeof(state));
    acclog::Write("ListBox::SetSelectedControl #%d %s (pre-update)", n, state);
}

// DllMain + OnRulesInit + EnsureTolkInitialized live in core_dllmain.cpp.
