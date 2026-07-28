# exports.def (27 lines)

Module-definition file listing the DLL's exported hook entry points that the
patch framework's detours call into (OnRulesInit, input/focus/control-change
handlers, sub-screen switch, combat-round mutators, pause-state, turret/door/
fire events, etc.). Each name here must have a matching `[[hooks]]` entry in
hooks.toml and a matching `extern "C"` definition somewhere in the patch
source — this is the ABI surface the injected DLL presents to the patcher.
