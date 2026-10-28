// Startup snapshot of swkotor.ini and install-root layout. Logged once at
// session start so support bundles always carry a baseline of the user's
// config — no need to chase a follow-up message asking what's in their ini.

#pragma once

namespace acc::diag::settings {

// Walk every [section] / key=value pair in <install>\swkotor.ini and emit
// one log line per entry. Also probes presence of audio/input proxy DLLs
// (dsound, dsoal, dinput8, mss32) and the Override\ file count. Idempotent.
void LogStartupSnapshot();

// Once the game's sound system is up, logs the audio path: where dsound.dll
// came from, which Miles 3D provider the game opened (only DirectSound3D /
// EAX hand 3D sounds to dsoal; Miles' software providers mix them to stereo
// first) and the alsoft.ini HRTF keys. Cheap per tick: returns at once after
// it logged.
//
// dsoal's and OpenAL Soft's own logs can't be switched on from here: both
// read their environment switches in dsound.dll's DllMain, and dsound.dll is
// already loaded when ours runs (2026-10-28). Set DSOAL_LOGLEVEL /
// ALSOFT_LOGLEVEL (+ _LOGFILE) in the shell that launches the game instead.
void LogAudioPathOnce();

}  // namespace acc::diag::settings
