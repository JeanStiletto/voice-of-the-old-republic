# Phase 3 scan — installer core flows and services

Scope (16 files, `installer\KotorAccessibilityInstaller\`): MainForm.cs (650),
InstallationManager.cs (599), InstallFlow.cs (507), UninstallFlow.cs (127),
Program.cs (292), GamePathDetector.cs (178), Config.cs (214),
RegistryManager.cs (127), Logger.cs (112), InstallerLocale.cs (149),
GameLocale.cs (123), LanguageDetector.cs (54), GitHubClient.cs (432),
LogCollector.cs (416), MinidumpStripper.cs (284), DeadlyStreamClient.cs (125).

Method: full read of every file in the batch (not excerpted), followed by
targeted greps to verify every "unused"/"duplicate"/"uncalled" claim before
writing it down — the exact greps are quoted under each finding. No file in
this batch was skimmed only.

Accessibility was reviewed as the primary lens per the task brief: every
`Label.Text` / status-surface write in the batch was traced back to its
call sites to check for a matching `RaiseAutomationNotification`, and every
form control's accessible name / tab reachability was checked. Only
`MainForm.cs` is a `Form` in this batch — the other installer Forms
(WelcomeForm, ModSelectionForm, etc.) are out of scope for this report.

## Section A — general low-level cleanup

### A1 — Stale doc comment stranded on the wrong method (Program.cs:252-256)

What's there now:
```
252	        /// <summary>
253	        /// Welcome → Base-components info → Optional-mods checkboxes → Main install.
254	        /// Each form can cancel the chain.
255	        /// </summary>
256	        internal static bool IsNewerVersion(string latestVersion, string installedVersion)
```
This XML doc describes the multi-dialog install chain (Welcome → base-info →
optional-mods → MainForm), but it sits on `IsNewerVersion`, a pure
version-string comparator. It is post-split residue from candidate 17 (the
Phase-1 `Program.cs` → `GamePathDetector.cs` + `InstallFlow.cs` +
`UninstallFlow.cs` split): the comment used to describe the flow that is now
`InstallFlow.RunFullInstallFlow`, which carries no such comment today.
Why it's a problem: actively misleading — a reader looking at
`IsNewerVersion` gets a description of an unrelated dialog chain.
Proposed change: delete the stale comment (or move an equivalent one-line
note to `InstallFlow.RunFullInstallFlow`, which currently has none).
Risk: mechanical. Estimated delta: -4 lines.

### A2 — Unused `using` directives, all traceable to the candidate-17 split

Every file below still carries the full using-block that `Program.cs` needed
before the split, even where the split-off file (or the parts left in
Program.cs) no longer touches that namespace. Verified with a grep for the
actual API surface each namespace provides in the file (`Assembly.`,
`WindowsIdentity`/`WindowsPrincipal`, `RegistryKey`/`Registry.`,
`StringBuilder`/`Encoding.`, `List<`/`Dictionary<`/`MessageBox.`,
`Thread.`/`CancellationToken`) — zero hits in every case below.

- **Program.cs:5-9** — `System.Reflection` (5), `System.Security.Principal`
  (6), `System.Text` (7), `Microsoft.Win32` (9) are all unused. Nothing in
  this file calls `Assembly.*`, `WindowsIdentity`/`WindowsPrincipal`,
  `StringBuilder`/`Encoding`, or `RegistryKey`/`Registry.*` — those
  responsibilities moved to `GamePathDetector.cs` and `RegistryManager.cs`.
- **InstallFlow.cs:5,6,9** — `System.Reflection` (5), `System.Security.Principal`
  (6), `Microsoft.Win32` (9) are unused. (`System.Text` at line 7 IS used —
  `StringBuilder` in `RunKotor2PreparationFlow`.)
- **UninstallFlow.cs:2,5-9** — six of nine usings are unused:
  `System.Collections.Generic` (2, no `List<`/`Dictionary<` — the file uses
  a plain `string[]`), `System.Reflection` (5), `System.Security.Principal`
  (6), `System.Text` (7, no `StringBuilder`), `System.Windows.Forms` (8, no
  `MessageBox`/`Form` call in this file — it only logs and deletes files),
  `Microsoft.Win32` (9).
- **GamePathDetector.cs:5,7** — `System.Reflection` (5), `System.Text` (7)
  unused. (`System.Security.Principal` and `Microsoft.Win32` here ARE used —
  `WindowsIdentity`/`WindowsPrincipal` in `IsRunningAsAdmin`, `RegistryKey`
  in `TryReadSteamAppInstallPath`/`TryReadSteamKotor2InstallPath`.)
- **GitHubClient.cs:8** — `using System.Threading;` is unused; only
  `System.Threading.Tasks` (line 9) is exercised (`Task`/`async`/`await`).
  No `Thread.*` or bare `CancellationToken` in this file.

Why it's a problem: header noise that misstates each file's real
dependencies, and mild evidence the split (candidate 17) copied the
using-block wholesale instead of trimming per new file.
Proposed change: delete the unused lines listed above, one file at a time.
Risk: mechanical (compiler-checked — an unused-using deletion that breaks
the build is an immediate, obvious compile error). Estimated delta: -16
lines total across 5 files.

### A3 — Duplicated Steam-registry-read helper (GamePathDetector.cs)

`TryReadSteamAppInstallPath()` (around line 81-91) and
`TryReadSteamKotor2InstallPath()` (around line 122-132) are structurally
identical — same `RegistryKey.OpenBaseKey(...).OpenSubKey(...)` call, same
try/catch-null shape — differing only in the literal Steam App ID string
embedded in the subkey path (`Steam App 32370` vs `Steam App 208580`).
Why it's a problem: exact duplicate logic; a bug fixed in one (e.g. a
`Registry32`/`Registry64` view issue) has to be remembered and reapplied to
the other.
Proposed change: `private static string TryReadSteamAppInstallPath(string
appId) => ...` parameterized on the Steam App ID, called with `"32370"` and
`"208580"` from the two existing call sites (`DetectGamePath` /
`DetectKotor2GamePath`).
Risk: mechanical/low (pure string-parameterization, no behavior change).
Estimated delta: -8 lines.

### A4 — Dead public method, zero callers anywhere in the installer tree
(RegistryManager.cs:97-105)

`RegistryManager.IsRegistered()` has no call site. Verified with
`grep -rn "IsRegistered"` over the entire `installer\` tree (not just this
batch, to rule out a caller in one of the Forms outside the batch) — the
only hit is the method's own definition at line 97.
Why it's a problem: dead code; nothing in `MainForm`, `InstallFlow`,
`UninstallFlow`, `Program`, or any of the other installer Forms consults it
before registering/unregistering.
Proposed change: delete the method (lines 97-105).
Risk: mechanical (compiler-checked; it's a public static method with a
private-equivalent visibility in practice since the assembly is a
self-contained EXE, not a referenced library).
Estimated delta: -9 lines.

## Section B — AI-pattern findings

### B1 — Hand-rolled JSON parser duplicates a dependency already in the same
project (InstallerLocale.cs:81-147)

`ParseFlatJson`/`ParseJsonString` (lines 81-147, ~65 lines) hand-parse a
flat `{"key": "value", ...}` JSON object character-by-character, including
manual `\"`, `\\`, `\/`, `\n`, `\r`, `\t`, `\uXXXX` escape handling. The same
project already references `System.Text.Json` — `GitHubClient.cs` uses
`JsonDocument.Parse(...)` for the GitHub API responses just a few files
over. A flat key→string object is exactly `JsonDocument`'s easy case:
`JsonDocument.Parse(json).RootElement.EnumerateObject()` populating the
dictionary directly, no custom escape table needed.
Why it's a problem: needless reinvention of already-solved, already-
dependency-present functionality; the hand-rolled version is also the only
place in the batch doing manual UTF-16 escape decoding, which is exactly the
kind of code that's easy to get subtly wrong (and hard to notice going wrong
for a screen-reader user, since a mis-decoded locale string just reads
oddly rather than throwing).
Proposed change: replace `LoadEmbeddedLocale`'s body with a
`JsonDocument.Parse` + `EnumerateObject()` loop; delete `ParseFlatJson` and
`ParseJsonString`.
Risk: low — behavior-preserving for well-formed JSON (which the locale
files are, being hand-authored key/value files), but every locale file's
apostrophes/quotes/format-placeholders should be diffed before/after since
this is user-facing text in 6 languages. Estimated delta: -60 lines.

