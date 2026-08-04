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

        // =====================================================================
        // Third-party mod pins
        // =====================================================================
        // These now live in Resources/sources.json and are served by SourcePins,
        // which reads the embedded copy and then lets the copy on the repo's
        // main branch override it. That is what makes a pin refreshable by
        // editing one file rather than cutting an installer release — see
        // SourcePins for the full rationale and the trust note.
        //
        // The members below forward to SourcePins so the call sites did not all
        // have to change. Edit sources.json, NOT these.

        // ---------------------------------------------------------------------
        // K1CP (KOTOR 1 Community Patch) — source pin
        // ---------------------------------------------------------------------
        // We pull from the GitHub repo's source tarball at a pinned commit SHA
        // rather than `master`. This avoids surprise regressions if K1CP cuts
        // a bad commit between our releases.
        //
        // Shipped pin: 2026-02-09 commit, which IS the v1.10.1 release point
        // (DeadlyStream uploaded 2 days later on 2026-02-11 with the same
        // tslpatchdata content). See docs/installer.md for the recipe.

        public static string K1cpRepoOwner => SourcePins.K1cp.RepoOwner;
        public static string K1cpRepoName => SourcePins.K1cp.RepoName;
        public static string K1cpPinnedRef => SourcePins.K1cp.Ref;
        public static string K1cpDisplayVersion => SourcePins.K1cp.DisplayVersion;

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

        public static string TweakPackDownloadPageUrl => SourcePins.TweakPack.PageUrl;
        public static string TweakPackArchiveFileName => SourcePins.TweakPack.FileName;
        public static string TweakPackDisplayVersion => SourcePins.TweakPack.DisplayVersion;

        /// <summary>
        /// SHA-256 of the Tweak Pack archive. Fail-closed: a changed upstream
        /// file is not installed until this is re-verified and bumped in
        /// sources.json. The user can still install it via the guided manual
        /// download, where they supply the file themselves.
        /// </summary>
        public static string TweakPackArchiveSha256 => SourcePins.TweakPack.Sha256;

        /// <summary>Archive size in bytes (progress fallback).</summary>
        public static long TweakPackArchiveSizeBytes => SourcePins.TweakPack.SizeBytes;

        // ---------------------------------------------------------------------
        // K2CP (KOTOR 2 Community Patch) — source pin
        // ---------------------------------------------------------------------
        // Same GitHub org as K1CP. The repo has NO export-ignore on tslpatchdata
        // and publishes no releases or tags; we pin the commit whose date matches
        // the v1.6.2 DeadlyStream release (both 2025-09-26). Not yet installed by
        // any shipped flow — see K2cpInstaller for the activation gates.

        // Provenance only — the payload is NOT in this repo. See K2cpInstaller.
        public static string K2cpRepoOwner => SourcePins.K2cp.RepoOwner;
        public static string K2cpRepoName => SourcePins.K2cp.RepoName;
        public static string K2cpPinnedRef => SourcePins.K2cp.Ref;

        // ---------------------------------------------------------------------
        // Thematic Companions (KOTOR 1 and KOTOR 2) — source pins
        // ---------------------------------------------------------------------
        // Two sibling mods by the same authors, one per game, each an author-
        // published GitHub release asset. Opt-in on both mod-selection screens
        // and off by default: they change companion stats, and a strict-vanilla
        // baseline is the safer default for a first blind playthrough.
        //
        // Both are pure GFF edits — empty [TLKList], no scripts, no 2DAs — so
        // they are language-agnostic and install identically on every locale.
        // The KOTOR 2 one requires TSLRCM; the KOTOR 2 flow already gates its
        // whole mod pipeline on TSLRCM being present.

        public static SourcePins.ReleasePin ThematicCompanionsK1 => SourcePins.ThematicCompanionsK1;
        public static SourcePins.ReleasePin ThematicCompanionsK2 => SourcePins.ThematicCompanionsK2;

        public static string K2cpDisplayVersion => SourcePins.K2cp.DisplayVersion;
        public static string K2cpDownloadPageUrl => SourcePins.K2cp.PageUrl;
        public static string K2cpArchiveFileName => SourcePins.K2cp.FileName;
        public static string K2cpArchiveSha256 => SourcePins.K2cp.Sha256;
        public static long K2cpArchiveSizeBytes => SourcePins.K2cp.SizeBytes;

        /// <summary>
        /// TSLRCM download page on DeadlyStream. TSLRCM has no auto-download
        /// host (no GitHub, ModDB is Cloudflare-gated, Steam Workshop version
        /// prohibited by the community build), so until a permission-based
        /// mirror exists the installer points the user at this page. The
        /// page's Download button serves the file to guests without an account.
        /// </summary>
        public static string TslrcmDownloadPageUrl => SourcePins.Tslrcm.PageUrl;

        /// <summary>
        /// Filename DeadlyStream serves for the TSLRCM installer
        /// (Content-Disposition on the download response), reused as the local
        /// temp filename.
        /// </summary>
        public static string TslrcmInstallerFileName => SourcePins.Tslrcm.FileName;

        /// <summary>
        /// SHA-256 of the TSLRCM installer exe. The scraped download is verified
        /// against this before we RUN the exe, elevated — the file comes from a
        /// third-party site over a scraped endpoint with no human in the loop,
        /// so nothing that doesn't match is executed. Never relax this to make a
        /// download work.
        ///
        /// <para>If TSLRCM ships an update, verification fails closed and the
        /// user is offered the guided manual download instead — which is safe
        /// precisely because they fetch the file themselves. Adopt the new
        /// version by re-hashing it in sources.json.</para>
        /// </summary>
        public static string TslrcmInstallerSha256 => SourcePins.Tslrcm.Sha256;

        /// <summary>
        /// Size of the TSLRCM installer in bytes (progress fallback when the
        /// server omits Content-Length).
        /// </summary>
        public static long TslrcmInstallerSizeBytes => SourcePins.Tslrcm.SizeBytes;

        /// <summary>
        /// HoloPatcher binary filename used at install time to drive
        /// TSLPatcher-style mod installs headlessly (K1CP, K2CP, Tweak Pack).
        /// Doubles as the embedded-resource name; staged for the install run
        /// only.
        /// </summary>
        public const string HoloPatcherExeName = "HoloPatcher.exe";

        /// <summary>
        /// Provenance of the bundled HoloPatcher, kept for attribution and for
        /// the licence's source-availability requirement. Not a download
        /// location any more — the binary is embedded (see
        /// <see cref="ModInstallers.HoloPatcherProvider"/> for why).
        ///
        /// <para>The canonical OpenKotOR/PyKotor repo (formerly
        /// OldRepublicDevs/PyKotor) re-tagged v1.80-patcher in 2025 but attached
        /// no binary assets, so the vendored build comes from NickHugi/PyKotor —
        /// the last upstream point with a real HoloPatcher_Windows_x64.zip.
        /// Being on a fork's four-year-old release, with no manual fallback
        /// available to the user, is exactly why it is bundled now.</para>
        /// </summary>
        public const string HoloPatcherRepositoryUrl = "https://github.com/NickHugi/PyKotor";

        /// <summary>Release tag the vendored binary was taken from.</summary>
        public const string HoloPatcherPinnedTag = "v1.60-patcher-beta4";

        /// <summary>Display version surfaced in logs/UI.</summary>
        public const string HoloPatcherDisplayVersion = "v1.60-beta4";
    }
}
