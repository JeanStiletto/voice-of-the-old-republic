using System.CommandLine;
using System.CommandLine.Invocation;
using System.Globalization;
using System.IO;
using System.Linq;
using Kdev.Models;
using Microsoft.Diagnostics.Runtime;
using Microsoft.Diagnostics.Runtime.DataReaders.Implementation;

namespace Kdev.Commands;

/// <summary>
/// Open a Windows minidump captured by WER (typically under
/// %LOCALAPPDATA%\CrashDumps), extract the exception record + crashing thread
/// register state, walk the EBP frame chain, ESP-scan for return addresses,
/// and resolve every code address against the Ghidra function table.
///
/// Purpose-built for the KOTOR Tab-crash investigation: surfaces "which engine
/// function did the bad indirect call" that lands EIP inside a heap struct.
/// One-time investment, runs again any time we crash post-Phase 3 click-sim.
/// </summary>
public static class AnalyzeDumpCommand
{
    private const string DefaultDumpDir = @"%LOCALAPPDATA%\CrashDumps";
    private const string DefaultGhidraXml = @"docs\llm-docs\re\k1_win_gog_swkotor.exe.xml";

    public static Command Build()
    {
        var dumpArg = new Argument<string?>(
            name: "dump",
            description: "Path to a .dmp file. If omitted, uses the newest dump in %LOCALAPPDATA%\\CrashDumps.")
        {
            Arity = ArgumentArity.ZeroOrOne,
        };

        var listOpt = new Option<bool>(
            name: "--list",
            description: "List available dumps newest-first instead of analyzing one.");

        var depthOpt = new Option<int>(
            name: "--depth",
            description: "Stack walk depth (frames). Default 16.",
            getDefaultValue: () => 16);

        var stackBytesOpt = new Option<int>(
            name: "--stack-bytes",
            description: "Bytes of stack memory above ESP to scan for return addresses + dump as hex. Default 256.",
            getDefaultValue: () => 256);

        // Peek mode: read raw bytes at an arbitrary VA from the dump.
        // Lets us inspect runtime-decrypted .text (the on-disk swkotor.exe is
        // packed, so dumpbin/ida can't disassemble; minidumps capture the
        // unpacked image). Used to RE individual functions whose XML metadata
        // exists but whose decompilation Lane's gzf doesn't expose.
        var peekOpt = new Option<string?>(
            name: "--peek",
            description: "Read N bytes at VA (e.g. \"0x006b0bb0\"). With --peek-len for length.");
        var peekLenOpt = new Option<int>(
            name: "--peek-len",
            description: "Bytes to read in --peek mode. Default 256.",
            getDefaultValue: () => 256);

        var modulesOpt = new Option<bool>(
            name: "--modules",
            description: "List loaded modules (name + base + size) sorted by base address, then exit.");

        var cmd = new Command("analyze-dump",
            "Analyze a swkotor.exe minidump: exception, registers, EBP-chain stack walk, " +
            "ESP-scan, all resolved against Lane's Ghidra function table.");

        cmd.AddArgument(dumpArg);
        cmd.AddOption(listOpt);
        cmd.AddOption(depthOpt);
        cmd.AddOption(stackBytesOpt);
        cmd.AddOption(peekOpt);
        cmd.AddOption(peekLenOpt);
        cmd.AddOption(modulesOpt);

        cmd.SetHandler((InvocationContext context) =>
        {
            var dump = context.ParseResult.GetValueForArgument(dumpArg);
            var list = context.ParseResult.GetValueForOption(listOpt);
            var depth = context.ParseResult.GetValueForOption(depthOpt);
            var stackBytes = context.ParseResult.GetValueForOption(stackBytesOpt);
            var peek = context.ParseResult.GetValueForOption(peekOpt);
            var peekLen = context.ParseResult.GetValueForOption(peekLenOpt);
            var modules = context.ParseResult.GetValueForOption(modulesOpt);
            if (list)
                context.ExitCode = RunList();
            else if (modules)
                context.ExitCode = RunModules(dump);
            else if (!string.IsNullOrEmpty(peek))
                context.ExitCode = RunPeek(dump, peek!, peekLen);
            else
                context.ExitCode = RunAnalyze(dump, depth, stackBytes);
        });

        return cmd;
    }