### B2 — Identity dictionary duplicates the array it's meant to validate
against (LanguageDetector.cs:25-33)

```
25	        private static readonly Dictionary<string, string> LanguageMap = new Dictionary<string, string>
26	        {
27	            { "en", "en" },
28	            { "de", "de" },
...
```
Every entry maps a key to itself. Functionally this dictionary is used only
as a set-membership check in `GetBestLanguage` (`LanguageMap.TryGetValue(...,
out match); return match;` — since key==value always, this is identical to
`SupportedLanguages.Contains(x) ? x : null`). The class's own doc comment
calls out that a new language must be "added ... in all three tables"
(`SupportedLanguages`, `DisplayNames`, `LanguageMap`) — but `LanguageMap`
carries no information `SupportedLanguages` doesn't already have.
Why it's a problem: a third table that must be kept in lockstep with a
second one for no semantic gain — a future language addition is one more
place to remember and get wrong.
Proposed change: replace `LanguageMap` with a direct `Array.IndexOf` /
`Contains` check against `SupportedLanguages` in `GetBestLanguage`; drop the
`LanguageMap` field entirely.
Risk: low — pure refactor, `GetBestLanguage`'s three-tier fallback logic
(exact name → two-letter ISO → parent culture) is unchanged, only the
lookup source changes. Estimated delta: -10 lines.

