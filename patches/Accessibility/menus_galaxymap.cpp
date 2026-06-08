#include "menus_galaxymap.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_input.h"     // kInput* logical codes
#include "engine_manager.h"   // kAddrGuiManagerPtr, kMgrPanels{Data,Size}Offset
#include "engine_offsets.h"   // kLabelGuiStringPtrOffset
#include "engine_panels.h"    // IdentifyPanel / PanelKind / PanelKindName
#include "engine_reads.h"     // ReadGuiString
#include "log.h"
#include "menus_pending.h"    // QueueGalaxyInput
#include "prism.h"
#include "strings.h"

using namespace acc::engine;

namespace acc::menus::galaxymap {

namespace {

// CSWGuiInGameGalaxyMap field offsets (Lane's gzf struct, SIZE 0x2550).
constexpr size_t kPlanetNameLabelOffset  = 0x1ca4;  // LBL_PLANETNAME (CSWGuiLabel)
constexpr size_t kDescriptionLabelOffset = 0x1de4;  // LBL_DESC       (CSWGuiLabel)

// CSWGuiInGameGalaxyMap::HandleInputEvent @0x00695980. Its switch maps:
//   0x27 / 0x2d → accept (run k_sup_galaxymap, HideGalaxyMapGui)  [travel]
//   0x28 / 0x2e / 0xdf → cancel (HideGalaxyMapGui)                [back]
//   0x2f / 0x31 / 0x3d / 0x3f → PrevPlanet
//   0x30 / 0x32 / 0x3e / 0x40 → NextPlanet
// NextPlanet/PrevPlanet iterate to the next CSWPartyTable planet that is both
// GetPlanetAvailable() and GetPlanetSelectable(), so hidden / unreachable
// planets are skipped for us. Engine codes we feed it:
constexpr int kEngineAccept   = 0x27;
constexpr int kEngineCancel   = 0x28;
constexpr int kEnginePrev     = 0x2f;
constexpr int kEngineNext     = 0x30;

// Read a CSWGuiLabel's rendered text (gui_string) at the given panel-relative
// offset into outBuf. Returns true on non-empty text.
bool ReadLabel(void* panel, size_t labelOffset, char* outBuf, size_t bufSize) {
    if (!panel || !outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    void* label = reinterpret_cast<unsigned char*>(panel) + labelOffset;
    bool ok = false;
    __try {
        ok = ReadGuiString(label, kLabelGuiStringPtrOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return ok && outBuf[0] != '\0';
}

void AnnouncePlanetName(void* panel, bool interrupt) {
    char name[256];
    if (ReadLabel(panel, kPlanetNameLabelOffset, name, sizeof(name))) {
        prism::Speak(name, interrupt);
        acclog::Write("GalaxyMap", "planet name=\"%s\"", name);
    } else {
        acclog::Write("GalaxyMap", "planet name empty (panel=%p)", panel);
    }
}

// First-sight tracking: single slot — the galaxy map is a singleton panel.
void* s_announcedPanel = nullptr;

void* FindGalaxyMapInPanels() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int    count = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** data  = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!data || count <= 0) return nullptr;
    int n = count > 16 ? 16 : count;
    for (int i = 0; i < n; ++i) {
        void* p = data[i];
        if (p && IdentifyPanel(p) == PanelKind::InGameGalaxyMap) return p;
    }
    return nullptr;
}

}  // namespace

bool IsGalaxyMapPanel(void* panel) {
    return panel && IdentifyPanel(panel) == PanelKind::InGameGalaxyMap;
}

bool TryHandleInput(void* activePanel, int param_1, int param_2, int& rv) {
    if (!IsGalaxyMapPanel(activePanel)) return false;

    const bool isUp    = (param_1 == kInputNavUp);
    const bool isDown  = (param_1 == kInputNavDown);
    const bool isEnter = (param_1 == kInputEnter1 || param_1 == kInputEnter2);
    const bool isEsc   = (param_1 == kInputEsc1   || param_1 == kInputEsc2);
    const bool isOtherNav =
        (param_1 == kInputNavLeft || param_1 == kInputNavRight ||
         param_1 == kInputHome    || param_1 == kInputEnd);

    if (!isUp && !isDown && !isEnter && !isEsc && !isOtherNav) {
        return false;  // letters / function keys pass through to the engine
    }

    // Own the key on both edges so the generic chain never walks the unnamed
    // planet buttons (or the engine double-acts on the release).
    rv = 1;
    if (param_2 == 0) return true;  // release: consumed, no action

    if (acc::menus::pending::IsPending()) {
        acclog::Write("GalaxyMap", "op already pending; ignoring key=%d", param_1);
        return true;
    }

    if (isUp) {
        acc::menus::pending::QueueGalaxyInput(activePanel, kEnginePrev,
                                              /*announce=*/true);
        acclog::Write("GalaxyMap", "prev planet (panel=%p)", activePanel);
    } else if (isDown) {
        acc::menus::pending::QueueGalaxyInput(activePanel, kEngineNext,
                                              /*announce=*/true);
        acclog::Write("GalaxyMap", "next planet (panel=%p)", activePanel);
    } else if (isEnter) {
        // Travel — clear our first-sight latch now so reopening the map (after
        // arriving / aborting) re-announces.
        s_announcedPanel = nullptr;
        acc::menus::pending::QueueGalaxyInput(activePanel, kEngineAccept,
                                              /*announce=*/false);
        acclog::Write("GalaxyMap", "accept/travel (panel=%p)", activePanel);
    } else if (isEsc) {
        s_announcedPanel = nullptr;
        acc::menus::pending::QueueGalaxyInput(activePanel, kEngineCancel,
                                              /*announce=*/false);
        acclog::Write("GalaxyMap", "cancel/back (panel=%p)", activePanel);
    } else {
        // Left/Right/Home/End: consumed as no-ops (Up/Down is the planet
        // axis). Re-announce the current planet so the key isn't silent.
        AnnouncePlanetName(activePanel, /*interrupt=*/true);
    }
    return true;
}

bool SpeakDescription(void* panel) {
    if (!IsGalaxyMapPanel(panel)) return false;
    char desc[4096];
    if (ReadLabel(panel, kDescriptionLabelOffset, desc, sizeof(desc))) {
        prism::Speak(desc, /*interrupt=*/true);
        acclog::Write("GalaxyMap", "description=\"%s\"", desc);
    } else {
        acclog::Write("GalaxyMap", "description empty (panel=%p)", panel);
    }
    return true;  // consume the Shift+Down regardless (predictable behaviour)
}

void DispatchInput(void* panel, int engineCode, bool announcePlanet) {
    if (!panel) return;
    // CSWGuiInGameGalaxyMap::HandleInputEvent(this, code, state). Calling by
    // address (not vtable index) — panel kind is already confirmed by the
    // caller, so this is unambiguous and avoids a panel-vs-control vtable
    // index mismatch.
    typedef void(__thiscall* PFN_GalaxyHandleInput)(void* this_, int code,
                                                    int state);
    constexpr std::uintptr_t kAddrGalaxyHandleInput = 0x00695980;
    auto fn = reinterpret_cast<PFN_GalaxyHandleInput>(kAddrGalaxyHandleInput);
    __try {
        fn(panel, engineCode, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("GalaxyMap", "DispatchInput SEH panel=%p code=0x%x",
                      panel, engineCode);
        return;
    }
    // NextPlanet/PrevPlanet update LBL_PLANETNAME synchronously via
    // DisplayPlanet, so the label is current by the time we read it here.
    if (announcePlanet) {
        AnnouncePlanetName(panel, /*interrupt=*/true);
    }
}

void Tick() {
    void* panel = FindGalaxyMapInPanels();
    if (!panel) {
        // Map closed — drop the latch so the next open re-announces.
        if (s_announcedPanel) {
            acclog::Write("GalaxyMap", "panel gone; clearing first-sight latch");
            s_announcedPanel = nullptr;
        }
        return;
    }
    if (panel == s_announcedPanel) return;  // already announced this instance
    s_announcedPanel = panel;

    // Title + current planet (the engine auto-selects the first
    // available+selectable planet in OnPanelAdded before we see the panel).
    prism::Speak(acc::strings::Get(acc::strings::Id::GalaxyMapTitle),
                 /*interrupt=*/false);
    acclog::Write("GalaxyMap", "first-sight panel=%p kind=%s", panel,
                  PanelKindName(IdentifyPanel(panel)));
    AnnouncePlanetName(panel, /*interrupt=*/false);
}

}  // namespace acc::menus::galaxymap
