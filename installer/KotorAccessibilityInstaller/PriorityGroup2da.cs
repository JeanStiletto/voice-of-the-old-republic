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
        /// <summary>Fingerprint stamped into the appended row's FadeTime column.</summary>
        public const ushort SentinelFadeTime = 31337;

        // Appended row values by column. "label" is empty (every row's is).
        // Clone of vanilla group 0 (priority 0, 4 voices, 20m/10m falloff, no
        // playback variance) but Volume=127 (full) and FadeTime=sentinel.
        // Interrupt=1 so a fresh cue preempts an old one when the voice budget
        // is full rather than being dropped.
        private static readonly Dictionary<string, string> NewRow =
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

        /// <summary>True if the table already carries our sentinel row.</summary>
        public static bool HasAccGroup(byte[] buf)
        {
            var t = Parse(buf);
            int fc = FadeTimeCol(t);
            if (fc < 0) return false;
            string s = SentinelFadeTime.ToString();
            for (int r = 0; r < t.RowCount; r++)
                if (Cell(t, r, fc) == s) return true;
            return false;
        }

        /// <summary>
        /// Returns the 2da bytes with our full-volume row appended. Idempotent:
        /// returns <paramref name="source"/> unchanged if the sentinel row is
        /// already present. Throws on a malformed/unrecognised 2da.
        /// </summary>
        public static byte[] AppendAccGroup(byte[] source)
        {
            if (HasAccGroup(source)) return source;

            var t = Parse(source);
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
                NewRow.TryGetValue(c, out string val);
                val ??= "";  // unknown column -> empty cell (defensive, forward-compat)
                int off = data.Count;
                data.AddRange(Encoding.ASCII.GetBytes(val));
                data.Add(0);
                t.Offsets.Add((ushort)off);
            }

            t.RowCount += 1;
            t.Labels.Add(newIdx.ToString());
            t.DataBlock = data.ToArray();

            byte[] outBytes = Build(t);

            // Self-check: the freshly written row must read back as our group.
            if (!HasAccGroup(outBytes))
                throw new InvalidDataException(
                    "prioritygroups.2da append self-check failed (sentinel not found in output)");
            return outBytes;
        }
    }
}
