using System.CommandLine;
using System.Globalization;
using System.Text;
using Kdev.Commands;

namespace Kdev;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        // Force invariant culture so command-line numeric arguments and
        // numeric output use "." as decimal separator regardless of the
        // host's regional setting (German `,` would otherwise reparse
        // `0.5` as `5`).
        CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;
        Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;

        // Register CP-1252 etc. — modern .NET only ships UTF-8/16/32 + ASCII
        // by default. combat-strings-extract needs CP-1252 to decode TLK
        // bytes for German/English/French/Italian/Spanish KOTOR locales.
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var root = new RootCommand("Dev CLI for the Voice of the Old Republic accessibility patch.")
        {
            StatusCommand.Build(),
            BuildCommand.Build(),
            CleanCommand.Build(),
            ApplyCommand.Build(),
            LaunchCommand.Build(),
            DevCommand.Build(),
            KillCommand.Build(),
            LogsCommand.Build(),
            AnalyzeDumpCommand.Build(),
            StripDumpCommand.Build(),
            WalkmeshStatsCommand.Build(),
            WalkmeshFaceTypesCommand.Build(),
            WalkmeshGeometryAuditCommand.Build(),
            CombatStringsExtractCommand.Build(),
            SoundScoreCommand.Build(),
            ReCommand.Build(),
            DumpTextCommand.Build(),
            SigScanCommand.Build(),
        };

        return await root.InvokeAsync(args);
    }
}
