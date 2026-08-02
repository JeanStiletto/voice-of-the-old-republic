"""Report, per hook, whether KOTOR 2 can actually run it: hook + gate + address.

Why this exists
---------------
A feature needs BOTH halves to work: the hook installed in kotor2.hooks.toml so
the handler is called at all, and its `acc::game::HandlerEnabled()` gate cleared
so the handler does not return immediately. Having one without the other is
worse than having neither, because the feature half-runs and looks like a bug.

That is not hypothetical. The focus handler was gated-clear and hooked while the
per-frame tick was neither, so every focus announcement was queued into the
pending-announce slot and never drained — KOTOR 2 navigated correctly and spoke
nothing, with no defect anywhere. One test round to discover something this
script answers offline.

Columns:
  hook      the KOTOR 1 address in hooks.toml
  handler   the exported function it detours to
  K2 hook   whether kotor2.hooks.toml installs it
  K2 gate   whether the handler still early-returns on KOTOR 2
  verdict   what that combination means

Read the verdicts as a work list: everything a feature's call graph touches must
reach READY before that feature is worth testing on KOTOR 2.

Usage:
    k2_hook_status.py <patch-dir>
"""
import os
import re
import sys


def parse_hooks(path):
    """[(address, function)] in file order."""
    if not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8", errors="replace").read()
    out, addr = [], None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#"):
            continue
        m = re.match(r"address\s*=\s*(0x[0-9a-fA-F]+)", s)
        if m:
            addr = m.group(1)
        m = re.match(r'function\s*=\s*"([^"]+)"', s)
        if m:
            out.append((addr or "?", m.group(1)))
            addr = None
    return out


def handler_gates(patch_dir):
    """handler name -> True if its body still calls HandlerEnabled()."""
    gated = {}
    for fn in sorted(os.listdir(patch_dir)):
        if not fn.endswith(".cpp"):
            continue
        lines = open(os.path.join(patch_dir, fn), encoding="utf-8",
                     errors="replace").read().splitlines()
        current = None
        depth = 0
        for line in lines:
            code = re.sub(r"//.*", "", line)
            m = re.search(r'extern\s+"C"\s+\w+\s+__cdecl\s+(\w+)\s*\(', code)
            if m:
                current = m.group(1)
                gated.setdefault(current, False)
                depth = 0
            depth += code.count("{") - code.count("}")
            if current and "HandlerEnabled()" in code:
                gated[current] = True
            if current and depth <= 0 and "}" in code:
                current = None
    return gated


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    patch = sys.argv[1]
    k1 = parse_hooks(os.path.join(patch, "hooks.toml"))
    k2 = {f for _, f in parse_hooks(os.path.join(patch, "kotor2.hooks.toml"))}
    gates = handler_gates(patch)

    # KOTOR 2 reaches some handlers through a shim rather than a direct hook.
    # Record those so the report does not call them missing.
    shims = {
        "OnSetActiveControl": "OnSetActiveControlK2",
        "OnHandleInputEvent": "OnHandleInputEventK2",
        "OnHandleFocusChange": "OnHandleFocusChangeK2",
        "OnListBoxSetActiveControl": "OnListBoxSetActiveControlK2",
        # Batch 2 — in-game GUI lifecycle
        "OnSwitchToSWInGameGui": "OnSwitchToSWInGameGuiK2",
        "OnHideSWInGameGui": "OnHideSWInGameGuiK2",
        "OnSetSWGuiStatus": "OnSetSWGuiStatusK2",
        "OnAppendToMsgBuffer": "OnAppendToMsgBufferK2",
        # Batch 3 — world / area / transitions
        "OnSetMoveToModuleString": "OnSetMoveToModuleStringK2",
        "OnDoorOpen": "OnDoorOpenK2",
        "OnShowObject": "OnShowObjectK2",
        # Batch 3c — interaction (in-world input pipeline)
        "OnClientHandleInputEvent": "OnClientHandleInputEventK2",
        # Batch 4 — combat round + pause state
        "OnCombatRoundAddAction": "OnCombatRoundAddActionK2",
        "OnCombatRoundRemoveAllActions": "OnCombatRoundRemoveAllActionsK2",
        "OnCombatRoundSetCurrentAction": "OnCombatRoundSetCurrentActionK2",
        "OnCombatRoundRemoveLastAction": "OnCombatRoundRemoveLastActionK2",
        "OnSetPauseState": "OnSetPauseStateK2",
    }

    print(f"{'handler':38s} {'K1 addr':12s} {'K2 hook':9s} {'K2 gate':9s} verdict")
    ready = blocked = 0
    for addr, fn in k1:
        hooked = fn in k2 or shims.get(fn) in k2
        gated = gates.get(fn, None)
        if gated is None:
            gatetxt, gateok = "n/a", True
        else:
            gatetxt, gateok = ("GATED", False) if gated else ("clear", True)
        if hooked and gateok:
            verdict, ok = "READY", True
        elif hooked and not gateok:
            verdict, ok = "HOOKED BUT GATED — never runs", False
        elif not hooked and gateok:
            verdict, ok = "gate clear, NO HOOK — never called", False
        else:
            verdict, ok = "off (no hook, gated)", False
        ready += ok
        blocked += not ok
        print(f"{fn:38s} {addr:12s} {'yes' if hooked else 'no':9s} "
              f"{gatetxt:9s} {verdict}")
    print(f"\n# READY: {ready}   not runnable on KOTOR 2: {blocked}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
