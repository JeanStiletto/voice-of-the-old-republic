// KOTOR Accessibility — control text extraction (the announce ladder).
//
// Step 2B of the menus.cpp refactor. ExtractAnnounceableText (renamed
// FromControl) and its three extract-only helpers (FindSiblingLabel,
// IsCycleFlankerArrow, LookupCycleCategory) lift out of menus.cpp.
// Body is unchanged from the original; helpers shared with chain
// navigation (IsChainNavigable, IsClassSelectionIcon, ClassLabelCache*,
// GetControlCenter) and the focused-panel pointer (g_currentPanel) cross
// the seam via menus_internal.h.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_extract.h"

#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "log.h"
#include "menus_charsheet.h"
#include "menus_credits.h"
#include "menus_internal.h"
#include "strings.h"

using namespace acc::engine;

// Bring shared seam helpers into unqualified scope so the function bodies
// read as they did when everything lived in menus.cpp.
using acc::menus::detail::IsChainNavigable;
using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::ClassLabelCacheLookup;
using acc::menus::detail::ClassLabelCacheStore;
using acc::menus::detail::GetControlCenter;

namespace acc::menus::extract {

namespace {

// CSWCCreature::GetPortraitId thiscall typedef — verified moving in the
// chargen probe (24 → 25 across a net +1 cycle). Storage location is
// opaque; the accessor is the only reliable source.
typedef uint32_t (__thiscall* PFN_CSWCCreatureGetPortraitId)(void*);

// CSWCCreature::GetPortrait — fills caller-supplied 16-byte CResRef.
// `side` is 0 for the default (light-side) baseresref. The function
// returns the same outBuf pointer; we don't use the return value.
typedef void* (__thiscall* PFN_CSWCCreatureGetPortrait)(
    void* this_, void* outBuf, int side);

// portraits.2da row → baseresref. Indices 0..31 cover the chargen PC
// rows (5 variants × 3 race codes × 2 genders, with two interleaved
// non-PC rows — 12 = po_pt3m3, 13 = po_pcarth — left as nullptr because
// the chargen filter (forpc=1) skips them and they aren't reachable
// via the cycle. Companion rows 32+ (po_pbastila, po_phk47, etc.) are
// also unreachable.
//
// Order derived from the data section of build/2da-extracted/portraits.2da
// (string offsets monotonic in row order). Verified in-game:
// portrait_id=24 is reached when cycling male portraits and matches
// "po_pmhc3" (parsed → "männlich hellhäutig 3" / "male light-skinned 3").
constexpr const char* kPortraitByRow[32] = {
    "po_pfha1", "po_pfha2", "po_pfha3", "po_pfha4", "po_pfha5",  //  0..4
    "po_pfhc1", "po_pfhc2", "po_pfhc3", "po_pfhc4", "po_pfhc5",  //  5..9
    "po_pfhb1", "po_pfhb2",                                       // 10..11
    nullptr, nullptr,                                             // 12..13 (T3, Carth)
    "po_pfhb3", "po_pfhb4", "po_pfhb5",                          // 14..16
    "po_pmha1", "po_pmha2", "po_pmha3", "po_pmha4", "po_pmha5",  // 17..21
    "po_pmhc1", "po_pmhc2", "po_pmhc3", "po_pmhc4", "po_pmhc5",  // 22..26
    "po_pmhb1", "po_pmhb2", "po_pmhb3", "po_pmhb4", "po_pmhb5",  // 27..31
};

// Cycle-category cache. Cycle widgets render as `[◀] value [▶]`; on each
// activation the engine rewrites the middle button's CExoString to the
// new value, losing the category name. We capture the (control →
// category) mapping at panel-walk time, BEFORE any activation runs, so
// FromControl can prepend the category in announce output.
//
// Populated externally via the public CaptureCycleCategory setter
// (called from OnSetActiveControl in menus.cpp on first focus into a
// panel). Cleared on the same path via ResetCycleCategoryCache.
// Invalidated implicitly on every RebindChain because the panel-walk
// path always resets-then-recaptures.
struct CycleCategoryEntry {
    void* control;
    char  category[128];
};
constexpr int kMaxCycleCategoryEntries = 16;
CycleCategoryEntry s_cycleCategories[kMaxCycleCategoryEntries];
int s_cycleCategoryCount = 0;

const char* LookupCycleCategory(void* control) {
    for (int i = 0; i < s_cycleCategoryCount; ++i) {
        if (s_cycleCategories[i].control == control) {
            return s_cycleCategories[i].category;
        }
    }
    return nullptr;
}

// True for image-only buttons that flank a cycle value-display button at
// the same y-row (e.g. the [◀]/[▶] arrows around `Normal` in the
// Difficulty cycle). Suppresses sibling-label fallback so the cycle
// mechanism's empty-text predicate engages — without this gate, the
// nearest plain label (which IS the value display) would paint both
// flankers, defeating the chain squash and FindAdjacentArrow.
//
// Heuristic:
//   - self has empty inline text (real cycle value buttons fail this);
//   - some sibling button at the same y-row within ±80 px on x has
//     non-empty text (the value-display);
//   - self is downcastable to AsButton (sliders / labels / listboxes
//     don't reach here — those announce paths run earlier in the ladder).
//
// Used only by FromControl; co-located here, not in any chain-side TU.
// CSWGuiClassSelection icons are positionally separate and handled by
// IsClassSelectionIcon; chargen wizard step decorations are CSWGuiLabel-
// Hilight, not buttons. CSWGuiSlider category resolution uses
// FindSiblingLabel against a slider control, never a button, so this
// predicate never fires there either.
bool IsCycleFlankerArrow(void* panel, void* control) {
    if (!panel || !control) return false;
    if (CallDowncast(control, kVtableAsButton) == nullptr) return false;

    // Self must have empty own text — real cycle value buttons (carrying
    // "Normal", "8", "Aus", …) keep the sibling-label fallback off the
    // table anyway, so this check guards the "image-only" classification.
    char selfTxt[64];
    if (ExtractTextOrStrRefIndirect(control,
                                    kButtonTextOffset,
                                    kButtonStrRefOffset,
                                    kButtonTextObjectOffset,
                                    selfTxt, sizeof(selfTxt))) {
        return false;
    }

    int focusCx, focusCy;
    if (!GetControlCenter(control, focusCx, focusCy)) return false;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list || !list->data || list->size <= 0) return false;

