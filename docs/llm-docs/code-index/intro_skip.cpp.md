# intro_skip.cpp (124 lines)

Runtime toggle for the launch-time intro movies (biologo/leclogo/legal.bik),
mirroring installer/IntroMovieDisabler.cs. State lives purely in the
filesystem: renaming each file to `<name>.bik.disabled` skips it on next
launch (the engine only plays movies at startup, so this can't affect the
current session). `GetMoviesDir` resolves `<install>/Movies` from
`GetModuleFileNameA(nullptr)` so it works regardless of Steam/GoG/custom
install path. All three files are kept in lockstep by `SetDisabled`.

## Declarations (in source order)

- L16 — `const char* const kIntroFiles[] = {"biologo.bik","leclogo.bik","legal.bik"}`
  note: mirror of installer/IntroMovieDisabler.cs::IntroFiles — keep both lists in sync
- L22 — `constexpr const char* kDisabledSuffix = ".disabled"`
- L28 — `bool GetMoviesDir(char* out, size_t outSize)` — strips the exe filename from GetModuleFileNameA, appends "\\Movies"
- L43 — `bool FileExists(const char* path)`
- L51 — `State CurrentState()` — probes biologo.bik only as representative of all three
- L73 — `bool SetDisabled(bool disable)`
  note: per-file rename via MoveFileExA with REPLACE_EXISTING; no-ops files already in the target state; logs renamed/noop/ok counts
