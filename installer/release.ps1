# KOTOR Accessibility - Local Release Script
# Usage: powershell -NoProfile -File installer\release.ps1 [-Version 0.1.0]
#
# Builds the .kpatch via kdev, then the self-contained installer EXE,
# creates a git tag, and uploads both as a GitHub release.

param(
    [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root

# ── 1. Resolve version ──────────────────────────────────────────────────────

if (-not $Version) {
    # Fallback: use date-based version if no -Version supplied. This is intended
    # for early development; bump to a proper SemVer when releases stabilise.
    $Version = (Get-Date -Format 'yyyy.MM.dd')
    Write-Host "No -Version passed; defaulting to date-based version $Version" -ForegroundColor Yellow
}

$tag = "v$Version"
Write-Host "Releasing $tag" -ForegroundColor Cyan

# ── 2. Pre-flight checks ────────────────────────────────────────────────────

foreach ($tool in @('dotnet', 'git', 'gh', 'msbuild')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Host "ERROR: $tool not found" -ForegroundColor Red; exit 1
    }
}

$ghStatus = gh auth status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: gh CLI not authenticated. Run 'gh auth login' first." -ForegroundColor Red; exit 1
}

$gitDirty = git diff --stat HEAD
if ($gitDirty) {
    Write-Host "ERROR: Uncommitted changes to tracked files. Commit or stash first." -ForegroundColor Red
    git diff --stat HEAD
    exit 1
}

$existingTag = git tag -l $tag
if ($existingTag) {
    Write-Host "ERROR: Tag $tag already exists" -ForegroundColor Red; exit 1
}

Write-Host "Pre-flight checks passed" -ForegroundColor Green

# ── 3. Build our vendored KotorPatcher.dll ──────────────────────────────────
# This is our locally-modified fork of Lane's upstream (see docs/known-issues.md
# "Installer" section + docs/framework-changes-backup.patch). Building here
# guarantees the installer ships our patches, not whatever happens to be in
# third_party/Kotor-Patch-Manager/bin/Release/ from a prior session.

Write-Host "`nBuilding vendored KotorPatcher.dll (Release, x86)..." -ForegroundColor Cyan
$kpmSln = Join-Path $root 'third_party\Kotor-Patch-Manager\KotorPatchManager.sln'
msbuild $kpmSln /p:Configuration=Release /p:Platform=x86 /t:KotorPatcher /m /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: KotorPatcher build failed" -ForegroundColor Red; exit 1
}

$kotorPatcherDll = Join-Path $root 'third_party\Kotor-Patch-Manager\bin\Release\KotorPatcher.dll'
if (-not (Test-Path $kotorPatcherDll)) {
    Write-Host "ERROR: KotorPatcher.dll not produced at $kotorPatcherDll" -ForegroundColor Red; exit 1
}
Write-Host "  KotorPatcher.dll: $kotorPatcherDll" -ForegroundColor Green

# ── 4. Build the .kpatch via kdev ───────────────────────────────────────────

Write-Host "`nBuilding .kpatch via kdev..." -ForegroundColor Cyan
$kdevExe = Join-Path $root 'tools\kdev\bin\Debug\net10.0\win-x64\kdev.exe'
if (-not (Test-Path $kdevExe)) {
    Write-Host "kdev.exe not found; building kdev first..." -ForegroundColor Yellow
    dotnet build (Join-Path $root 'tools\kdev\kdev.csproj') -c Debug
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: kdev build failed" -ForegroundColor Red; exit 1 }
}

& $kdevExe build
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: kdev build failed" -ForegroundColor Red; exit 1 }

$kpatchPath = Join-Path $root 'build\Accessibility.kpatch'
if (-not (Test-Path $kpatchPath)) {
    Write-Host "ERROR: Accessibility.kpatch not produced at $kpatchPath" -ForegroundColor Red; exit 1
}

# ── 5. Refresh installer bundled resources from current third_party state ───

Write-Host "`nRefreshing installer bundled resources..." -ForegroundColor Cyan
$installerResources = Join-Path $root 'installer\KotorAccessibilityInstaller\Resources'
Copy-Item (Join-Path $root 'third_party\Kotor-Patch-Manager\bin\Release\KotorPatcher.dll') $installerResources -Force
Copy-Item (Join-Path $root 'third_party\Kotor-Patch-Manager\bin\Release\sqlite3.dll')      $installerResources -Force
Copy-Item (Join-Path $root 'third_party\prism-dist\x86\prism.dll')                          $installerResources -Force
Copy-Item (Join-Path $root 'third_party\Kotor-Patch-Manager\AddressDatabases\kotor1_0_3.db') $installerResources -Force
Write-Host "  Resources refreshed" -ForegroundColor Green

# ── 6. Publish installer as self-contained single-file EXE ──────────────────

Write-Host "`nPublishing installer (Release, single-file, self-contained)..." -ForegroundColor Cyan
$installerCsproj = Join-Path $root 'installer\KotorAccessibilityInstaller\KotorAccessibilityInstaller.csproj'
dotnet publish $installerCsproj -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true `
    -p:Version=$Version
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: Installer publish failed" -ForegroundColor Red; exit 1 }

$installerExe = Join-Path $root 'installer\KotorAccessibilityInstaller\bin\Release\net8.0-windows\win-x64\publish\KotorAccessibilityInstaller.exe'
if (-not (Test-Path $installerExe)) {
    Write-Host "ERROR: Installer EXE not found at $installerExe" -ForegroundColor Red; exit 1
}

Write-Host "Artifacts built:" -ForegroundColor Green
Write-Host "  .kpatch:  $kpatchPath"
Write-Host "  installer: $installerExe"

# ── 7. Release notes ────────────────────────────────────────────────────────

$installerHash = (Get-FileHash -Path $installerExe -Algorithm SHA256).Hash.ToLower()
$kpatchHash    = (Get-FileHash -Path $kpatchPath   -Algorithm SHA256).Hash.ToLower()

$notesText = @"
KOTOR Accessibility $tag

Run ``KotorAccessibilityInstaller.exe`` to install. The installer detects your
Steam copy of KOTOR, downloads this release's ``Accessibility.kpatch``, applies
it via KPatchManager, and drops the Prism speech runtime alongside.

---

**Verification (SHA256):**

- ``KotorAccessibilityInstaller.exe``: ``$installerHash``
- ``Accessibility.kpatch``: ``$kpatchHash``

Verify with ``Get-FileHash <filename> -Algorithm SHA256`` (PowerShell) or
``certutil -hashfile <filename> SHA256`` (CMD).
"@

$notesFile = Join-Path $root 'release_notes.txt'
$notesText | Out-File -FilePath $notesFile -Encoding utf8

# ── 8. Tag + push ───────────────────────────────────────────────────────────

Write-Host "`nCreating tag $tag..." -ForegroundColor Cyan
git tag -a $tag -m "Release $tag"
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: Failed to create tag" -ForegroundColor Red; exit 1 }

Write-Host "Pushing tag $tag..." -ForegroundColor Cyan
git push origin $tag
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to push tag. Deleting local tag." -ForegroundColor Red
    git tag -d $tag
    exit 1
}

# ── 9. Create GitHub release ─────────────────────────────────────────────────

Write-Host "`nCreating GitHub release..." -ForegroundColor Cyan
gh release create $tag `
    --title $tag `
    --notes-file $notesFile `
    $kpatchPath `
    $installerExe

if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: Failed to create GitHub release" -ForegroundColor Red; exit 1 }

Remove-Item $notesFile -ErrorAction SilentlyContinue

Write-Host "`nRelease $tag published successfully." -ForegroundColor Green