### B3 — Duplicated embedded-resource lookup-by-suffix helper across two
files (InstallationManager.cs:565-576, LogCollector.cs:248-267)

`InstallationManager.FindResourceName` and `LogCollector.ExtractResource`
both implement the identical "find the embedded resource whose full name
ends with the given short name (case-insensitive), then read it" search:
```
foreach (var name in assembly.GetManifestResourceNames())
    if (name.EndsWith(shortName, StringComparison.OrdinalIgnoreCase))
        return name; // / fullName = name; break;
```
`InstallationManager` wraps this in `ReadEmbeddedResourceBytes` /
`ExtractEmbeddedResource` (with the resilient-retry writer);
`LogCollector.ExtractResource` re-implements the same lookup loop inline,
minus the retry.
Why it's a problem: the suffix-match resource lookup is copy-pasted rather
than shared; a bug in the lookup logic (e.g. the assembly's resource naming
convention changing) has to be fixed in two places.
Proposed change: extract the lookup loop (only — not the retry writer,
which is `InstallationManager`-specific and load-bearing for its antivirus-
lock retry) into a small shared helper, e.g.
`EmbeddedResources.FindFullName(Assembly, string shortName)`, used by both
files.
Risk: low. Estimated delta: -12 lines net.

### B4 — Minor copy-paste: identical catch-log-rethrow wrapper
(GitHubClient.cs:57-89)

`DownloadReleaseAssetAsync` and `DownloadReleaseAssetByTagAsync` both wrap a
call into `DownloadTaggedAssetAsync` in the same
`catch (Exception ex) { Logger.Error($"Failed to download release asset
'{assetName}'", ex); throw; }` block. Small (5 lines each) and the two
methods otherwise do genuinely different tag-resolution work, so this is
low value — flagged for completeness, not urgency.
Risk: low. Estimated delta: -5 lines if squeezed into one private wrapper;
optional.

## Findings (possible bugs — user decides)

### F1 — Status-label update after Browse never reaches a screen reader
(MainForm.cs:201-216, called from BrowseButton_Click:197)

This is the accessibility defect the task brief specifically asked to hunt
for, and it is present:

```
201	        private void ValidatePath()
202	        {
203	            bool isValid = GamePathDetector.IsValidGamePath(_pathTextBox.Text);
204	            _installButton.Enabled = isValid;
205	
206	            if (!isValid && !string.IsNullOrEmpty(_pathTextBox.Text))
207	            {
208	                _statusLabel.Text = InstallerLocale.Get("Main_PathNotFound");
209	                _statusLabel.ForeColor = Color.Red;
210	            }
211	            else
212	            {
213	                _statusLabel.Text = InstallerLocale.Get(_updateOnly ? "Main_StatusUpdate" : "Main_StatusInstall");
214	                _statusLabel.ForeColor = SystemColors.ControlText;
215	            }
216	        }
```

`ValidatePath()` writes `_statusLabel.Text` directly at lines 208 and 213,
with no `RaiseAutomationNotification` call. Contrast with `UpdateStatus()`
(lines 546-574 in the same file), which sets the same label's `Text` and
then explicitly raises a UIA notification, with a comment explaining
exactly why: "WinForms Labels are not UIA live regions, so just changing
`_statusLabel.Text` is invisible to NVDA / JAWS / Narrator until the user
navigates to it." `ValidatePath()` is the one code path in this file that
does not follow its own house rule.

