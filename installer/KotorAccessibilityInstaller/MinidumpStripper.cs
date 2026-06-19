using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Shrinks a Windows minidump by dropping the captured memory of modules we
    /// never inspect during KOTOR triage (Windows + GPU-driver DLLs), while
    /// keeping everything our diagnostics actually use: swkotor.exe + our own
    /// DLLs (code+data), every thread stack, all crash-referenced heap, the
    /// thread contexts, the exception record, and the full module list.
    ///
    /// On a real ~151 MB WER dump (CustomDumpFlags 0x2141), ~145 MB is stock
    /// system/driver module code+data — reconstructible from disk and pure
    /// noise for us. Dropping it lands the dump at ~6 MB (then ~2-4 MB once
    /// 7-zipped), small enough to send over Discord without a file host.
    ///
    /// Layout it relies on (verified against WER's output for our flag set):
    /// memory blobs form a contiguous tail; the header, stream directory,
    /// module-name strings, thread contexts, exception context and the
    /// thread/memory descriptor arrays all sit in the front, below the first
    /// blob. So we copy the front verbatim, drop unwanted blobs from the tail,
    /// relocate the (now shorter) memory-descriptor list to the new tail, and
    /// patch just two things in the front: the MemoryList directory entry and
    /// each thread's stack RVA.
    ///
    /// Only the 32-bit MemoryListStream layout is handled (the one WER produces
    /// without MiniDumpWithFullMemory). A Memory64List, a missing stream, or any
    /// structural surprise throws NotSupportedException so the caller can fall
    /// back to shipping the original dump untouched.
    /// </summary>
    public static class MinidumpStripper
    {
        public sealed class Stats
        {
            public long OriginalBytes;
            public long StrippedBytes;
            public int KeptRanges;
            public int DroppedRanges;
            public long KeptMemoryBytes;
            public long DroppedMemoryBytes;
        }

        /// <summary>Modules whose code+data we keep. Lowercase, file name only.</summary>
        public static readonly string[] KeepModules =
        {
            "swkotor.exe", "kotorpatcher.dll", "dinput8.dll", "prism.dll", "sqlite3.dll",
        };

        private const uint Signature = 0x504D444D;   // 'MDMP'
        private const uint StreamThreadList = 3;
        private const uint StreamModuleList = 4;
        private const uint StreamMemoryList = 5;
        private const uint StreamMemory64List = 9;
        private const int ModuleRecordSize = 108;
        private const int ThreadRecordSize = 48;
        private const int MemoryDescriptorSize = 16;

        private readonly struct Module
        {
            public readonly ulong Base;
            public readonly ulong End;
            public readonly string Name;
            public Module(ulong b, ulong end, string name) { Base = b; End = end; Name = name; }
        }

        /// <summary>Read a dump, strip it, write the result. Returns size stats.</summary>
        public static Stats StripFile(string inputPath, string outputPath)
        {
            byte[] input = File.ReadAllBytes(inputPath);
            var stats = new Stats();
            byte[] output = Strip(input, stats);
            File.WriteAllBytes(outputPath, output);
            return stats;
        }

        public static byte[] Strip(byte[] input, Stats stats)
        {
            if (input.Length < 32 || U32(input, 0) != Signature)
                throw new NotSupportedException("not a minidump (bad signature)");

            uint nStreams = U32(input, 8);
            int dirRva = (int)U32(input, 12);

            int memDirEntry = -1;
            int memRva = 0, thrRva = 0, modRva = 0;
            for (uint i = 0; i < nStreams; i++)
            {
                int e = dirRva + (int)i * 12;
                uint type = U32(input, e);
                int rva = (int)U32(input, e + 8);
                if (type == StreamMemory64List)
                    throw new NotSupportedException("dump uses Memory64List (full-memory dump)");
                if (type == StreamMemoryList) { memDirEntry = e; memRva = rva; }
                else if (type == StreamThreadList) thrRva = rva;
                else if (type == StreamModuleList) modRva = rva;
            }
            if (memDirEntry < 0 || memRva == 0)
                throw new NotSupportedException("dump has no MemoryList stream");

            List<Module> modules = ReadModules(input, modRva);

            // --- Parse memory descriptors ---
            uint memCount = U32(input, memRva);
            int memBase = memRva + 4;
            var descStart = new ulong[memCount];
            var descSize = new uint[memCount];
            var descRva = new uint[memCount];
            for (uint r = 0; r < memCount; r++)
            {
                int o = memBase + (int)r * MemoryDescriptorSize;
                descStart[r] = U64(input, o);
                descSize[r] = U32(input, o + 8);
                descRva[r] = U32(input, o + 12);
            }

            // --- Front ends at the first memory blob (verified contiguous tail). ---
            uint blobStart = uint.MaxValue;
            for (uint r = 0; r < memCount; r++)
                if (descRva[r] != 0 && descRva[r] < blobStart) blobStart = descRva[r];
            // Thread stacks are memory blobs too; fold their RVAs into the floor.
            int threadCount = thrRva == 0 ? 0 : (int)U32(input, thrRva);
            int thrBase = thrRva + 4;
            for (int t = 0; t < threadCount; t++)
            {
                uint sRva = U32(input, thrBase + t * ThreadRecordSize + 36);
                if (sRva != 0 && sRva < blobStart) blobStart = sRva;
            }
            if (blobStart == uint.MaxValue || blobStart > input.Length)
                throw new NotSupportedException("could not locate memory-blob region");

            // --- Decide keep/drop per descriptor, build blob map (old RVA -> new RVA). ---
            var newRvaByOld = new Dictionary<uint, uint>();
            var blobOld = new List<uint>();
            var blobLen = new List<uint>();
            uint cursor = blobStart;
            int kept = 0, dropped = 0;
            long keptBytes = 0, droppedBytes = 0;

            void KeepBlob(uint oldRva, uint size)
            {
                if (oldRva == 0 || size == 0) return;
                if (newRvaByOld.ContainsKey(oldRva)) return;
                newRvaByOld[oldRva] = cursor;
                blobOld.Add(oldRva);
                blobLen.Add(size);
                cursor += size;
            }

            var keepDesc = new bool[memCount];
            for (uint r = 0; r < memCount; r++)
            {
                bool keep = ShouldKeep(descStart[r], modules);
                keepDesc[r] = keep;
                if (keep)
                {
                    kept++; keptBytes += descSize[r];
                    KeepBlob(descRva[r], descSize[r]);
                }
                else { dropped++; droppedBytes += descSize[r]; }
            }
            // Thread stacks are always kept (they are non-module memory and are
            // normally already in the kept set; this is belt-and-braces).
            for (int t = 0; t < threadCount; t++)
            {
                int rec = thrBase + t * ThreadRecordSize;
                uint sSize = U32(input, rec + 32);
                uint sRva = U32(input, rec + 36);
                KeepBlob(sRva, sSize);
            }

            uint newMemArrayRva = cursor;            // descriptor list moves to the tail
            uint newMemDataSize = 4 + (uint)kept * MemoryDescriptorSize;

            // --- Assemble: front (patched) + kept blobs + new descriptor list. ---
            byte[] front = new byte[blobStart];
            Array.Copy(input, 0, front, 0, blobStart);

            // Patch the MemoryList directory entry (DataSize @ +4, Rva @ +8).
            PutU32(front, memDirEntry + 4, newMemDataSize);
            PutU32(front, memDirEntry + 8, newMemArrayRva);
            // Patch each thread's stack RVA to its relocated position.
            for (int t = 0; t < threadCount; t++)
            {
                int rec = thrBase + t * ThreadRecordSize;
                uint sRva = U32(input, rec + 36);
                if (sRva != 0 && newRvaByOld.TryGetValue(sRva, out uint nr))
                    PutU32(front, rec + 36, nr);
            }

            using var ms = new MemoryStream();
            ms.Write(front, 0, front.Length);
            for (int i = 0; i < blobOld.Count; i++)
                ms.Write(input, (int)blobOld[i], (int)blobLen[i]);
            // New descriptor list: count, then kept descriptors with relocated RVAs.
            var arr = new byte[newMemDataSize];
            PutU32(arr, 0, (uint)kept);
            int w = 4;
            for (uint r = 0; r < memCount; r++)
            {
                if (!keepDesc[r] || descSize[r] == 0 || descRva[r] == 0) continue;
                PutU64(arr, w, descStart[r]);
                PutU32(arr, w + 8, descSize[r]);
                PutU32(arr, w + 12, newRvaByOld[descRva[r]]);
                w += MemoryDescriptorSize;
            }
            // kept count above includes any size==0/rva==0 ranges we skip when
            // writing; rewrite the real count so the list is self-consistent.
            int realKept = (w - 4) / MemoryDescriptorSize;
            PutU32(arr, 0, (uint)realKept);
            ms.Write(arr, 0, w);

            byte[] output = ms.ToArray();
            // Fix DataSize if realKept < kept (skipped empties): the directory
            // must match what we actually wrote.
            if (realKept != kept)
                PutU32(output, memDirEntry + 4, 4 + (uint)realKept * MemoryDescriptorSize);

            stats.OriginalBytes = input.Length;
            stats.StrippedBytes = output.Length;
            stats.KeptRanges = realKept;
            stats.DroppedRanges = dropped;
            stats.KeptMemoryBytes = keptBytes;
            stats.DroppedMemoryBytes = droppedBytes;
            return output;
        }

        private static bool ShouldKeep(ulong start, List<Module> modules)
        {
            foreach (var m in modules)
            {
                if (start >= m.Base && start < m.End)
                {
                    foreach (var keep in KeepModules)
                        if (string.Equals(keep, m.Name, StringComparison.OrdinalIgnoreCase))
                            return true;
                    return false;            // inside a module we don't care about
                }
            }
            return true;                     // not in any module: stack/heap/TEB -> keep
        }

        private static List<Module> ReadModules(byte[] input, int modRva)
        {
            var list = new List<Module>();
            if (modRva == 0) return list;
            uint count = U32(input, modRva);
            int baseOff = modRva + 4;
            for (uint m = 0; m < count; m++)
            {
                int mo = baseOff + (int)m * ModuleRecordSize;
                ulong imgBase = U64(input, mo);
                uint size = U32(input, mo + 8);
                int nameRva = (int)U32(input, mo + 20);
                string name = ReadMinidumpString(input, nameRva);
                list.Add(new Module(imgBase, imgBase + size, FileName(name)));
            }
            return list;
        }

        private static string ReadMinidumpString(byte[] input, int rva)
        {
            if (rva <= 0 || rva + 4 > input.Length) return string.Empty;
            uint byteLen = U32(input, rva);
            int start = rva + 4;
            if (byteLen == 0 || start + (int)byteLen > input.Length) return string.Empty;
            return Encoding.Unicode.GetString(input, start, (int)byteLen);
        }

        private static string FileName(string path)
        {
            if (string.IsNullOrEmpty(path)) return string.Empty;
            int slash = path.LastIndexOfAny(new[] { '\\', '/' });
            return slash >= 0 ? path.Substring(slash + 1) : path;
        }

        private static uint U32(byte[] b, int o) => BitConverter.ToUInt32(b, o);
        private static ulong U64(byte[] b, int o) => BitConverter.ToUInt64(b, o);
        private static void PutU32(byte[] b, int o, uint v) => Array.Copy(BitConverter.GetBytes(v), 0, b, o, 4);
        private static void PutU64(byte[] b, int o, ulong v) => Array.Copy(BitConverter.GetBytes(v), 0, b, o, 8);
    }
}
