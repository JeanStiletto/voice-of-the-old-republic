// Startup snapshot of swkotor.ini and install-root layout. Logged once at
// session start so support bundles always carry a baseline of the user's
// config — no need to chase a follow-up message asking what's in their ini.

#pragma once

namespace acc::diag::settings {

// Walk every [section] / key=value pair in <install>\swkotor.ini and emit
// one log line per entry. Also probes presence of audio/input proxy DLLs
// (dsound, dsoal, dinput8, mss32) and the Override\ file count. Idempotent.
void LogStartupSnapshot();

}  // namespace acc::diag::settings