Call path that triggers it: `BrowseButton_Click` (line 181) → user picks a
folder in `FolderBrowserDialog` → `ValidatePath()` (line 197). If the picked
folder is not a valid KOTOR install, the Install button silently becomes
disabled and the status text silently changes to "path not found" — with
no announcement. A blind user who just browsed to the wrong folder gets no
feedback at all; they would only discover the problem by manually
re-navigating to the status label, or by tabbing to the now-silently-
disabled Install button and inferring the reason from JAWS/NVDA's
"unavailable" state cue alone (which says the button is off, not why).

This also silently fires on the *valid* path: after fixing a bad path, the
success status again lands with no announcement — less harmful (the button
becoming enabled is discoverable by tabbing to it), but still inconsistent
with `UpdateStatus`'s pattern.

Proposed change (for the user to decide, not applied): route both branches
of `ValidatePath()` through `UpdateStatus()` instead of setting
`_statusLabel.Text` directly — `UpdateStatus` already centralizes the
"set text + set color-neutral + notify" sequence; only the red-on-invalid
color would need to move into `ValidatePath` as a follow-up
`_statusLabel.ForeColor = ...` after calling `UpdateStatus`, or
`UpdateStatus` could grow an optional color parameter.

Risk: needs-in-game-test (well — needs a screen-reader test, not an
in-game one; the exact action to exercise it: launch the installer, click
Browse, pick a folder that is NOT a valid KOTOR install, and confirm NVDA
speaks the "path not found" status without moving focus).

### F2 — Install button's AccessibleDescription is captured once and goes
stale (MainForm.cs:165-167)

```
165	            string body = $"{_titleLabel.Text}. {_statusLabel.Text}";
166	            AccessibleDescription = body;
167	            _installButton.AccessibleDescription = body;
```
This runs once, inside `InitializeComponents()`, and concatenates the
title and the *initial* status text into both the form's and the Install
button's `AccessibleDescription`. Neither is ever refreshed afterward. Once
`_statusLabel.Text` changes — via `ValidatePath()` after a Browse (see F1),
or via `UpdateStatus()` during the install itself — a screen reader that
reads the button's accessible description (as opposed to just its `Text`)
still reports the form's original opening state, not the current one.
This compounds F1: even if F1 were fixed and `ValidatePath()` started
raising notifications, the stale description would still be sitting there
misreporting context on demand-read (e.g. NVDA's "read description" combo,
or JAWS's virtual-cursor description read).
Proposed change (for the user to decide): either drop the per-control
`AccessibleDescription` mirroring of status text (the notification from F1
already covers the "what changed" announcement) and rely on
`AccessibleDescription` for a static, unchanging "what does this button do"
description instead, or refresh `_installButton.AccessibleDescription`
alongside every `_statusLabel.Text` write.
Risk: needs-in-game-test — same screen-reader verification method as F1
(this time checking NVDA/JAWS's explicit "read description" command against
the Install button after a status change, not just the passive
announcement).

## Candidate 28 — narrow-header include opportunities

N/A for this batch. Candidate 28 is specific to the C++ patch's
`engine_offsets.h` aggregator family; this batch is C# and has no
equivalent aggregator-header pattern to migrate off of.

## Files scanned with nothing to report

- Config.cs — pure constants + doc comments, no logic, nothing to flag.
- InstallationManager.cs — beyond B3 (shared with LogCollector.cs), clean;
  the resilient-writer / staging / widescreen-gate logic is careful and
  well-commented, no redundancy found.
- Logger.cs — small, consistent, no dead code.
- GameLocale.cs — TLK-header locale detection, well-commented, no issues.
- MinidumpStripper.cs — dense but not sloppy; the minidump-format parsing
  is inherently low-level, every magic number is a named constant or has
  an inline comment explaining the offset, no duplication found.
- DeadlyStreamClient.cs — clean, no issues.
- LogCollector.cs — beyond B3, clean; the 7z-then-zip-fallback and
  system-info writer are straightforward.
- GitHubClient.cs — beyond A2 (unused `System.Threading`) and B4, clean;
  the redirect-first/API-fallback download strategy is well-documented and
  not duplicated elsewhere in the batch.
