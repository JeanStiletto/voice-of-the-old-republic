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
