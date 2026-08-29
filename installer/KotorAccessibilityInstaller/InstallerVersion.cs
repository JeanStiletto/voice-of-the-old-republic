using System;
using System.Reflection;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// The installer's own version, read off the assembly and formatted the one
    /// way the whole program agrees on.
    ///
    /// <para>There is no separate installer version line to maintain:
    /// installer/release.ps1 publishes with <c>-p:Version=$Version</c>, so every
    /// shipped EXE is stamped with the release it went out in (v0.8.0 -> file
    /// version 0.8.0.0). That is deliberate here in a way it would not be for a
    /// pure downloader — this EXE *carries* the payload (KotorPatcher.dll,
    /// prism.dll, the dinput8 proxy, the VC runtime, HoloPatcher, the address
    /// database, the Override samples), all of which are refreshed from
    /// third_party at release time. An installer one release behind therefore
    /// installs a current .kpatch next to stale bundled binaries, which is
    /// exactly what <see cref="SelfUpdater"/> exists to prevent.</para>
    ///
    /// <para>Three components always, never trimmed: the release tags are
    /// three-component (v0.8.0), and reporting "0.8" for what the release page
    /// calls "0.8.0" invites being heard as a different, rounded-off number.
    /// The fourth (revision) component is always zero and is dropped.</para>
    ///
    /// <para>A local dev build carries the csproj default (1.0.0) rather than a
    /// release number. It therefore sorts ABOVE every published release and no
    /// self-update is ever offered from one — which is the right outcome, but is
    /// an accident of the current numbering rather than a rule. Use
    /// <see cref="SelfUpdater.SkipSwitch"/> when a dev build must be held back
    /// from the check for certain.</para>
    /// </summary>
    public static class InstallerVersion
    {
        /// <summary>Bare version, e.g. "0.8.0". What gets compared.</summary>
        public static string Number()
        {
            var v = Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 0, 0);
            return $"{Math.Max(0, v.Major)}.{Math.Max(0, v.Minor)}.{Math.Max(0, v.Build)}";
        }

        /// <summary>Version for display, e.g. "v0.8.0". What goes in title bars.</summary>
        public static string Display() => "v" + Number();
    }
}
