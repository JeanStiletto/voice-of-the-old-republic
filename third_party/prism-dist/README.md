# Prism (prebuilt)

The speech library the mod talks to. Upstream: <https://github.com/ethindp/prism>, MPL-2.0.

- `x86/prism.dll`, `x86/prism.lib` — the 32-bit build the mod ships (`kdev apply` copies the DLL
  into the game, `installer/release.ps1` bundles it).
- `include/prism.h`, `include/prism_version.h` — the C ABI. `patches/Accessibility/prism.cpp`
  resolves the functions it uses at runtime and documents the subset it relies on.

Version: **0.18.3** plus local patches, built 2026-11-14.

## Source and patches

The source is not in this repository. It lives in a shared checkout next to it, `Dev\prism`: a
clone of ethindp/prism, branch `arena-patched` = the release tag plus local commits. Accessible
Arena builds its 64-bit DLL from the same checkout; each project keeps its own copy of the built
DLL, so updating one never changes the other.

Local commits on top of 0.18.3:

1. SEH-guarded `initialize()` (`source/backend.h`, `seh_guarded_initialize`): a vendor DLL that
   faults during start-up is reported as unavailable instead of crashing the game
   (docs/upstream-prs.md PR-7). C# hosts cannot catch this, and neither can this mod's wrapper
   once the fault comes from inside Prism's own registry walk.
2. `CMAKE_C_STANDARD` pinned to 17: this MSVC/CMake pair rejects C23. Build environment only.
3. Cherry-pick of upstream #181 (ZDSR's `Braille` export optional), until a release contains it.

The former runtime-loading patch for ZDSR / PC-Talker / BoyPC (PR-10) is upstream since 0.18.3.

## Rebuilding

1. In `Dev\prism`: `git fetch --tags`, then for a new release
   `git rebase --onto <new tag> <old tag> arena-patched`.
2. Run `third_party\build-prism-x86.bat` (optionally with the checkout path).
3. Copy `<prism>\build_kotor_x86\prism.dll` and `prism.lib` into `x86\`, `include\prism.h`
   and `build_kotor_x86\generated\include\prism_version.h` into `include\`.
4. Check before shipping:
   - `dumpbin /dependents x86\prism.dll` lists no `ZDSRAPI.dll`, `byctrl.dll`, `PCTKUSR.dll`,
     `vcruntime140.dll` or `msvcp140.dll`
   - `dumpbin /exports` covers every `prism_*` name `patches/Accessibility/prism.cpp` resolves
   - the probe script from Accessible Arena (`third_party\prism\probe-prism.ps1`) run under the
     32-bit PowerShell (`C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`) with
     `-DllPath` pointing at the new DLL finishes cleanly
