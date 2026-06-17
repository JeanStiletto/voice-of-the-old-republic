using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Appends the mod's dedicated full-volume audio priority group to
    /// <c>prioritygroups.2da</c>.
    ///
    /// <para>Why a new row instead of editing an existing group: the mod's
    /// audio cues need a Volume=127 (unity) group, but the legacy group they
    /// used (group 0) is Volume=106 (~83%). Editing an existing group would
    /// change every engine sound assigned to it. Appending a fresh row leaves
    /// all vanilla groups untouched.</para>
    ///
    /// <para>Index-drift safety: the row index can't be hard-coded — another
    /// mod that also extends this table could land its rows before ours. The
    /// accessibility DLL finds our group at runtime by a sentinel value in the
    /// <c>FadeTime</c> column (<see cref="SentinelFadeTime"/>; vanilla fade
    /// times are only 0 or 1000), so it works wherever the row ends up.</para>
    ///
    /// <para>2DA V2.b binary layout:
    /// <code>
    /// "2DA V2.b\n"
    /// &lt;col header&gt;\t ... \t \0
    /// uint32 row_count
    /// &lt;row label&gt;\t ...            (one tab-terminated token per row)
    /// uint16 cell_offset[row_count*col_count]   (offset into data block)
    /// uint16 data_size
    /// &lt;data block&gt;                  (NUL-terminated strings)
    /// </code>
    /// Append preserves every existing byte of the offset table and data block:
    /// row_count grows by one, one row label and col_count offsets are appended,
    /// the new cells' strings go on the end of the data block, and data_size is
    /// bumped. Existing offsets stay valid because the block only grows at the
    /// tail.</para>
    /// </summary>
    public static class PriorityGroup2da
    {
        /// <summary>Fingerprint stamped into the flat full-volume row's FadeTime column.</summary>
        public const ushort SentinelFadeTime = 31337;

        /// <summary>Fingerprint for the near-field "spatial" row (tight falloff band).</summary>
        public const ushort SpatialSentinelFadeTime = 31338;

        // Appended row values by column. "label" is empty (every row's is).
        //
        // Flat full-volume group: clone of vanilla group 0 (priority 0, 4 voices,
        // 10m full-volume radius / 20m floor, no playback variance) but Volume=127
        // (full) and FadeTime=sentinel. Interrupt=1 so a fresh cue preempts an old
        // one when the voice budget is full rather than being dropped. Used by the
        // on-demand cues (cycling, beacon, combat) that may target distant objects
        // and just need to stay audible at range.
        private static readonly Dictionary<string, string> FlatRow =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "priority", "0" },
                { "volume", "127" },
                { "maxplaying", "4" },
                { "interrupt", "1" },
                { "fadetime", SentinelFadeTime.ToString() },
                { "maxvolumedist", "10" },
                { "minvolumedist", "20" },
                { "playbackvariance", "0" },
            };

        // Near-field "spatial" group: same Volume=127 but a TIGHT falloff band —
        // full volume only within 1m, ramping to the floor by 8m. This restores a
        // distance-loudness gradient across the passive proximity cues' ~5m
        // awareness range (the flat group is full-volume out to 10m, so cues fired
        // within 5m never varied with distance). Mirrors the engine's own
        // near-field SFX groups (e.g. vanilla groups 19/20: full to 4m, floor 8m),
        // compressed tighter to fit our shorter awareness range. Used only by the
        // passive wall/door/container/NPC/item/transition cues that funnel through
        // PlayCueAtPosition. One-shot cues take their falloff band from their
        // priority group (the one-shot engine API has no per-source distance), so
        // a dedicated group is the only way to give them a band of their own.
        private static readonly Dictionary<string, string> SpatialRow =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "priority", "0" },
                { "volume", "127" },
                { "maxplaying", "4" },
                { "interrupt", "1" },
                { "fadetime", SpatialSentinelFadeTime.ToString() },
                { "maxvolumedist", "1" },
                { "minvolumedist", "8" },
                { "playbackvariance", "0" },
            };

        // Every accessibility row we maintain, paired with its sentinel.
        private static readonly (ushort Sentinel, Dictionary<string, string> Row)[] AccRows =
            new[]
            {
                (SentinelFadeTime, FlatRow),
                (SpatialSentinelFadeTime, SpatialRow),
            };

        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("2DA V2.b\n");

        private sealed class Table
        {
            public List<string> Cols;
            public int RowCount;
            public List<string> Labels;
            public List<ushort> Offsets;
            public byte[] DataBlock;
        }

        private static Table Parse(byte[] buf)
        {
            if (buf.Length < Magic.Length)
                throw new InvalidDataException("prioritygroups.2da too short");
            for (int i = 0; i < Magic.Length; i++)
                if (buf[i] != Magic[i])
                    throw new InvalidDataException("not a 2DA V2.b file");

            int p = Magic.Length;
            int hdrEnd = Array.IndexOf(buf, (byte)0, p);
            if (hdrEnd < 0) throw new InvalidDataException("2da header not NUL-terminated");
            var cols = new List<string>(
                Encoding.ASCII.GetString(buf, p, hdrEnd - p).Split('\t'));
            if (cols.Count > 0 && cols[cols.Count - 1].Length == 0)
                cols.RemoveAt(cols.Count - 1);  // trailing tab leaves an empty token

            p = hdrEnd + 1;
            int rowCount = BitConverter.ToInt32(buf, p);
            p += 4;

            var labels = new List<string>(rowCount);
            for (int r = 0; r < rowCount; r++)
            {
                int t = Array.IndexOf(buf, (byte)'\t', p);
                if (t < 0) throw new InvalidDataException("2da row label not terminated");
                labels.Add(Encoding.ASCII.GetString(buf, p, t - p));
                p = t + 1;
            }

            int ncells = rowCount * cols.Count;
            var offsets = new List<ushort>(ncells);
            for (int i = 0; i < ncells; i++) { offsets.Add(BitConverter.ToUInt16(buf, p)); p += 2; }

            int dataSize = BitConverter.ToUInt16(buf, p);
            p += 2;
            if (p + dataSize > buf.Length)
                throw new InvalidDataException("2da data block runs past EOF");
            var data = new byte[dataSize];
            Array.Copy(buf, p, data, 0, dataSize);

            return new Table { Cols = cols, RowCount = rowCount, Labels = labels,
                               Offsets = offsets, DataBlock = data };
        }

        private static byte[] Build(Table t)
        {
            using var ms = new MemoryStream();
            ms.Write(Magic, 0, Magic.Length);
            byte[] hdr = Encoding.ASCII.GetBytes(string.Join("\t", t.Cols) + "\t");
            ms.Write(hdr, 0, hdr.Length);
            ms.WriteByte(0);
            ms.Write(BitConverter.GetBytes(t.RowCount), 0, 4);
            foreach (var lbl in t.Labels)
            {
                byte[] lb = Encoding.ASCII.GetBytes(lbl);
                ms.Write(lb, 0, lb.Length);
                ms.WriteByte((byte)'\t');
            }
            foreach (var off in t.Offsets) ms.Write(BitConverter.GetBytes(off), 0, 2);
            ms.Write(BitConverter.GetBytes((ushort)t.DataBlock.Length), 0, 2);
            ms.Write(t.DataBlock, 0, t.DataBlock.Length);
            return ms.ToArray();
        }

        private static string Cell(Table t, int row, int colIdx)
        {
            int off = t.Offsets[row * t.Cols.Count + colIdx];
            int end = Array.IndexOf(t.DataBlock, (byte)0, off);
            return Encoding.ASCII.GetString(t.DataBlock, off, end - off);
        }

        private static int FadeTimeCol(Table t) =>
            t.Cols.FindIndex(c => c.Equals("fadetime", StringComparison.OrdinalIgnoreCase));

        /// <summary>True if the parsed table carries a row with the given sentinel.</summary>
        private static bool HasSentinel(Table t, int fadeCol, ushort sentinel)
        {
            if (fadeCol < 0) return false;
            string s = sentinel.ToString();
            for (int r = 0; r < t.RowCount; r++)
                if (Cell(t, r, fadeCol) == s) return true;
            return false;
        }

        /// <summary>True if the table already carries ALL of our sentinel rows.</summary>
        public static bool HasAccGroup(byte[] buf)
        {
            var t = Parse(buf);
            int fc = FadeTimeCol(t);
            if (fc < 0) return false;
            foreach (var (sentinel, _) in AccRows)
                if (!HasSentinel(t, fc, sentinel)) return false;
            return true;
        }

        // Appends one row (dictionary keyed by column name) to the parsed table.
        private static void AppendRow(Table t, Dictionary<string, string> row)
        {
            int newIdx = t.RowCount;
            var data = new List<byte>(t.DataBlock);

            foreach (var c in t.Cols)
            {
                if (c.Equals("label", StringComparison.OrdinalIgnoreCase))
                {
                    // Row label column is empty for every row -> the shared
                    // empty string at data offset 0.
                    t.Offsets.Add(0);
                    continue;
                }
                row.TryGetValue(c, out string? val);
                val ??= "";  // unknown column -> empty cell (defensive, forward-compat)
                int off = data.Count;
                data.AddRange(Encoding.ASCII.GetBytes(val));
                data.Add(0);
                t.Offsets.Add((ushort)off);
            }

            t.RowCount += 1;
            t.Labels.Add(newIdx.ToString());
            t.DataBlock = data.ToArray();
        }

        /// <summary>
        /// Returns the 2da bytes with every accessibility row appended that isn't
        /// already present. Idempotent: returns <paramref name="source"/> unchanged
        /// when all our rows are already there (so callers can detect "no change"
        /// via reference equality). Adds only the missing rows otherwise — an
        /// install that already has the flat row but not the spatial row gains just
        /// the spatial one. Throws on a malformed/unrecognised 2da.
        /// </summary>
        public static byte[] AppendAccGroup(byte[] source)
        {
            var t = Parse(source);
            int fc = FadeTimeCol(t);

            bool changed = false;
            foreach (var (sentinel, row) in AccRows)
            {
                if (HasSentinel(t, fc, sentinel)) continue;
                AppendRow(t, row);
                changed = true;
            }
            if (!changed) return source;

            byte[] outBytes = Build(t);

            // Self-check: every row must read back from the freshly written bytes.
            if (!HasAccGroup(outBytes))
                throw new InvalidDataException(
                    "prioritygroups.2da append self-check failed (a sentinel row is missing from output)");
            return outBytes;
        }
    }
}
