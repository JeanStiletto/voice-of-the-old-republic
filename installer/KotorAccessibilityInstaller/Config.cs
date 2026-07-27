namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Configuration constants for the installer.
    /// Update these values before building a release.
    /// </summary>
    public static class Config
    {
        /// <summary>
        /// GitHub repository URL for the Voice of the Old Republic mod.
        /// Format: "https://github.com/username/repo"
        /// </summary>
        public const string ModRepositoryUrl = "https://github.com/JeanStiletto/voice-of-the-old-republic";

        /// <summary>
        /// GitHub Pages site URL. Used for opening the README without the
        /// surrounding GitHub repo chrome. Must end with a trailing slash.
        /// </summary>
        public const string ModSiteUrl = "https://jeanstiletto.github.io/voice-of-the-old-republic/";

        /// <summary>
        /// Filename of the .kpatch artifact uploaded to GitHub releases.
        /// </summary>
        public const string KPatchAssetName = "Accessibility.kpatch";

        /// <summary>
        /// Patch ID inside the .kpatch — used by KPatchCore to look it up.
        /// Must match the manifest.toml inside patches/Accessibility/.
        /// </summary>
        public const string PatchId = "accessibility";

        /// <summary>
        /// Filename of the bundled Widescreen .kpatch embedded as a resource.
        /// Source: third_party/Kotor-Patch-Manager/Patches/Widescreen (Lane).
        /// Built artifact mirrored from build/patches/ at packaging time.
        /// </summary>
        public const string WidescreenKPatchAssetName = "Widescreen.kpatch";

        /// <summary>
        /// Patch ID for Lane's Widescreen patch. Matches the manifest.toml
        /// inside third_party/Kotor-Patch-Manager/Patches/Widescreen.
        /// </summary>
        public const string WidescreenPatchId = "widescreen";

        /// <summary>
        /// Publisher name for registry entries.
        /// </summary>
        public const string Publisher = "Voice of the Old Republic Project";

        /// <summary>
        /// Display name for Add/Remove Programs.
        /// </summary>
        public const string DisplayName = "Voice of the Old Republic";

        /// <summary>
        /// Filename of the persistent uninstaller copied into the game folder
        /// at install time so Add/Remove Programs keeps working after the
        /// original download is deleted.
        /// </summary>
        public const string UninstallerExeName = "VoiceOfTheOldRepublic_Uninstaller.exe";

        // ---------------------------------------------------------------------
        // K1CP (KOTOR 1 Community Patch) — source pin
        // ---------------------------------------------------------------------
        // We pull from the GitHub repo's source tarball at a pinned commit SHA
        // rather than `master`. This avoids surprise regressions if K1CP cuts
        // a bad commit between our releases; bumping requires editing this
        // constant and re-cutting an installer release.
        //
        // Current pin: 2026-02-09 commit, which IS the v1.10.1 release point
        // (DeadlyStream uploaded 2 days later on 2026-02-11 with the same
        // tslpatchdata content). See docs/installer.md for the recipe.

        public const string K1cpRepoOwner = "KOTORCommunityPatches";
        public const string K1cpRepoName = "K1_Community_Patch";
        public const string K1cpPinnedRef = "4778ae5e2f5facc2bb6449cf7ffa3720e35a5b0f";
        public const string K1cpDisplayVersion = "v1.10.1";

        // ---------------------------------------------------------------------
        // KOTOR 2 engine patches (Lane's static kpatches)
        // ---------------------------------------------------------------------
        // Both are pure static byte patches (no DLL payload), bundled as
        // resources and applied to swkotor2.exe when the user selects KOTOR 2.
        // Packaged from third_party/Kotor-Patch-Manager/Patches/<name>/; the
        // patch IDs must match each manifest.toml. Feature-equivalent to the
        // community 3C-FD tool's 4GB + borderless patches (see
        // docs/installer.md, "KOTOR 2 mod bundle").

        public const string K2FourGbKPatchAssetName = "4GBPatch.kpatch";
        public const string K2FourGbPatchId = "4gb-patch";
        public const string K2BorderlessKPatchAssetName = "BorderlessFullscreen.kpatch";
        public const string K2BorderlessPatchId = "borderless_fullscreen";

        // ---------------------------------------------------------------------
        // Unofficial TSLRCM Tweak Pack — source pin
        // ---------------------------------------------------------------------
        // DeadlyStream-hosted RAR5 archive, downloaded via the same guest
        // scrape as TSLRCM and extracted with Windows' built-in tar.exe
        // (libarchive reads RAR5). Contains per-component TSLPatcher payloads
        // that TweakPackInstaller drives through HoloPatcher one by one.
        // Requires TSLRCM 1.8.3+ — the KOTOR 2 flow gates on TSLRCM presence.

        /// <summary>
        /// Steam App ID of KOTOR 2 (Aspyr build) as a string, used for the
        /// workshop content path <c>steamapps/workshop/content/&lt;app&gt;/</c>.
        /// </summary>
        public const string Kotor2WorkshopAppId = "208580";

        public const string TweakPackDownloadPageUrl =
            "https://deadlystream.com/files/file/296-unofficial-tslrcm-tweak-pack/";
        public const string TweakPackArchiveFileName = "tweakpack.rar";
        public const string TweakPackDisplayVersion = "v1.3";

        /// <summary>
        /// SHA-256 of the Tweak Pack archive as downloaded 2026-07-27.
        /// Same fail-closed rule as the TSLRCM pin: a changed upstream file is
        /// not installed until this constant is bumped and re-verified.
        /// </summary>
        public const string TweakPackArchiveSha256 =
            "E98C94D53DFCCADDF6753AA58662E1AFD1D6EBB0241F66C8000BA0FF3A2F13B5";

        /// <summary>Archive size in bytes (progress fallback).</summary>
        public const long TweakPackArchiveSizeBytes = 1366051;

        // ---------------------------------------------------------------------
        // K2CP (KOTOR 2 Community Patch) — source pin
        // ---------------------------------------------------------------------
        // Same GitHub org as K1CP. The repo has NO export-ignore on tslpatchdata
        // and publishes no releases or tags; we pin the commit whose date matches
        // the v1.6.2 DeadlyStream release (both 2025-09-26). Not yet installed by
        // any shipped flow — see K2cpInstaller for the activation gates.

        public const string K2cpRepoOwner = "KOTORCommunityPatches";
        public const string K2cpRepoName = "TSL_Community_Patch";
        public const string K2cpPinnedRef = "4850a441368678ff72f3a173b33366c3c960d95e";
        public const string K2cpDisplayVersion = "v1.6.2";

        /// <summary>
        /// TSLRCM download page on DeadlyStream. TSLRCM has no auto-download
        /// host (no GitHub, ModDB is Cloudflare-gated, Steam Workshop version
        /// prohibited by the community build), so until a permission-based
        /// mirror exists the installer points the user at this page. The
        /// page's Download button serves the file to guests without an account.
        /// </summary>
        public const string TslrcmDownloadPageUrl =
            "https://deadlystream.com/files/file/578-tsl-restored-content-mod/";

        /// <summary>
        /// Filename DeadlyStream serves for the TSLRCM 1.8.6 installer
        /// (Content-Disposition on the download response), reused as the local
        /// temp filename.
        /// </summary>
        public const string TslrcmInstallerFileName = "tslrcm2022.exe";

        /// <summary>
        /// SHA-256 of the TSLRCM 1.8.6 installer exe as downloaded from
        /// DeadlyStream on 2026-07-27. The scraped download is verified against
        /// this before we run the exe — the file comes from a third-party site
        /// over a scraped endpoint, so run nothing that doesn't match. If
        /// TSLRCM ships an update, verification fails closed and the user is
        /// pointed at the manual browser download; bump this constant (and
        /// re-verify) to adopt the new file.
        /// </summary>
        public const string TslrcmInstallerSha256 =
            "94C99C4807DA4B304DE6E0EFED1BE55E0F43D13CC5A274582ED3323AC0E2F1A6";

        /// <summary>
        /// Size of the TSLRCM installer in bytes (progress fallback when the
        /// server omits Content-Length). Same capture as the hash above.
        /// </summary>
        public const long TslrcmInstallerSizeBytes = 137947655;

        /// <summary>
        /// HoloPatcher binary filename used at install time to drive
        /// TSLPatcher-style mod installs headlessly (K1CP and similar).
        /// Extracted from <see cref="HoloPatcherAssetName"/> in the system
        /// temp dir; cached for the install run only.
        /// </summary>
        public const string HoloPatcherExeName = "HoloPatcher.exe";

        /// <summary>
        /// Upstream GitHub repo whose releases hold the HoloPatcher Windows
        /// binary. The canonical OpenKotOR/PyKotor repo (formerly
        /// OldRepublicDevs/PyKotor) re-tagged v1.80-patcher in 2025 but
        /// attached no binary assets, so we pull from NickHugi/PyKotor —
        /// the last upstream point with a real HoloPatcher_Windows_x64.zip.
        /// </summary>
        public const string HoloPatcherRepositoryUrl = "https://github.com/NickHugi/PyKotor";

        /// <summary>
        /// Pinned release tag on <see cref="HoloPatcherRepositoryUrl"/>.
        /// We pin (rather than resolving "latest") because that repo's
        /// "latest" release is the Holocron Toolset, not HoloPatcher.
        /// </summary>
        public const string HoloPatcherPinnedTag = "v1.60-patcher-beta4";

        /// <summary>Display version surfaced in logs/UI.</summary>
        public const string HoloPatcherDisplayVersion = "v1.60-beta4";

        /// <summary>
        /// Asset filename on the GitHub release. Upstream ships HoloPatcher
        /// as a per-platform zip; we extract <see cref="HoloPatcherExeName"/>
        /// out of <see cref="HoloPatcherExePathInsideZip"/>.
        /// </summary>
        public const string HoloPatcherAssetName = "HoloPatcher_Windows_x64.zip";

        /// <summary>
        /// Path inside <see cref="HoloPatcherAssetName"/> at which the
        /// HoloPatcher.exe lives. The zip wraps the exe in a top-level
        /// folder matching the asset name.
        /// </summary>
        public const string HoloPatcherExePathInsideZip = "HoloPatcher_Windows_x64/HoloPatcher.exe";
    }
}
