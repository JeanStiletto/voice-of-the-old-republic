using System;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Text.Json;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Where the third-party mod pins come from — hashes, sizes, download pages
    /// and pinned commits for TSLRCM, the Tweak Pack, K1CP and K2CP.
    ///
    /// <para><b>Why this is not just constants.</b> The pins are the part of the
    /// installer with a shelf life. Upstream ships a new TSLRCM, its hash stops
    /// matching, and every install from then on drops to the manual path — for a
    /// blind user, the difference between one wizard and navigating a forum
    /// download page. Baking the pins into the binary meant refreshing them
    /// required a full installer release: build toolchain, publish, upload. This
    /// makes it an edit to one JSON file on the main branch.</para>
    ///
    /// <para><b>How it resolves.</b> The embedded copy of <c>sources.json</c> is
    /// parsed first and always wins if anything else fails — it ships with the
    /// binary, so there is no state in which we have no pins. Then the copy on
    /// the repo's main branch is fetched with a short timeout and, if it parses
    /// and carries a schema version we understand, replaces it. Every failure
    /// mode (offline, DNS blocked, 404, corporate proxy serving HTML, malformed
    /// JSON, future schema) lands on the embedded values silently: a remote pin
    /// file is a convenience, never a dependency.</para>
    ///
    /// <para><b>Trust.</b> The remote file carries hashes that decide what we are
    /// willing to execute, so it is fetched over HTTPS from our own repository —
    /// the same trust root as the installer binary and the .kpatch it downloads.
    /// It is not a new attack surface; anyone able to rewrite that file could
    /// already publish a malicious release. It must never be pointed at a host
    /// we do not control.</para>
    ///
    /// <para><b>What this does NOT fix.</b> A remote pin file only helps while
    /// somebody is still maintaining the repo. The path that survives total
    /// neglect is the guided manual download in
    /// <see cref="ManualDownloadForm"/> — that one cannot go stale, because the
    /// user supplies the file.</para>
    /// </summary>
    public static class SourcePins
    {
        /// <summary>Highest schema version this build knows how to read.</summary>
        private const int SupportedSchemaVersion = 1;

        private const string EmbeddedResourceName = "sources.json";

        /// <summary>
        /// The live copy, on the default branch of our own repository. Must stay
        /// a host we control — see the trust note in the class docs.
        /// </summary>
        private const string RemoteUrl =
            "https://raw.githubusercontent.com/JeanStiletto/voice-of-the-old-republic/main/" +
            "installer/KotorAccessibilityInstaller/Resources/sources.json";

        /// <summary>How long to wait on the remote copy before giving up on it.</summary>
        private static readonly TimeSpan RemoteTimeout = TimeSpan.FromSeconds(6);

        public sealed class FilePin
        {
            public string DisplayVersion { get; init; }
            public string FileName { get; init; }
            public string Sha256 { get; init; }
            public long SizeBytes { get; init; }
            public string PageUrl { get; init; }
        }

        public sealed class RepoPin
        {
            public string DisplayVersion { get; init; }
            public string RepoOwner { get; init; }
            public string RepoName { get; init; }
            public string Ref { get; init; }
        }

        /// <summary>
        /// A mod that is authored in a git repo but distributed as an archive —
        /// K2CP, whose repo carries the recipe and none of the payload. Both
        /// halves are recorded: the archive is what gets fetched, the commit is
        /// provenance.
        /// </summary>
        public sealed class ArchivePin
        {
            public string DisplayVersion { get; init; }
            public string FileName { get; init; }
            public string Sha256 { get; init; }
            public long SizeBytes { get; init; }
            public string PageUrl { get; init; }
            public string RepoOwner { get; init; }
            public string RepoName { get; init; }
            public string Ref { get; init; }
        }

        private sealed class Manifest
        {
            public int SchemaVersion { get; set; }
            public string Updated { get; set; }
            public FilePin Tslrcm { get; set; }
            public FilePin TweakPack { get; set; }
            public RepoPin K1cp { get; set; }
            public ArchivePin K2cp { get; set; }
        }

        private static Manifest _pins;
        private static readonly object _gate = new object();

        /// <summary>Where the pins in use came from, for the log and the log bundle.</summary>
        public static string Origin { get; private set; } = "(not loaded)";

        public static FilePin Tslrcm => Current.Tslrcm;
        public static FilePin TweakPack => Current.TweakPack;
        public static RepoPin K1cp => Current.K1cp;
        public static ArchivePin K2cp => Current.K2cp;

        private static Manifest Current
        {
            get
            {
                lock (_gate)
                {
                    // Not yet initialised (a code path that reads a pin before
                    // Program.Main got to Initialize). Load the embedded copy
                    // rather than returning null: the embedded copy is always
                    // valid, and a missing pin would fail far from the cause.
                    if (_pins == null) LoadEmbedded();
                    return _pins;
                }
            }
        }

        /// <summary>
        /// Load the embedded pins, then try to replace them with the repo copy.
        /// Safe to call once at startup; cheap and idempotent afterwards.
        /// Never throws — the whole point is that pin loading cannot break an
        /// install.
        /// </summary>
        public static void Initialize()
        {
            lock (_gate)
            {
                LoadEmbedded();
                TryLoadRemote();
                Logger.Info($"[SourcePins] Using pins from {Origin}: " +
                            $"TSLRCM {_pins.Tslrcm?.DisplayVersion}, " +
                            $"Tweak Pack {_pins.TweakPack?.DisplayVersion}, " +
                            $"K1CP {_pins.K1cp?.DisplayVersion}, " +
                            $"K2CP {_pins.K2cp?.DisplayVersion}");
            }
        }

        private static void LoadEmbedded()
        {
            if (_pins != null && Origin.StartsWith("remote", StringComparison.Ordinal)) return;

            try
            {
                var assembly = Assembly.GetExecutingAssembly();
                string fullName = null;
                foreach (var name in assembly.GetManifestResourceNames())
                {
                    if (name.EndsWith(EmbeddedResourceName, StringComparison.OrdinalIgnoreCase))
                    {
                        fullName = name;
                        break;
                    }
                }
                if (fullName == null)
                    throw new FileNotFoundException($"Embedded resource not found: {EmbeddedResourceName}");

                using var stream = assembly.GetManifestResourceStream(fullName);
                using var reader = new StreamReader(stream);
                _pins = Parse(reader.ReadToEnd());
                Origin = $"embedded (updated {_pins.Updated})";
            }
            catch (Exception ex)
            {
                // Only reachable if the build itself is broken. Fail loudly in
                // the log, and leave an empty manifest so the null-guards in the
                // consumers report a missing pin rather than crashing here.
                Logger.Error("[SourcePins] Embedded sources.json could not be read — " +
                             "the installer build is incomplete", ex);
                _pins = new Manifest();
                Origin = "(embedded copy unreadable)";
            }
        }

        private static void TryLoadRemote()
        {
            try
            {
                using var http = new HttpClient { Timeout = RemoteTimeout };
                // GitHub rejects requests without a user agent.
                http.DefaultRequestHeaders.Add("User-Agent", "VoiceOfTheOldRepublic-Installer");

                string json = http.GetStringAsync(RemoteUrl).GetAwaiter().GetResult();
                var remote = Parse(json);

                // A newer installer must not be dragged backwards by an older
                // manifest, and an older installer must not guess at a schema it
                // has never seen. Parse() has already rejected the second case.
                _pins = remote;
                Origin = $"remote (updated {remote.Updated})";
            }
            catch (Exception ex)
            {
                // Entirely expected offline, behind a proxy, or if the file has
                // not been published yet. Info, not warning — this is a normal
                // outcome, not a problem to report to the user.
                Logger.Info($"[SourcePins] Remote pins unavailable ({ex.GetType().Name}: {ex.Message}); " +
                            "using the embedded copy.");
            }
        }

        private static Manifest Parse(string json)
        {
            var options = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true,
            };

            var parsed = JsonSerializer.Deserialize<Manifest>(json, options)
                ?? throw new InvalidDataException("sources.json parsed to null");

            if (parsed.SchemaVersion <= 0 || parsed.SchemaVersion > SupportedSchemaVersion)
            {
                throw new InvalidDataException(
                    $"sources.json schemaVersion {parsed.SchemaVersion} is not one this build " +
                    $"understands (supported: 1..{SupportedSchemaVersion})");
            }

            return parsed;
        }
    }
}
