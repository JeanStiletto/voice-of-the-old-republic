// Persistent mod-settings store.
//
// A tiny `key=value` text file at <install>\acc_settings.ini (next to the
// logs\ dir), loaded lazily on first access and rewritten in full on every
// Set (the file is a handful of lines, so a full rewrite is trivial and
// keeps the on-disk state always consistent).
//
// This exists because the Mod-Einstellungen toggles and the cue-volume
// slider are otherwise in-memory only — they reset to their defaults every
// launch. Owners (menus_modsettings for the toggles, audio_bus for the cue
// volume) pull their persisted value on first use and push back here on
// change; this module is just typed get/set over the file.
//
// Defaults: a missing key (or a missing/unreadable file) returns the caller-
// supplied default, so first-run behaviour is unchanged. Thread-safe (one
// mutex); accesses are main-thread in practice but cheap to guard.

#pragma once

namespace acc::settings {

bool GetBool(const char* key, bool defValue);
int  GetInt (const char* key, int  defValue);

void SetBool(const char* key, bool value);
void SetInt (const char* key, int  value);

}  // namespace acc::settings