    constexpr int kSameRowDyTol = 5;
    constexpr int kMaxDxPx      = 80;  // matches FindAdjacentArrow's reach

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == control) continue;
        if (CallDowncast(c, kVtableAsButton) == nullptr) continue;

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;
        int dy = cy - focusCy;
        if (dy < -kSameRowDyTol || dy > kSameRowDyTol) continue;
        int dx = cx - focusCx;
        int adx = dx < 0 ? -dx : dx;
        if (adx == 0 || adx > kMaxDxPx) continue;

        char nbrTxt[64];
        if (ExtractTextOrStrRefIndirect(c,
                                        kButtonTextOffset,
                                        kButtonStrRefOffset,
                                        kButtonTextObjectOffset,
                                        nbrTxt, sizeof(nbrTxt))) {
            return true;
        }
    }
    return false;
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
// doesn't recurse through FromControl so it's safe to call from inside
// extraction code.
const char* FindSiblingLabel(void* panel, void* control,
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

}  // namespace

void ResetCycleCategoryCache() {
    s_cycleCategoryCount = 0;
}

void CaptureCycleCategory(void* control, const char* category) {
    if (!control || !category) return;
    // Upsert: if `control` is already in the cache, replace its category
    // text. This lets a panel-specific override (e.g. chargen Attribute's
    // ability_button → ability_label binding in menus_chargen_attr.cpp)
    // replace whatever the generic capture loop registered earlier this
    // panel-walk pass — without an upsert, `LookupCycleCategory` returns
    // the first hit and the override silently loses.
    for (int i = 0; i < s_cycleCategoryCount; ++i) {
        if (s_cycleCategories[i].control == control) {
            strncpy_s(s_cycleCategories[i].category, category, _TRUNCATE);
            return;
        }
    }
    if (s_cycleCategoryCount >= kMaxCycleCategoryEntries) return;
    s_cycleCategories[s_cycleCategoryCount].control = control;
    strncpy_s(s_cycleCategories[s_cycleCategoryCount].category,
              category, _TRUNCATE);
    ++s_cycleCategoryCount;
}

