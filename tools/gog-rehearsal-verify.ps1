# Verification for the fake-GOG installer rehearsal (no GOG copy needed).
# See docs/installer.md "Preserve-tlk English install" for what is being tested.
#
# Usage:
#   Before the installer run:  .\gog-rehearsal-verify.ps1 -Snapshot
#   After the installer run:   .\gog-rehearsal-verify.ps1
#
# All output is plain text, one finding per line, PASS/FAIL prefixed.
# ASCII only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.

param(
    [string]$GamePath = "C:\Users\fabia\KotorAccTest\swkotor2",
    [switch]$Snapshot
)

$snapshotFile = Join-Path $PSScriptRoot "gog-rehearsal-snapshot.json"
$tlk = Join-Path $GamePath "dialog.tlk"

function Get-TlkLanguageId([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $buf = New-Object byte[] 12
        [void]$fs.Read($buf, 0, 12)
        return [BitConverter]::ToUInt32($buf, 8)
    } finally { $fs.Close() }
}

if ($Snapshot) {
    $info = @{
        TlkHash       = (Get-FileHash $tlk -Algorithm SHA256).Hash
        TlkLanguageId = Get-TlkLanguageId $tlk
        TakenAt       = (Get-Date).ToString("o")
    }
    $info | ConvertTo-Json | Out-File $snapshotFile -Encoding utf8
    "Snapshot saved: dialog.tlk language ID $($info.TlkLanguageId), hash starts $($info.TlkHash.Substring(0,16))"
    return
}

if (-not (Test-Path $snapshotFile)) {
    "FAIL  no snapshot found - run with -Snapshot before the installer test"
    return
}
$base = Get-Content $snapshotFile -Raw | ConvertFrom-Json
$failures = 0

# 1. The localized table is back, byte-identical to before the test.
$hashNow = (Get-FileHash $tlk -Algorithm SHA256).Hash
if ($hashNow -eq $base.TlkHash) {
    "PASS  dialog.tlk is byte-identical to the pre-test localized table"
} else {
    "FAIL  dialog.tlk changed (hash starts: was $($base.TlkHash.Substring(0,16)), is $($hashNow.Substring(0,16)))"
    $langNow = Get-TlkLanguageId $tlk
    "      language ID now: $langNow (0 is English, 2 is German)"
    $failures++
}

# 2. TSLRCM's English table was kept aside.
$englishBak = Join-Path $GamePath "dialog.tlk.english.bak"
if (Test-Path $englishBak) {
    $langId = Get-TlkLanguageId $englishBak
    if ($langId -eq 0) {
        "PASS  dialog.tlk.english.bak exists and is English (language ID 0)"
    } else {
        "FAIL  dialog.tlk.english.bak exists but has language ID $langId, expected 0"
        $failures++
    }
} else {
    "FAIL  dialog.tlk.english.bak missing - either the restore never ran or the install never replaced the table"
    $failures++
}

# 3. The working backup was cleaned up after a successful restore.
$localizedBak = Join-Path $GamePath "dialog.tlk.localized.bak"
if (Test-Path $localizedBak) {
    "FAIL  dialog.tlk.localized.bak still present - restore did not complete cleanly"
    $failures++
} else {
    "PASS  dialog.tlk.localized.bak was cleaned up"
}

# 4. The trap-kit items whose descriptions only exist in the English table are gone.
$trapKits = @(Get-ChildItem (Join-Path $GamePath "override\g_i_trapkit*.uti") -ErrorAction SilentlyContinue)
if ($trapKits.Count -eq 0) {
    "PASS  no g_i_trapkit*.uti in Override (English install's copies were removed)"
} else {
    $names = $trapKits.Name -join ", "
    "FAIL  $($trapKits.Count) g_i_trapkit*.uti file(s) remain in Override: $names"
    $failures++
}

# 5. TSLRCM content actually arrived (spot check: module count).
$modules = @(Get-ChildItem (Join-Path $GamePath "modules\*.mod") -ErrorAction SilentlyContinue)
"INFO  $($modules.Count) .mod file(s) in modules folder (TSLRCM ships dozens; near zero means it never installed)"

""
if ($failures -eq 0) {
    "RESULT: all checks passed."
} else {
    "RESULT: $failures check(s) FAILED - see lines above. The installer log (offered for saving to the Desktop on exit) has the Preserve-tlk lines."
}