    // ---------------- Modules (loaded module list) ----------------

    private static int RunModules(string? dumpArg)
    {
        FileInfo? dumpFile;
        if (!string.IsNullOrEmpty(dumpArg))
        {
            if (!File.Exists(dumpArg))
            {
                Console.Error.WriteLine($"ERROR: dump file not found: {dumpArg}");
                return 2;
            }
            dumpFile = new FileInfo(dumpArg);
        }
        else
        {
            var dumps = DiscoverDumps();
            if (dumps.Length == 0)
            {
                Console.Error.WriteLine("ERROR: no dumps and no path given.");
                return 2;
            }
            dumpFile = dumps[0];
        }

        Console.WriteLine($"Dump: {dumpFile.Name}");
        Console.WriteLine();

        using var dt = DataTarget.LoadDump(dumpFile.FullName);
        var mods = dt.EnumerateModules()
            .OrderBy(m => m.ImageBase)
            .ToList();
        Console.WriteLine($"Loaded modules ({mods.Count}):");
        Console.WriteLine($"  {"base",-12} {"end",-12} {"size",-10}  name");
        foreach (var m in mods)
        {
            ulong end = m.ImageBase + (ulong)m.IndexFileSize;
            string name = m.FileName ?? "<unknown>";
            Console.WriteLine($"  0x{m.ImageBase:x8}   0x{end:x8}   {m.IndexFileSize,-10}  {name}");
        }
        return 0;
    }

    // ---------------- Peek (raw byte read at VA) ----------------

    private static int RunPeek(string? dumpArg, string vaStr, int len)
    {
        if (!TryParseHex(vaStr, out uint va))
        {
            Console.Error.WriteLine($"ERROR: bad --peek address: {vaStr}");
            return 2;
        }
        if (len <= 0 || len > 0x10000)
        {
            Console.Error.WriteLine($"ERROR: --peek-len out of range: {len}");
            return 2;
        }

        FileInfo? dumpFile;
        if (!string.IsNullOrEmpty(dumpArg))
        {
            if (!File.Exists(dumpArg))
            {
                Console.Error.WriteLine($"ERROR: dump file not found: {dumpArg}");
                return 2;
            }
            dumpFile = new FileInfo(dumpArg);
        }
        else
        {
            var dumps = DiscoverDumps();
            if (dumps.Length == 0)
            {
                Console.Error.WriteLine($"ERROR: no dumps and no path given.");
                return 2;
            }
            dumpFile = dumps[0];
        }
        Console.WriteLine($"Dump: {dumpFile.Name}");
        Console.WriteLine($"VA:   0x{va:x8}");
        Console.WriteLine($"Len:  {len} bytes");
        Console.WriteLine();

        using var dt = DataTarget.LoadDump(dumpFile.FullName);
        var buf = new byte[len];
        int n = ReadAvailable(dt.DataReader, va, buf);
        if (n <= 0)
        {
            Console.Error.WriteLine("(no readable bytes at that VA in this dump)");
            return 4;
        }
        if (n < len)
            Console.WriteLine($"(short read: {n}/{len} bytes)");

        DumpHex(buf, n, va);
        return 0;
    }

