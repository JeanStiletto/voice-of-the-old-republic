# update_checker.cpp (994 lines)

Implements the auto-updater as an atomic-flag state machine driven by two
worker threads (version check, download) polled from the main thread's
Tick(). Version lookup goes through TWO paths: PRIMARY reads the
Location header of github.com's non-API `/releases/latest` redirect
(doesn't count against GitHub's 60/hr unauthenticated rate limit);
FALLBACK hits the rate-limited api.github.com REST endpoint only if the
redirect fails (covers partial GitHub outages where the redirect host
504s). Download mirrors that split: PRIMARY is a direct
`releases/download/<tag>/<asset>` URL; FALLBACK re-extracts the asset's
API url from the (possibly cached) release JSON. JSON parsing is
hand-rolled string search (no `<regex>`, keeps the DLL slim). Two opt-in
file-based test overrides (`update_test_version.txt`,
`update_test_installer.txt` in the patch dir) let the whole flow be
exercised locally without touching GitHub. On download success writes a
self-deleting handoff .bat (waits for swkotor.exe exit, `start /wait`s
the manifest-elevated installer with `--auto-update`, relaunches via the
Steam protocol so saves land on the right profile) and calls
ExitProcess. Talks to: engine_player (GetPlayerPosition gate), hotkeys
(F5 Action::CheckForUpdate), mod_version (kModVersion), prism, strings.

## Declarations (in source order)

- L72-88 — GitHub host/path constants (dual web+API host split), kInstallerAsset,
  kSteamAppId="32370"
- L92-97 — timeouts (check 5s, download 60s), kExitGraceMs=2000 (lets the spoken cue finish before ExitProcess)
- L133-166 — dev test-file overrides: `kTestVersionFile`/`kTestInstallerFile` +
  `bool ReadTestFile(filename, out, cap)`
  note: file-based not env-var — Steam's env inheritance is unreliable across setx/relaunch
- L174-201 — atomic state flags (check_started/complete/update_available/announced,
  download_started/complete/failed), g_exit_at_tick, shared string buffers + cached g_release_json
  note: strings written before the flag flips (release semantics); Tick reads with acquire
- L207 — `bool OpenSession(host, session, connection)` — WinHTTP session+connection open
- L237 — `bool HttpGetToString(connection, path, timeoutMs, out)` — GET expecting HTTP 200 body
- L299 — `bool HttpGetRedirectLocation(connection, path, timeoutMs, outLoc, outCap)`
  note: disables auto-redirect so the 302 Location header comes back instead of being silently followed
- L340 — `bool ParseTagFromLocation(loc, out, outCap)` — extracts tag after "/releases/tag/"
- L361 — `bool HttpDownloadUrlToFile(url, destPath, timeoutMs)` — full session/connect/request/stream-to-file
- L467/478 — `SkipColon` / `ReadQuotedString` — hand-rolled JSON value extraction primitives
- L495 — `void StripTagToVersion(rawTag, out, outCap)` — strips leading v/V + "-suffix"
- L509/519 — `bool ExtractRawTagName` / `bool ExtractTagName` — top-level "tag_name" extraction
- L543 — `bool ExtractAssetApiUrl(json, assetName, outUrl, outCap)`
  note: scans back to the matching asset object's opening brace so the uploader sub-object's own "url" field isn't picked up by mistake
- L584-614 — `struct ParsedVersion` + `ParseVersion` + `bool IsRemoteNewer(remote, local)` — 4-part dot version compare
- L620/625 — `void CheckVersionWorker()` / `CheckVersionWorkerImpl()` — test-mode override,
  redirect-then-API tag resolution, IsRemoteNewer gate, flag/string publish
- L713 — `void DownloadWorker()` — test-mode override, direct-URL-then-API-fallback download
- L813 — `bool WriteHandoffBatch(installerPath, batchPathOut, outCap)` — writes the
  wait/elevate/relaunch/self-delete .bat
- L852 — `bool SpawnHandoffBatch(batchPath)`
  note: CREATE_NO_WINDOW only (not DETACHED_PROCESS) — cmd's `start /wait` needs SOME console to function
- L882 — `void LaunchHandoffAndExit()` — writes+spawns the bat, then ExitProcess(0)
- L901 — `void StartBackgroundCheck()` — compare_exchange idempotency guard
- L912 — `void PollF5()` — rising-edge hotkey poll + in-gameplay refusal (UpdateNotInMenu)
- L922 — `void Tick()` — PollF5 + one-shot available-announce + download-completion handling + scheduled exit
- L961 — `void HandleF5()` — in-flight / not-available / kick-download branches