const char* FromControl(void* control,
                        char* outBuf, size_t bufSize,
                        void* ownerPanel) {
    if (!control || bufSize < 2) return nullptr;

    const char* source = nullptr;

    // 0. Per-kind row formatter for virtual chain entries. Runs BEFORE
    //    the standard text-extraction ladder because the formatted
    //    phrase must OVERRIDE the bare label text. lbl_class's inline
    //    CExoString is "Soldat"; the user navigating the stat chain
    //    expects "Klasse: Soldat" with the context word, so without
    //    this early hook the AsLabel path (section 4) would return the
    //    bare value and never reach the per-kind handler.
    //
    //    Only fires when `control` is a registered anchor for the
    //    owning panel's kind (IsStatRowAnchor short-circuits otherwise),
    //    so non-virtual controls in InGameCharacter still hit the
    //    standard ladder.
    //
    //    Owner is resolved inline rather than at line 669 because that
    //    block is after the standard extract sections; we need owner
    //    here. Cost is one extra FindOwningPanel call when ownerPanel
    //    is null (announce-control path).
    {
        void* owner = ownerPanel;
        if (!owner) owner = FindOwningPanel(control);
        if (!owner) owner = g_currentPanel;
        // Filter stale/wild owner pointers — the resolution chain can
        // surface a freed g_currentPanel right after a commit-style button
        // (Annehmen on InGameLevelUp) synchronously destroys its panel
        // inside our FireActivate dispatch. IsPanelInManager is
        // deref-free, so it's safe even when `owner` is wild. Downstream
        // probes (IdentifyPanel, IsStatRowAnchor) deref the panel vtable
        // and would AV otherwise.
        if (owner && !acc::engine::IsPanelInManager(owner)) owner = nullptr;
        if (owner && IdentifyPanel(owner) ==
                PanelKind::InGameCharacter &&
            acc::menus::charsheet::IsStatRowAnchor(owner, control)) {
            __try {
                if (acc::menus::charsheet::ExtractStatRow(
                        owner, control, outBuf, bufSize) &&
                    outBuf[0] != '\0') {
                    source = "perkind-charsheet-row";
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                source = nullptr;
            }
        }
        // Credits row (Inventory + Store). IsCreditsRowAnchor self-gates on
        // the owning panel's kind, so the per-kind branch isn't repeated
        // here — same shape as the stat-row block. Only fires when the
        // control is the panel's credits_value_label inline member.
        if (!source && owner &&
            acc::menus::credits::IsCreditsRowAnchor(owner, control)) {
            __try {
                if (acc::menus::credits::ExtractCreditsRow(
                        owner, control, outBuf, bufSize) &&
                    outBuf[0] != '\0') {
                    source = "perkind-credits-row";
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                source = nullptr;
            }
        }
    }

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

    // 6b. CSWGuiEditbox — same vtable-identity pattern as slider (no AsEditbox
    //     accessor exists). The chargen Name screen's `name_editbox` is the
    //     only editbox in vanilla KOTOR. Speech format: "{role}. {value}" so
    //     the screen-reader user immediately knows they've landed in an input
    //     field plus the current contents. The per-tick poll in
    //     menus_editbox.cpp owns subsequent text-change announcements (single
    //     char on insert/delete, full re-read on Up/Down or Random-button
    //     replacement) — this branch only handles the focus-enter announce.
    if (!source && IsEditbox(control)) {
        const char* role  = acc::strings::Get(acc::strings::Id::EditboxRole);
        const char* empty = acc::strings::Get(acc::strings::Id::EditboxEmpty);
        const char* cstr =
            *reinterpret_cast<const char**>(
                static_cast<unsigned char*>(control) + kEditboxStringCStrOffset);
        uint32_t len = *reinterpret_cast<uint32_t*>(
            static_cast<unsigned char*>(control) + kEditboxStringLengthOffset);
        if (cstr && len > 0) {
            // Bound the copy so a corrupted length can't blow past outBuf.
            char text[128];
            uint32_t copyLen = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
            memcpy(text, cstr, copyLen);
            text[copyLen] = '\0';
            snprintf(outBuf, bufSize, "%s. %s", role, text);
        } else {
            snprintf(outBuf, bufSize, "%s. %s", role, empty);
        }
        source = "editbox";
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
            // PartySelection (Gruppenauswahl) portrait-slot buttons —
            // children [9..17] of CSWGuiPartySelection in
            // patch-20260504-160847.log. vtable[22]=0x00641DB0 (AsButton
            // returns this) but the standard CallDowncast path at the
            // CSWGuiButton text offsets returns empty, so they show
            // src=none on the screen-reader pass. Registering here puts
            // the speculative button-text reads on the allowlist; if the
            // text sits at non-standard offsets we'll see misses logged
            // and can chase the right field next.
            { 0x00756BB8, false, true,  "party-portrait-spec" },
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
                    acclog::Write("Menus.SpecRead", "label SEH for vtable=0x%x "
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
                    acclog::Write("Menus.SpecRead", "button SEH for vtable=0x%x "
                                  "control=%p", (unsigned)vta, control);
                    got = false;
                }
            }
            if (got) {
                size_t tlen = strnlen(text, sizeof(text));
                if (tlen > 0 && tlen + 1 <= bufSize) {
                    memcpy(outBuf, text, tlen + 1);
                    source = ov.tag;
                    // Trace: chain rebind / step / fingerprint all visit the
                    // same control multiple times per arrow press; collapse
                    // identical hit/empty/miss runs while preserving the
                    // suppressed count.
                    acclog::Trace("Menus.SpecRead",
                                  "hit vtable=0x%x control=%p text=\"%s\"",
                                  (unsigned)vta, control, outBuf);
                    break;
                }
                acclog::Trace("Menus.SpecRead",
                              "empty vtable=0x%x control=%p "
                              "(read returned but text was empty)",
                              (unsigned)vta, control);
            } else {
                acclog::Trace("Menus.SpecRead",
                              "miss vtable=0x%x control=%p tag=%s",
                              (unsigned)vta, control, ov.tag);
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
    // Same stale-owner filter as section 0 above — drops freed
    // g_currentPanel before any of the per-kind detectors (IdentifyPanel,
    // IsClassSelectionIcon, IsPanelKindInGameMenu) dereferences it.
    if (ownerForPerkind && !acc::engine::IsPanelInManager(ownerForPerkind)) {
        ownerForPerkind = nullptr;
    }
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
                            acclog::Write("Menus.PerKind", "InGameMenu TLK control=%p "
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
                        acclog::Write("Menus.PerKind", "InGameMenu literal control=%p "
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
                        acclog::Write("Menus.PerKind", "InGameEquip TLK control=%p "
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
                    acclog::Write("Menus.PerKind", "InGameEquip literal control=%p "
                                  "id=%d strref=%u -> \"%s\"",
                                  control, cid, s.strref, outBuf);
                }
            }
            break;
        }
    }

    // 9b2. Per-kind label fallback for the in-game map (CSWGuiInGameMap).
    //     The panel has two image-only buttons on either side of the map
    //     render — `up_button` at struct offset +0xab0 and `down_button`
    //     at +0xc74 (verified 2026-05-12 via the GoG xml MEMBER offsets +
    //     Ghidra decomp of the panel's OnUpArrowPressed @0x006927b0 and
    //     OnDownArrowPressed @0x006927c0 which both dispatch
    //     CSWGuiPanel::HandleInputEvent(0x31/0x32, 1) → the InGameMap
    //     override routes 0x31 → CSWGuiMapHider::GetPrevMapNote and
    //     0x32 → CSWGuiMapHider::GetNextMapNote, cycling the engine's
    //     filtered list of explored map-note waypoints).
    //
    //     Both buttons share vtable 0x0073E658 (CSWGuiButton) with empty
    //     text and no strref — the speculative button-spec read at 9 hits
    //     "miss" on both. Identify them by panel-base offset (analogous
    //     to InGameEquip identifying slot buttons by .gui ID). The other
    //     three buttons (return / partyselect / exit) have real text and
    //     never reach this fallback.
    if (!source && ownerForPerkind &&
        IdentifyPanel(ownerForPerkind) == PanelKind::InGameMap) {
        constexpr uintptr_t kInGameMapUpButtonOffset   = 0xab0;
        constexpr uintptr_t kInGameMapDownButtonOffset = 0xc74;
        uintptr_t panelBase = reinterpret_cast<uintptr_t>(ownerForPerkind);
        uintptr_t ctrl      = reinterpret_cast<uintptr_t>(control);
        acc::strings::Id sid = acc::strings::Id::Count_;
        const char* tag = nullptr;
        if (ctrl == panelBase + kInGameMapUpButtonOffset) {
            sid = acc::strings::Id::MapPrevNote;
            tag = "MapPrevNote";
        } else if (ctrl == panelBase + kInGameMapDownButtonOffset) {
            sid = acc::strings::Id::MapNextNote;
            tag = "MapNextNote";
        }
        if (sid != acc::strings::Id::Count_) {
            const char* lit = acc::strings::Get(sid);
            size_t llen = strlen(lit);
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, lit, llen + 1);
                source = "perkind-map";
                acclog::Write("Menus.PerKind", "InGameMap control=%p kind=%s -> \"%s\"",
                              control, tag, outBuf);
            }
        }
    }

    // 9b3. Per-kind label fallback for the workbench upgrade panel
    //     (upgrade.gui). The 7 BTN_UPGRADE3X/4X slot buttons at .gui IDs
    //     12..18 have empty inline text — their visual content is the
    //     installed mod's icon + name set programmatically by the engine
    //     when an item is committed via BTN_ASSEMBLE. Synthesise a
    //     speakable "Aufwertungssteckplatz N" / "Kristall-Steckplatz N"
    //     label so the user can tell which slot is focused.
    //
    //     IDs 12..14 → weapon upgrade slots (slot index 1..3, matching the
    //                  BTN_UPGRADE31/32/33 tag suffix).
    //     IDs 15..18 → lightsaber crystal slots (slot index 1..4, matching
    //                  the BTN_UPGRADE41/42/43/44 tag suffix).
    //
    //     A future enrichment could read LBL_SLOTNAME (ID 20) for the
    //     *currently active* slot's category word ("Vibrationszelle",
    //     "Skopus", …) — but that label only reflects the focused slot,
    //     so it doesn't help with announcing the other six slot buttons.
    //     For now the synthesised label keeps the user oriented without
    //     promising more semantic info than we can reliably deliver.
    if (!source && ownerForPerkind &&
        IdentifyPanel(ownerForPerkind) == PanelKind::WorkbenchUpgrade) {
        int cid = -1;
        __try {
            cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(control) + 0x50);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cid = -1;
        }
        acc::strings::Id sid = acc::strings::Id::Count_;
        const char* tag = nullptr;
        switch (cid) {
            case 12: sid = acc::strings::Id::WorkbenchSlotWeapon1;       tag = "BTN_UPGRADE31"; break;
            case 13: sid = acc::strings::Id::WorkbenchSlotWeapon2;       tag = "BTN_UPGRADE32"; break;
            case 14: sid = acc::strings::Id::WorkbenchSlotWeapon3;       tag = "BTN_UPGRADE33"; break;
            case 15: sid = acc::strings::Id::WorkbenchSlotSaberCrystal1; tag = "BTN_UPGRADE41"; break;
            case 16: sid = acc::strings::Id::WorkbenchSlotSaberCrystal2; tag = "BTN_UPGRADE42"; break;
            case 17: sid = acc::strings::Id::WorkbenchSlotSaberCrystal3; tag = "BTN_UPGRADE43"; break;
            case 18: sid = acc::strings::Id::WorkbenchSlotSaberCrystal4; tag = "BTN_UPGRADE44"; break;
            default: break;
        }
        if (sid != acc::strings::Id::Count_) {
            const char* lit = acc::strings::Get(sid);
            size_t llen = strlen(lit);
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, lit, llen + 1);
                source = "perkind-workbench-slot";
                acclog::Write("Menus.PerKind",
                              "WorkbenchUpgrade control=%p id=%d tag=%s -> \"%s\"",
                              control, cid, tag, outBuf);
            }
        }
    }

    // 9c. Per-kind label override for the chargen class-selection panel
    //     (CSWGuiClassSelection, vtable=0x00758020). Panel hosts 6
    //     image-only class-icon buttons (vtable=0x0073E658, empty text)
    //     in class_selections[6]; the engine maintains the currently-
    //     hovered class name in the parent's class_label, which it
    //     updates via OnEnterButton.
    //
    //     Two timing hazards we work around:
    //
    //     1. STALE READ on focus event. SetActiveControl fires before the
    //        engine's OnEnterButton has updated class_label, so reading
    //        class_label at chain-step / SetActive time returns the
    //        PREVIOUSLY focused icon's class. The chain step's
    //        AnnounceControl would then speak "previous icon's class" —
    //        an audible echo of the prior selection.
    //
    //     2. ECHO REVERT. After the engine briefly updates class_label
    //        for the user's target icon, an internal bounce / re-select
    //        chain (engine sets active_control back to a different
    //        "selected" icon) reverts class_label to that other icon's
    //        class. The per-frame focused-control monitor catches the
    //        revert and speaks it as a phantom "echo of an earlier
    //        navigated entry."
    //
    //     The fix: cache (icon → class_text) lazily — only populate when
    //     active_control == icon AND class_label is non-empty AND the
    //     nav-suppress budget has settled. Once cached, lock the entry:
    //     subsequent extracts return the cached text regardless of the
    //     engine's transient class_label state, so the revert can't fire
    //     a phantom utterance. Combine with chain-step gating in the
    //     navigation handler (skip AnnounceControl for class icons; let
    //     the per-frame monitor speak when the cache is populated) to
    //     also kill the stale-read echo.
    //
    //     Detection is positional: CSWGuiClassSelection isn't a CGuiInGame
    //     slot (it's a top-level chargen panel), so IdentifyPanel never
    //     resolves it. We check the panel's vtable directly and confirm
    //     the focused control pointer lands on a multiple-of-0x25c offset
    //     inside the class_selections[] array.
    if (!source && IsClassSelectionIcon(ownerForPerkind, control)) {
        const char* cached = ClassLabelCacheLookup(ownerForPerkind, control);
        if (cached && cached[0] != '\0') {
            size_t clen = strnlen(cached, 64);
            if (clen + 1 <= bufSize) {
                memcpy(outBuf, cached, clen + 1);
                source = "perkind-classsel";
            }
        } else {
            // Cache miss. Populate only when active_control == icon AND
            // class_label is non-empty. CSWGuiClassSelection runs
            // OnEnterButton synchronously inside its SetActiveControl
            // override, so by the time active_control == icon is
            // observable, class_label has already been updated to the
            // matching class — this gives us the icon's CORRECT class
            // text in a single read. First-write-wins locking on the
            // cache then keeps subsequent reads stable, immune to the
            // engine's later bounces / class_label reverts.
            void* activeCtrl = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(ownerForPerkind) +
                kPanelActiveControlOffset);
            if (activeCtrl == control) {
                void* classLabel =
                    reinterpret_cast<unsigned char*>(ownerForPerkind) +
                    kClassSelectionClassLabelOffset;
                char text[256];
                if (ExtractTextOrStrRefIndirect(classLabel,
                                                kLabelTextOffset,
                                                kLabelStrRefOffset,
                                                kLabelTextObjectOffset,
                                                text, sizeof(text)) &&
                    text[0] != '\0') {
                    size_t tlen = strnlen(text, sizeof(text));
                    if (tlen + 1 <= bufSize) {
                        ClassLabelCacheStore(ownerForPerkind, control, text);
                        memcpy(outBuf, text, tlen + 1);
                        source = "perkind-classsel";
                        acclog::Write("Menus.PerKind",
                                      "ClassSelection cache+speak control=%p "
                                      "-> \"%s\"", control, outBuf);
                    }
                }
            }
        }
    }

    // 9d. Per-kind value-display for the chargen portrait-selection panel
    //     (CSWGuiPortraitCharGen, vtable=0x00759ea8). The two arrow buttons
    //     (image-only, empty inline text) cycle the portrait but have no
    //     own readable label — the visible "value" between them is a 3D
    //     head scene. We anchor the chain on the LEFT arrow only (the
    //     RIGHT arrow gets filtered out in RebindChain) and announce just
    //     the current portrait value off the panel here, so the cycle UX
    //     mirrors a normal `[◀] value [▶]` widget: one chain entry, Left
    //     and Right cycle, focus on it reads the value.
    //
    //     The RIGHT arrow is intentionally returned without text — that's
    //     what FindAdjacentArrow keys on (it skips controls that have
    //     announceable text), so Left/Right routing in menus.cpp can find
    //     the right_arrow as the cycle neighbour.
    //
    //     Live cycle state lives on the chargen creature: UpdatePortrait-
    //     Button (0x006f8ad0) writes the new resref into CSWCObject.portrait
    //     (+0xa8, inline CSWPortrait = CResRef = char[16]) on every cycle.
    //     Reading those 16 bytes and parsing the `po_p[mf]h[abc]\d` pattern
    //     gives us a localised description (e.g. "weiblich asiatisch 3").
    //     The per-frame focused-control monitor re-extracts every tick —
    //     when the resref changes, the composed string changes and the
    //     diff fires speech.
    //
    //     Fallback ladder for the value: portrait_label.gui_string
    //     (defensive — engine doesn't populate it today) → resref-pattern
    //     parse → raw resref (non-PC / modded portraits) → numeric
    //     portrait_id+1 (last resort, stable but uninformative).
    if (!source && ownerForPerkind) {
        void** ownerVt = *reinterpret_cast<void***>(ownerForPerkind);
        if (reinterpret_cast<uintptr_t>(ownerVt) ==
                kVtableCSWGuiPortraitCharGen) {
            auto* panelBase = reinterpret_cast<unsigned char*>(ownerForPerkind);
            auto* ctrlBase  = reinterpret_cast<unsigned char*>(control);
            ptrdiff_t off   = ctrlBase - panelBase;

            // Only the left_arrow is the value-display anchor. The
            // right_arrow returns nullptr from FromControl so it (a) gets
            // skipped by the chain rebind's text-presence check downstream,
            // and (b) is discoverable by FindAdjacentArrow as a text-less
            // cycle neighbour.
            if (off == (ptrdiff_t)kPortraitLeftArrowOffset) {
                // Try the panel's portrait_label first — engine doesn't
                // populate it today but we don't lose anything by checking.
                void* portraitLabel = panelBase + kPortraitLabelOffset;
                char portraitText[256];
                bool gotLabel = ExtractTextOrStrRefIndirect(
                    portraitLabel,
                    kLabelTextOffset,
                    kLabelStrRefOffset,
                    kLabelTextObjectOffset,
                    portraitText, sizeof(portraitText));

                // Get the live portrait state from panel.creature via the
                // engine accessors. Direct field reads on the creature
                // (+0xa8 CSWCObject.portrait, +0x14/+0x24 CSWCPlayer-style)
                // and on the panel (+0x1238 portrait_id) all stay zero
                // through cycling — only the engine accessors return live
                // values (verified 2026-05-09 in patch-20260509-053256.log).
                //
                // GetPortrait fills a CResRef (16-byte char[]) with the
                // baseresref string for the currently-selected portrait,
                // regardless of the underlying storage location. This is
                // our preferred source — works for any portrait id the
                // engine cycles through, including rows our static
                // kPortraitByRow table is unsure about (e.g. id=32 turned
                // out to be a male PC portrait, not po_pbastila as the
                // data-section ordering had suggested).
                //
                // GetPortraitId is logged alongside as diagnostic context;
                // kPortraitByRow remains a defensive fallback for the
                // (theoretical) case where GetPortrait fails.
                uint32_t portraitId = 0xFFFFFFFFu;
                char liveResref[kResRefSize + 1] = {0};
                __try {
                    void* creature = *reinterpret_cast<void**>(
                        panelBase + kPortraitCharGenCreatureOffset);
                    if (creature) {
                        auto getPid = reinterpret_cast<
                            PFN_CSWCCreatureGetPortraitId>(
                                kAddrCSWCCreatureGetPortraitId);
                        portraitId = getPid(creature) & 0xFFFFu;

                        auto getP = reinterpret_cast<
                            PFN_CSWCCreatureGetPortrait>(
                                kAddrCSWCCreatureGetPortrait);
                        char tmp[kResRefSize] = {0};
                        getP(creature, tmp, /*side=*/0);
                        memcpy(liveResref, tmp, kResRefSize);
                        liveResref[kResRefSize] = '\0';
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    portraitId  = 0xFFFFFFFFu;
                    liveResref[0] = '\0';
                }

                // Pick the source: live resref from engine if non-empty,
                // else the static row → baseresref table for the rows we
                // know are correct.
                const char* mappedResref = nullptr;
                if (liveResref[0] != '\0') {
                    mappedResref = liveResref;
                } else if (portraitId < 32 && kPortraitByRow[portraitId]) {
                    mappedResref = kPortraitByRow[portraitId];
                }

                // Parse the regular chargen pattern: "po_p[mf]h[abc]\d".
                // Index map:
                //   [0..3] = "po_p"
                //   [4]    = gender ('m'|'f')
                //   [5]    = 'h'
                //   [6]    = race code ('a'|'b'|'c')
                //   [7]    = variant digit '1'..'5'
                char description[128] = {0};
                bool parsedPattern = false;
                if (mappedResref &&
                    mappedResref[0] == 'p' && mappedResref[1] == 'o' &&
                    mappedResref[2] == '_' && mappedResref[3] == 'p' &&
                    mappedResref[5] == 'h' &&
                    (mappedResref[4] == 'm' || mappedResref[4] == 'f') &&
                    (mappedResref[6] == 'a' || mappedResref[6] == 'b' ||
                     mappedResref[6] == 'c') &&
                    mappedResref[7] >= '0' && mappedResref[7] <= '9') {
                    auto genderId = (mappedResref[4] == 'f')
                        ? acc::strings::Id::PortraitGenderFemale
                        : acc::strings::Id::PortraitGenderMale;
                    acc::strings::Id raceId = acc::strings::Id::Count_;
                    switch (mappedResref[6]) {
                        case 'a': raceId = acc::strings::Id::PortraitRaceAsian; break;
                        case 'b': raceId = acc::strings::Id::PortraitRaceDark;  break;
                        case 'c': raceId = acc::strings::Id::PortraitRaceLight; break;
                    }
                    if (raceId != acc::strings::Id::Count_) {
                        snprintf(description, sizeof(description),
                                 acc::strings::Get(
                                     acc::strings::Id::FmtPortraitDescription),
                                 acc::strings::Get(genderId),
                                 acc::strings::Get(raceId),
                                 (int)(mappedResref[7] - '0'));
                        parsedPattern = true;
                    }
                }

                const char* label =
                    acc::strings::Get(acc::strings::Id::PortraitLabel);
                const char* fmtArrow =
                    acc::strings::Get(acc::strings::Id::FmtPortraitArrow);
                if (gotLabel && portraitText[0] != '\0') {
                    snprintf(outBuf, bufSize, fmtArrow,
                             label, portraitText);
                } else if (parsedPattern) {
                    snprintf(outBuf, bufSize, fmtArrow,
                             label, description);
                } else if (mappedResref) {
                    snprintf(outBuf, bufSize, fmtArrow,
                             label, mappedResref);
                } else if (portraitId != 0xFFFFFFFFu) {
                    snprintf(outBuf, bufSize,
                             acc::strings::Get(
                                 acc::strings::Id::FmtPortraitArrowId),
                             label, (int)(portraitId + 1));
                } else {
                    // Couldn't even read GetPortraitId — fall back to
                    // the panel's stale portrait_id field as a last
                    // resort so the chain entry isn't text-less.
                    uint32_t fallbackId =
                        ReadU32(ownerForPerkind, kPortraitIdOffset);
                    snprintf(outBuf, bufSize,
                             acc::strings::Get(
                                 acc::strings::Id::FmtPortraitArrowId),
                             label, (int)(fallbackId + 1));
                }
                if (outBuf[0] != '\0') {
                    source = "perkind-portrait";
                    acclog::Write("Menus.PerKind",
                                  "PortraitCharGen control=%p anchor=left "
                                  "id=%u resref=%s -> \"%s\"",
                                  control, portraitId,
                                  mappedResref ? mappedResref : "<null>",
                                  outBuf);
                }
            }
        }
    }

    // 9e. Per-kind label fallback for the character sheet (CSWGuiInGameCharacter).
    //     The bottom-row icon strip has 4 unlabeled CSWGuiButtons:
    //       btn_change1  (GUI id 64) — portrait crossfade slot A (decorative)
    //       btn_charright(GUI id 65) — next-party-member arrow → OnSwitchRight
    //       btn_charleft (GUI id 66) — prev-party-member arrow → OnSwitchLeft
    //       btn_change2  (GUI id 67) — portrait crossfade slot B (decorative)
    //
    //     btn_change1/btn_change2 are decorative animation slots — the
    //     OnSwitch handlers mutate them as the portrait transitions, the
    //     user doesn't click them. They're suppressed from the chain
    //     entirely in RebindChain via IsDecorativeForChain; that filter
    //     lives there so navigation skips them outright. Here we only
    //     wire the readable names for the two arrow buttons the user
    //     actually focuses (btn_charleft / btn_charright).
    //
    //     IDs come from the panel walk's `Tolk.spoke: control 64..67`
    //     fallback line in patch-20260514-150455.log + the four-step
    //     mapping above (struct-order in swkotor.exe.h:9838-9841 against
    //     ascending CSWGuiButton instance addresses in the panel walk).
    if (!source && ownerForPerkind &&
        IdentifyPanel(ownerForPerkind) == PanelKind::InGameCharacter) {
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(control) + 0x50);
        acc::strings::Id sid = acc::strings::Id::Count_;
        const char* tag = nullptr;
        if (cid == 66) {
            sid = acc::strings::Id::CharSwitchPrev;
            tag = "btn_charleft";
        } else if (cid == 65) {
            sid = acc::strings::Id::CharSwitchNext;
            tag = "btn_charright";
        }
        if (sid != acc::strings::Id::Count_) {
            const char* lit = acc::strings::Get(sid);
            size_t llen = strlen(lit);
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, lit, llen + 1);
                source = "perkind-charsheet";
                acclog::Write("Menus.PerKind",
                              "InGameCharacter control=%p id=%d kind=%s -> \"%s\"",
                              control, cid, tag, outBuf);
            }
        }
        // Alignment slider is now exposed via the virtual stat-row chain
        // (StatRowKind::Slider in menus_charsheet's spec table). Section 0
        // routes through ExtractStatRow for it, so no separate handler
        // here.
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
    //
    //    Cycle flanker arrows (chargen ± and Difficulty-style left/right)
    //    are suppressed here so the cycle-mechanism's empty-text predicate
    //    engages. Without the suppression the nearest plain label would
    //    paint both flankers (e.g. "Stärke" for the + and the -),
    //    defeating both the chain squash and FindAdjacentArrow.
    if (!source && g_currentPanel && IsChainNavigable(control) &&
        !IsCycleFlankerArrow(g_currentPanel, control)) {
        char label[256];
        if (FindSiblingLabel(g_currentPanel, control,
                             label, sizeof(label))) {
            size_t llen = strnlen(label, sizeof(label));
            if (llen > 0 && llen + 1 <= bufSize) {
                memcpy(outBuf, label, llen + 1);
                source = "siblinglabel-fallback";
                // Trace: fired for every chain entry on every arrow press,
                // resolves to the same (control, label) tuple in tight bursts.
                acclog::Trace("Menus.SiblingFallback",
                              "control=%p label=\"%s\"", control, outBuf);
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

    // Append "nicht verfügbar" / "unavailable" suffix when the focused
    // button is engine-disabled. The signal is bit 1 (mask 0x2) of
    // CSWGuiControl.bit_flags (+0x44). Empirically the only bit that
    // splits "click-dispatch fires" from "click is silently dropped"
    // across every observed control:
    //
    //   bit 0 (0x1) — selection state (CSWGuiControl::SetActive)
    //   bit 1 (0x2) — interactive / accepts click (this gate)
    //   bit 2 (0x4) — visible           (CSWGuiPanel::SetVisible)
    //   bit 3 (0x8) — enabled flag      (CSWGuiControl::SetEnabled)
    //
    // SetEnabled toggles bit 3 but the actual runtime click gate uses
    // bit 1: CSWGuiLevelUpPanel's step buttons have bit_flags=0x6 when
    // enabled and 0x4 when disabled, bit 3 stays cleared on all of them
    // (SetEnabled is never called). Test corpus:
    //   working: Talente 0x6, Annehmen 0x6, Levelaufst 0xa, Spiel laden
    //            0x10e, Schliess 0xa, Charakter-arrows 0xa, Auto-Pause 0xe
    //   disabled: Attribute 0x4, Kräfte 0x4
    //
    // Edge case — Zurück on LevelUp panel has bit_flags=0x2 (bit 1 set,
    // bit 2 clear). The engine routes its click but CSWGuiLevelUpPanel::
    // OnCancelPressed @0x006ee5f0 gates internally on `field16_0x1ce8`
    // and no-ops until allocations are pending. We can't detect that
    // internal gate from the control's bit_flags, so Zurück labels as
    // "enabled" by this check even when it's effectively a no-op.
    //
    // Earlier attempts used (a) is_active (the idle-vs-active selection
    // state, not enabled/disabled), (b) bit 3 (set only when SetEnabled
    // is called, miss on Talente/Annehmen), and (c) GetIsSelectable
    // (gui_object path produces false on enabled step buttons). All three
    // were wrong on different button sets; bit 1 matches the actual
    // engine dispatch outcome.
    //
    // Skipped for toggles — their ", ein" / ", aus" suffix already
    // disambiguates state. Gated on IsChainNavigable so labels don't get
    // the suffix.
    if (source && !IsToggle(control) && IsChainNavigable(control)) {
        uint32_t bitFlags = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(control) + 0x44);
        if ((bitFlags & 0x2) == 0) {
            const char* suffix = acc::strings::Get(
                acc::strings::Id::DisabledSuffix);
            size_t len = strnlen(outBuf, bufSize);
            size_t suffixLen = strlen(suffix);
            if (suffixLen > 0 && len + suffixLen + 1 <= bufSize) {
                memcpy(outBuf + len, suffix, suffixLen + 1);
            }
        }
    }

    return source;
}

}  // namespace acc::menus::extract