    private static bool TryParseHex(string s, out uint v)
    {
        s = s.Trim();
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) s = s.Substring(2);
        return uint.TryParse(s, System.Globalization.NumberStyles.HexNumber,
                             CultureInfo.InvariantCulture, out v);
    }

    // ---------------- Dump discovery ----------------

    private static string ExpandDumpDir() =>
        Environment.ExpandEnvironmentVariables(DefaultDumpDir);

    private static FileInfo[] DiscoverDumps()
    {
        var dir = ExpandDumpDir();
        if (!Directory.Exists(dir)) return Array.Empty<FileInfo>();
        return new DirectoryInfo(dir)
            .GetFiles("*.dmp")
            .OrderByDescending(f => f.LastWriteTimeUtc)
            .ToArray();
    }

    private static int RunList()
    {
        var dumps = DiscoverDumps();
        if (dumps.Length == 0)
        {
            Console.WriteLine($"No .dmp files in {ExpandDumpDir()}");
            return 0;
        }
        Console.WriteLine($"Dumps in {ExpandDumpDir()} (newest first):");
        Console.WriteLine();
        foreach (var f in dumps)
        {
            var sizeMb = f.Length / (1024.0 * 1024.0);
            Console.WriteLine($"  {f.LastWriteTimeUtc:yyyy-MM-dd HH:mm:ss} UTC  {sizeMb,6:F1} MB  {f.Name}");
        }
        return 0;
    }

    // ---------------- Main entry ----------------

    private static int RunAnalyze(string? dumpArg, int depth, int stackBytes)
    {
        var config = KdevConfig.LoadOrPrintErrors(out var exitCode);
        if (config is null) return exitCode;

        // Resolve dump path: explicit arg wins, else newest under %LOCALAPPDATA%\CrashDumps.
        FileInfo? dumpFile;
        if (!string.IsNullOrEmpty(dumpArg))
        {
            if (!File.Exists(dumpArg))
            {
                Console.Error.WriteLine($"ERROR: dump file not found: {dumpArg}");
                return 2;
            }
            dumpFile = new FileInfo(dumpArg);
        }
        else
        {
            var dumps = DiscoverDumps();
            if (dumps.Length == 0)
            {
                Console.Error.WriteLine($"ERROR: no .dmp files in {ExpandDumpDir()}, and no path given.");
                return 2;
            }
            dumpFile = dumps[0];
            Console.WriteLine($"(no dump path given — using newest: {dumpFile.Name})");
            Console.WriteLine();
        }

        // Locate the Ghidra XML. Skipping resolution is OK — addresses just won't
        // be named — but we warn loudly so the operator notices.
        var xmlPath = Path.Combine(config.ProjectRoot, DefaultGhidraXml);
        FunctionTable? table = null;
        if (File.Exists(xmlPath))
        {
            Console.WriteLine($"Loading function table from {xmlPath}...");
            var t0 = DateTime.UtcNow;
            table = FunctionTable.LoadFromGhidraXml(xmlPath);
            var elapsed = (DateTime.UtcNow - t0).TotalSeconds;
            Console.WriteLine($"  {table.Count} functions, image base 0x{table.ImageBase:x8} ({elapsed:F2}s)");
            Console.WriteLine();
        }
        else
        {
            Console.Error.WriteLine($"WARNING: Ghidra XML not found at {xmlPath}");
            Console.Error.WriteLine("         Addresses will not be resolved to function names.");
            Console.WriteLine();
        }

        return AnalyzeOne(dumpFile, table, depth, stackBytes);
    }

    // ---------------- Single-dump analysis ----------------

    private static int AnalyzeOne(FileInfo dump, FunctionTable? table, int depth, int stackBytes)
    {
        Console.WriteLine($"Dump:        {dump.Name}  ({dump.LastWriteTimeUtc:yyyy-MM-dd HH:mm:ss} UTC)");
        Console.WriteLine($"Path:        {dump.FullName}");
        Console.WriteLine($"Size:        {dump.Length / (1024.0 * 1024.0):F1} MB");
        Console.WriteLine();

        // Step 1: parse the minidump header directly for the exception stream.
        // ClrMD's DataTarget exposes threads + memory but not the exception
        // record. Format is stable + documented; ~50 lines does it.
        ExceptionRecord? exc = ReadExceptionStream(dump.FullName);
        if (exc is null)
        {
            Console.Error.WriteLine("WARNING: dump has no exception stream (was it a hang dump, not a crash dump?).");
            Console.WriteLine();
        }
        else
        {
            var e = exc.Value;
            var codeStr = ExceptionCodeName(e.Code);
            var avRw = e.Code == 0xC0000005 && e.NumParams >= 2
                ? AccessViolationKind(e.Param0)
                : "";
            Console.WriteLine("Exception");
            Console.WriteLine($"  code:           0x{e.Code:x8}  {codeStr}{avRw}");
            Console.WriteLine($"  faulting addr:  0x{e.FaultingAddress:x8}{ResolveSuffix(table, (uint)e.FaultingAddress)}");
            Console.WriteLine($"  exception EIP:  0x{e.ExceptionEip:x8}{ResolveSuffix(table, e.ExceptionEip)}");
            Console.WriteLine($"  thread id:      {e.ThreadId}");
            Console.WriteLine();
        }

        // Step 2: open via ClrMD for thread context + memory access. We don't
        // need ClrR (managed runtime) — we only use DataReader directly.
        using var dt = DataTarget.LoadDump(dump.FullName);
        var reader = dt.DataReader;

        // Thread enumeration lives on a sub-interface in ClrMD 3.x. Cast or
        // explain why we can't.
        if (reader is not IThreadReader threadReader)
        {
            Console.Error.WriteLine("ERROR: DataReader does not expose IThreadReader (unexpected for a minidump).");
            return 4;
        }
        var threadIds = threadReader.EnumerateOSThreadIds().ToArray();
        Console.WriteLine($"Threads:     {threadIds.Length}");

        // Pick the crashing thread: prefer the one named in the exception
        // record; otherwise fall back to the first thread.
        uint crashingTid = exc?.ThreadId ?? (threadIds.Length > 0 ? threadIds[0] : 0);
        if (!threadIds.Contains(crashingTid))
        {
            Console.Error.WriteLine($"WARNING: exception thread id {crashingTid} not in thread list; " +
                                    $"falling back to first ({threadIds[0]}).");
            crashingTid = threadIds[0];
        }
        Console.WriteLine($"Crashing thread: TID {crashingTid}");
        Console.WriteLine();

        // Step 3: read x86 CONTEXT for that thread. PREFER the saved context
        // from the exception stream — that's the register state at fault time,
        // before the OS unwound into the exception dispatcher. The live thread
        // context (via ClrMD's GetThreadContext) at this point is deep inside
        // ntdll/kernel32 exception machinery and useless for stack-walking
        // back into the crashing engine code.
        Ctx32 ctx;
        string ctxSource;
        if (exc?.SavedContext is { } savedCtxBytes)
        {
            ctx = Ctx32.From(savedCtxBytes);
            ctxSource = "saved (fault time)";
        }
        else
        {
            // CONTEXT_FULL on x86 = CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS
            const uint ContextFullX86 = 0x10007;
            var ctxBytes = new byte[Ctx32.Size];
            if (!reader.GetThreadContext(crashingTid, ContextFullX86, ctxBytes))
            {
                Console.Error.WriteLine("ERROR: GetThreadContext failed for crashing thread.");
                return 4;
            }
            ctx = Ctx32.From(ctxBytes);
            ctxSource = "live (post-unwind)";
        }
        Console.WriteLine($"Context source: {ctxSource}");
        Console.WriteLine();

        // Step 4: register dump.
        Console.WriteLine("Registers");
        Console.WriteLine($"  EAX=0x{ctx.Eax:x8}  EBX=0x{ctx.Ebx:x8}  ECX=0x{ctx.Ecx:x8}  EDX=0x{ctx.Edx:x8}");
        Console.WriteLine($"  ESI=0x{ctx.Esi:x8}  EDI=0x{ctx.Edi:x8}  EBP=0x{ctx.Ebp:x8}  ESP=0x{ctx.Esp:x8}");
        Console.WriteLine($"  EIP=0x{ctx.Eip:x8}  EFLAGS=0x{ctx.EFlags:x8}");
        Console.WriteLine();

        // Resolve every register that looks like a code pointer. Surfaces
        // stale function pointers cached in callee-saved registers.
        if (table is not null)
        {
            Console.WriteLine("Register resolution (best-effort):");
            ResolveAndPrint("EIP", ctx.Eip, table);
            ResolveAndPrint("EAX", ctx.Eax, table);
            ResolveAndPrint("EBX", ctx.Ebx, table);
            ResolveAndPrint("ECX", ctx.Ecx, table);
            ResolveAndPrint("EDX", ctx.Edx, table);
            ResolveAndPrint("ESI", ctx.Esi, table);
            ResolveAndPrint("EDI", ctx.Edi, table);
            Console.WriteLine();
        }

        // Step 5: EBP-chain walk.
        Console.WriteLine($"Stack walk (EBP chain, max {depth} frames)");
        Console.WriteLine($"  [00] 0x{ctx.Eip:x8}  {Resolve(table, ctx.Eip)}");
        var ebp = ctx.Ebp;
        for (int frame = 1; frame <= depth; frame++)
        {
            // Frame layout: [ebp] = saved old_ebp; [ebp+4] = return address.
            if (ebp == 0 || ebp == 0xFFFFFFFF) { Console.WriteLine($"  (chain ended: ebp=0x{ebp:x8})"); break; }
            if (!TryReadU32(reader, ebp, out var savedEbp) ||
                !TryReadU32(reader, ebp + 4, out var retAddr))
            {
                Console.WriteLine($"  (read failed at ebp=0x{ebp:x8})"); break;
            }
            if (retAddr == 0) { Console.WriteLine($"  (returned to 0)"); break; }
            Console.WriteLine($"  [{frame:D2}] 0x{retAddr:x8}  {Resolve(table, retAddr)}");
            // Detect EBP cycles (rare but possible with corrupt frame).
            if (savedEbp <= ebp) { Console.WriteLine($"  (ebp not increasing: 0x{ebp:x8} -> 0x{savedEbp:x8}; stopping)"); break; }
            ebp = savedEbp;
        }
        Console.WriteLine();

        // Step 6: ESP-region scan for code-pointer-shaped values. Catches
        // return addresses missed by EBP-walk when the frame chain breaks
        // (FPO, corrupt frame, leaf function before the crash).
        Console.WriteLine($"Stack scan (ESP..ESP+{stackBytes}, code-shaped values only)");
        var stackBuf = new byte[stackBytes];
        int read = ReadAvailable(reader, ctx.Esp, stackBuf);
        if (read <= 0)
        {
            Console.WriteLine("  (could not read stack memory)");
        }
        else
        {
            int hits = 0;
            for (int off = 0; off + 4 <= read; off += 4)
            {
                uint v = BitConverter.ToUInt32(stackBuf, off);
                if (table?.Find(v) is { } e)
                {
                    var slotOffset = (uint)(v - e.Start);
                    Console.WriteLine($"  ESP+0x{off:x3}  0x{v:x8}  {e.Name}+0x{slotOffset:x}");
                    hits++;
                }
            }
            if (hits == 0) Console.WriteLine("  (no code-shaped 4-byte values found)");
        }
        Console.WriteLine();

        // Step 7: top-of-stack hex dump.
        Console.WriteLine($"Top of stack (hex, {read} bytes from 0x{ctx.Esp:x8})");
        DumpHex(stackBuf, read, ctx.Esp);

        return 0;
    }

    // ---------------- Helpers ----------------

    private static void ResolveAndPrint(string reg, uint value, FunctionTable table)
    {
        var entry = table.Find(value);
        if (entry is { } e)
        {
            var off = value - e.Start;
            Console.WriteLine($"  {reg}=0x{value:x8}  {e.Name}+0x{off:x}");
        }
    }

    private static string Resolve(FunctionTable? table, uint addr) =>
        table is null ? "" : table.Resolve(addr);

    private static string ResolveSuffix(FunctionTable? table, uint addr)
    {
        if (table is null) return "";
        var r = table.Resolve(addr);
        return $"  {r}";
    }

    private static bool TryReadU32(IDataReader reader, uint addr, out uint value)
    {
        Span<byte> buf = stackalloc byte[4];
        int n = reader.Read(addr, buf);
        if (n != 4) { value = 0; return false; }
        value = BitConverter.ToUInt32(buf);
        return true;
    }

    private static int ReadAvailable(IDataReader reader, uint addr, byte[] buf)
    {
        // Read in 4KB chunks so partial misses don't kill the whole window
        // (pages near the top of the stack may be unmapped).
        int total = 0;
        const int chunk = 0x1000;
        while (total < buf.Length)
        {
            int want = Math.Min(chunk, buf.Length - total);
            int n = reader.Read(addr + (uint)total, buf.AsSpan(total, want));
            if (n <= 0) break;
            total += n;
            if (n < want) break;  // short read = boundary; stop here
        }
        return total;
    }

    private static void DumpHex(byte[] buf, int count, uint baseAddr)
    {
        for (int row = 0; row < count; row += 16)
        {
            int len = Math.Min(16, count - row);
            var hex = new System.Text.StringBuilder();
            var asc = new System.Text.StringBuilder();
            for (int i = 0; i < 16; i++)
            {
                if (i < len)
                {
                    byte b = buf[row + i];
                    hex.Append($"{b:x2} ");
                    asc.Append(b >= 0x20 && b < 0x7f ? (char)b : '.');
                }
                else
                {
                    hex.Append("   ");
                    asc.Append(' ');
                }
                if (i == 7) hex.Append(' ');
            }
            Console.WriteLine($"  0x{baseAddr + row:x8}  {hex} {asc}");
        }
    }

    // ---------------- Minidump exception-stream reader ----------------

    private readonly record struct ExceptionRecord(
        uint ThreadId,
        uint Code,
        uint ExceptionEip,
        uint NumParams,
        ulong Param0,            // for ACCESS_VIOLATION: 0=read 1=write 8=DEP
        ulong FaultingAddress,   // for ACCESS_VIOLATION: faulting VA
        byte[]? SavedContext);   // CONTEXT at fault time (pre-unwind), or null

    private const uint StreamTypeException = 6;

    private static ExceptionRecord? ReadExceptionStream(string path)
    {
        using var fs = File.OpenRead(path);
        using var br = new BinaryReader(fs);

        // MINIDUMP_HEADER
        var sig = br.ReadUInt32();
        if (sig != 0x504D444D) return null;            // 'MDMP'
        br.ReadUInt32();                                // version
        var nStreams = br.ReadUInt32();
        var dirRva = br.ReadUInt32();

        // Iterate directory looking for the exception stream.
        for (uint i = 0; i < nStreams; i++)
        {
            fs.Seek(dirRva + i * 12L, SeekOrigin.Begin);
            var streamType = br.ReadUInt32();
            var dataSize = br.ReadUInt32();
            var streamRva = br.ReadUInt32();
            if (streamType != StreamTypeException) continue;
            if (dataSize < 24) return null;             // malformed

            // MINIDUMP_EXCEPTION_STREAM:
            //   ThreadId                u32   +0x00
            //   __alignment             u32   +0x04
            //   ExceptionRecord:                +0x08
            //     ExceptionCode         u32   +0x08
            //     ExceptionFlags        u32   +0x0C
            //     ExceptionRecord       u64   +0x10  (chained, ignored)
            //     ExceptionAddress      u64   +0x18  (eip when crash hit)
            //     NumberParameters      u32   +0x20
            //     __unusedAlignment     u32   +0x24
            //     ExceptionInformation  u64[15]  +0x28 ... +0x9C
            //   ThreadContext           location descriptor  +0xA0
            //     DataSize              u32   +0xA0
            //     Rva                   u32   +0xA4
            //
            // ThreadContext.Rva points to the saved CONTEXT at fault time —
            // ESP/EBP/EIP from BEFORE the OS unwound into the exception
            // dispatcher. The live thread context (read via ClrMD's
            // GetThreadContext) is post-fault and useless for stack walking
            // back into the crashing engine code.
            fs.Seek(streamRva, SeekOrigin.Begin);
            var threadId = br.ReadUInt32();
            br.ReadUInt32();
            var code = br.ReadUInt32();
            br.ReadUInt32();
            br.ReadUInt64();
            var excAddr = br.ReadUInt64();
            var nParams = br.ReadUInt32();
            br.ReadUInt32();
            var p0 = br.ReadUInt64();
            var p1 = br.ReadUInt64();
            // Skip remaining 13 ExceptionInformation slots to reach ThreadContext.
            for (int k = 0; k < 13; k++) br.ReadUInt64();
            var ctxDataSize = br.ReadUInt32();
            var ctxRva = br.ReadUInt32();

            byte[]? ctx = null;
            if (ctxDataSize >= Ctx32.Size)
            {
                fs.Seek(ctxRva, SeekOrigin.Begin);
                ctx = br.ReadBytes((int)Ctx32.Size);
                if (ctx.Length != Ctx32.Size) ctx = null;
            }

            return new ExceptionRecord(
                ThreadId: threadId,
                Code: code,
                ExceptionEip: (uint)excAddr,
                NumParams: nParams,
                Param0: p0,
                FaultingAddress: p1,
                SavedContext: ctx);
        }

        return null;
    }

    private static string ExceptionCodeName(uint code) => code switch
    {
        0xC0000005 => "ACCESS_VIOLATION",
        0xC000001D => "ILLEGAL_INSTRUCTION",
        0xC0000094 => "INTEGER_DIVIDE_BY_ZERO",
        0xC0000096 => "PRIV_INSTRUCTION",
        0x80000003 => "BREAKPOINT",
        0xC00000FD => "STACK_OVERFLOW",
        _ => $"({code.ToString("x8", CultureInfo.InvariantCulture)})",
    };

    private static string AccessViolationKind(ulong rwFlag) => rwFlag switch
    {
        0 => " read",
        1 => " write",
        8 => " DEP/execute",
        _ => $" rw=0x{rwFlag:x}",
    };

    // ---------------- x86 CONTEXT layout ----------------

    private readonly struct Ctx32
    {
        public const int Size = 716;

        public readonly uint Edi, Esi, Ebx, Edx, Ecx, Eax, Ebp, Eip, EFlags, Esp;

        private Ctx32(uint edi, uint esi, uint ebx, uint edx, uint ecx, uint eax,
                      uint ebp, uint eip, uint eflags, uint esp)
        {
            Edi = edi; Esi = esi; Ebx = ebx; Edx = edx; Ecx = ecx; Eax = eax;
            Ebp = ebp; Eip = eip; EFlags = eflags; Esp = esp;
        }

        public static Ctx32 From(byte[] buf)
        {
            // CONTEXT_X86 fixed offsets (see SDK winnt.h):
            //   ContextFlags +0x000  Dr0..Dr7 +0x004  FloatSave +0x01c (112B)
            //   SegGs/Fs/Es/Ds +0x08c..+0x098
            //   Edi +0x09c  Esi +0x0a0  Ebx +0x0a4  Edx +0x0a8
            //   Ecx +0x0ac  Eax +0x0b0  Ebp +0x0b4  Eip +0x0b8
            //   SegCs +0x0bc  EFlags +0x0c0  Esp +0x0c4  SegSs +0x0c8
            //   ExtendedRegisters +0x0cc (512B); total 0x2cc = 716.
            uint U(int o) => BitConverter.ToUInt32(buf, o);
            return new Ctx32(
                edi: U(0x9C), esi: U(0xA0), ebx: U(0xA4), edx: U(0xA8),
                ecx: U(0xAC), eax: U(0xB0), ebp: U(0xB4), eip: U(0xB8),
                eflags: U(0xC0), esp: U(0xC4));
        }
    }
}
