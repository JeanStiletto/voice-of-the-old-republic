using System.CommandLine;
using System.CommandLine.Invocation;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Kdev.Commands;

/// <summary>
/// Capture the running game's decrypted image from memory.
///
/// Why this exists
/// ---------------
/// Our hook addresses and the ~260 `constexpr uintptr_t kAddr*` engine
/// constants were all derived against the Steam 1.0.3 layout, but the Steam
/// swkotor.exe is SteamStub-encrypted ON DISK — it carries a `.bind` section
/// and its `.text` reads as ciphertext until the loader decrypts it. So there
/// is no offline byte-reference for those addresses.
///
/// Supporting another build (the Allard Russian exe, a future GoG re-pack,
/// anything) means locating each of our addresses in the other binary, and
/// that needs known-good bytes to search for. This command produces them: it
/// reads the mapped image straight out of the live process, where it is
/// decrypted and sitting at its link-time base (the exe has no ASLR —
/// DllCharacteristics is 0 and ImageBase is 0x400000 — so an address in the
/// dump is the same address we hardcode).
///
/// One launch is enough, and it does not need our DLL: SteamStub decrypts
/// before the entry point runs, so any moment after the process starts works.
///
/// Usage
/// -----
///   1. Start the game (kdev launch, or just run it).
///   2. kdev dump-text                       # writes build/re/imagedump/
///   3. kdev sigscan ...                     # consumes the dump
///
/// Output (build/re/imagedump/ by default, gitignored):
///   swkotor-image.bin   the mapped image, SizeOfImage bytes from ImageBase.
///                       Indexed by RVA, NOT by file offset — sections sit at
///                       their VirtualAddress, not their PointerToRawData.
///   swkotor-image.json  imageBase, sizeOfImage, timestamp and the section
///                       table, so consumers need not re-parse the headers.
/// </summary>
public static class DumpTextCommand
{
    public static Command Build()
    {
        var outOpt = new Option<DirectoryInfo?>(
            name: "--out",
            description: "Output directory (default: build/re/imagedump).");

        var pidOpt = new Option<int?>(
            name: "--pid",
            description: "Explicit process id, if several copies are running.");

        var cmd = new Command("dump-text",
            "Dump the running game's decrypted image from memory as a byte reference.")
        {
            outOpt, pidOpt
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var outDir = ctx.ParseResult.GetValueForOption(outOpt);
            var pid = ctx.ParseResult.GetValueForOption(pidOpt);
            ctx.ExitCode = Run(outDir, pid);
        });
        return cmd;
    }

    internal static int Run(DirectoryInfo? outDir, int? pid)
    {
        Process? proc = ResolveProcess(pid);
        if (proc == null) return 1;

        string dest;
        if (outDir != null)
        {
            dest = outDir.FullName;
        }
        else
        {
            var cfg = KdevConfig.LoadOrPrintErrors(out int cfgExit);
            if (cfg == null) return cfgExit;
            dest = Path.Combine(cfg.ProjectRoot, "build", "re", "imagedump");
        }
        Directory.CreateDirectory(dest);

        using var handle = new ProcessHandle(proc.Id);
        if (handle.IsInvalid)
        {
            Console.Error.WriteLine(
                $"ERROR: could not open process {proc.Id} for reading " +
                $"(Win32 error {Marshal.GetLastWin32Error()}). " +
                "Run kdev from an elevated shell if the game is elevated.");
            return 1;
        }

        // The main module's base is the image base for a non-ASLR exe.
        ulong imageBase = (ulong)proc.MainModule!.BaseAddress.ToInt64();
        Console.WriteLine($"Attached to pid {proc.Id}, image base 0x{imageBase:X8}");

        // Read the PE headers first so we know how much to pull.
        var headers = new byte[0x1000];
        if (!handle.Read(imageBase, headers))
        {
            Console.Error.WriteLine("ERROR: could not read PE headers from the process.");
            return 1;
        }

        PeInfo pe;
        try
        {
            pe = PeInfo.Parse(headers, imageBase);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"ERROR: PE header parse failed: {ex.Message}");
            return 1;
        }

        Console.WriteLine($"SizeOfImage 0x{pe.SizeOfImage:X}, {pe.Sections.Count} sections, " +
                          $"link timestamp {pe.TimestampUtc:u}");

        var image = new byte[pe.SizeOfImage];
        // Copy the headers we already have, then pull each section at its RVA.
        // Reading the whole range in one call fails: an image has guard/unmapped
        // gaps between sections, and ReadProcessMemory is all-or-nothing.
        Array.Copy(headers, image, Math.Min(headers.Length, image.Length));

        int failed = 0;
        foreach (var s in pe.Sections)
        {
            uint span = Math.Max(s.VirtualSize, s.RawSize);
            if (span == 0) continue;
            if (s.VirtualAddress + span > pe.SizeOfImage) span = pe.SizeOfImage - s.VirtualAddress;

            var buf = new byte[span];
            if (handle.Read(imageBase + s.VirtualAddress, buf))
            {
                Array.Copy(buf, 0, image, (int)s.VirtualAddress, buf.Length);
                Console.WriteLine($"  {s.Name,-9} RVA 0x{s.VirtualAddress:X8}  {span,10} bytes  ok");
            }
            else
            {
                failed++;
                Console.WriteLine($"  {s.Name,-9} RVA 0x{s.VirtualAddress:X8}  {span,10} bytes  " +
                                  $"FAILED (Win32 {Marshal.GetLastWin32Error()})");
            }
        }

        if (pe.Sections.All(s => s.Name != ".text"))
        {
            Console.Error.WriteLine("ERROR: no .text section found; refusing to write a useless dump.");
            return 1;
        }

        string binPath = Path.Combine(dest, "swkotor-image.bin");
        string jsonPath = Path.Combine(dest, "swkotor-image.json");
        File.WriteAllBytes(binPath, image);

        var meta = new
        {
            note = "Mapped image read from the live process. Index by RVA, not file offset.",
            capturedUtc = DateTime.UtcNow.ToString("u"),
            processId = proc.Id,
            imageBase = pe.ImageBase,
            sizeOfImage = pe.SizeOfImage,
            linkTimestamp = pe.Timestamp,
            linkTimestampUtc = pe.TimestampUtc.ToString("u"),
            sections = pe.Sections.Select(s => new
            {
                name = s.Name,
                virtualAddress = s.VirtualAddress,
                virtualSize = s.VirtualSize,
                rawSize = s.RawSize,
            }),
        };
        File.WriteAllText(jsonPath,
            JsonSerializer.Serialize(meta, new JsonSerializerOptions { WriteIndented = true }));

        Console.WriteLine();
        Console.WriteLine($"Wrote {binPath} ({image.Length} bytes)");
        Console.WriteLine($"Wrote {jsonPath}");
        if (failed > 0)
        {
            Console.Error.WriteLine($"WARNING: {failed} section(s) could not be read; " +
                                    "the dump has zero-filled gaps.");
            return 1;
        }

        // Cheap sanity check: a decrypted .text should not look like ciphertext.
        // SteamStub'd bytes are near-uniformly random; real x86 is not.
        var text = pe.Sections.First(s => s.Name == ".text");
        double ratio = PrintableRunRatio(image, (int)text.VirtualAddress,
                                         (int)Math.Min(text.VirtualSize, 0x40000));
        Console.WriteLine($"Decryption sanity check: {ratio:P1} of sampled .text bytes are " +
                          "common x86 opcodes (ciphertext scores near 2%).");
        if (ratio < 0.05)
        {
            Console.Error.WriteLine(
                "WARNING: .text still looks encrypted. Was the dump taken before the " +
                "loader unpacked the image?");
            return 1;
        }

        return 0;
    }

    /// <summary>
    /// Fraction of sampled bytes that are among the handful of opcodes real x86
    /// is saturated with (push/pop reg, mov reg-reg, call rel32, int3 padding).
    /// Decrypted engine code lands well above 10%; random ciphertext sits at
    /// roughly 2% because those opcodes are ~2% of the byte space.
    /// </summary>
    private static double PrintableRunRatio(byte[] image, int start, int count)
    {
        if (count <= 0) return 0;
        int hits = 0;
        int end = Math.Min(start + count, image.Length);
        for (int i = start; i < end; i++)
        {
            byte b = image[i];
            bool common =
                (b >= 0x50 && b <= 0x5F) ||  // push/pop r32
                b == 0x8B || b == 0x89 ||    // mov r/m32
                b == 0xE8 || b == 0xE9 ||    // call/jmp rel32
                b == 0xCC ||                 // int3 alignment padding
                b == 0xC3;                   // ret
            if (common) hits++;
        }
        return (double)hits / (end - start);
    }

    private static Process? ResolveProcess(int? pid)
    {
        if (pid.HasValue)
        {
            try { return Process.GetProcessById(pid.Value); }
            catch (ArgumentException)
            {
                Console.Error.WriteLine($"ERROR: no process with id {pid.Value}.");
                return null;
            }
        }

        var procs = Process.GetProcessesByName(
            Path.GetFileNameWithoutExtension(GameProcess.ExeName));
        if (procs.Length == 0)
        {
            Console.Error.WriteLine(
                $"ERROR: {GameProcess.ExeName} is not running. Start the game first " +
                "(kdev launch), then re-run this command. Any point after launch works — " +
                "the image is already decrypted by the time the entry point runs.");
            return null;
        }
        if (procs.Length > 1)
        {
            Console.Error.WriteLine(
                $"ERROR: {procs.Length} copies of {GameProcess.ExeName} are running. " +
                "Pass --pid to choose one.");
            return null;
        }
        return procs[0];
    }

    // ---- Win32 process-memory access -------------------------------------

    private sealed class ProcessHandle : IDisposable
    {
        private const int PROCESS_VM_READ = 0x0010;
        private const int PROCESS_QUERY_INFORMATION = 0x0400;

        private readonly IntPtr _handle;
        public bool IsInvalid => _handle == IntPtr.Zero;

        public ProcessHandle(int pid)
        {
            _handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pid);
        }

        public bool Read(ulong address, byte[] buffer)
        {
            return ReadProcessMemory(_handle, (IntPtr)address, buffer,
                                     (IntPtr)buffer.Length, out IntPtr read)
                   && read.ToInt64() == buffer.Length;
        }

        public void Dispose()
        {
            if (!IsInvalid) CloseHandle(_handle);
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(int access, bool inherit, int pid);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadProcessMemory(IntPtr process, IntPtr address,
            [Out] byte[] buffer, IntPtr size, out IntPtr bytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);
    }
}
