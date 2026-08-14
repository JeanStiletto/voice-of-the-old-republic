using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// The running event log shown on the long install screens: what has
    /// happened so far, one entry per thing that happened.
    ///
    /// The status label says only what is happening *right now* and is
    /// overwritten by the next step. On a screen that can sit in one phase for
    /// minutes — the 138 MB TSLRCM download, a HoloPatcher run, waiting on Steam
    /// for a 335 MB Workshop item — that leaves the user with no way to tell a
    /// slow step from a stuck one, and no way to see what already went past.
    /// This keeps the history in view while the work runs.
    ///
    /// A ListBox rather than a multiline TextBox: the content is a list of
    /// discrete steps, and a ListBox reads back one step per arrow key. A
    /// read-only TextBox announces itself as an edit field and hands the user a
    /// block of text to navigate by line and character instead.
    ///
    /// <para><b>Self-updating rows.</b> Passing a <c>key</c> to
    /// <see cref="Report"/> makes a line REPLACE the previous line with the same
    /// key instead of adding another. That is what keeps a percent-by-percent
    /// download, or a heartbeat that ticks every two seconds, to a single row
    /// that counts up — TSLRCM's download alone would otherwise add a hundred
    /// rows, and the Workshop wait several hundred.</para>
    ///
    /// <para><b>Keys are authored by the caller, never parsed out of the
    /// text.</b> The text carries elapsed seconds or a byte count and therefore
    /// changes on every tick, so deriving a key from it would start a fresh row
    /// each time and rebuild the exact flood this exists to prevent.</para>
    ///
    /// <para>Rows are only ever replaced, never removed, so a recorded index
    /// stays valid for the life of the run.</para>
    ///
    /// <para>This does NOT speak. Announcing stays with the callers' existing
    /// <c>UpdateStatus</c> / <see cref="ScreenReaderAnnouncer"/> path, which
    /// already decides what is worth interrupting the user for — a log that
    /// also announced would double every message.</para>
    /// </summary>
    public class EventLogView : ListBox
    {
        private readonly Dictionary<string, int> _rowByKey =
            new Dictionary<string, int>(StringComparer.Ordinal);

        public EventLogView()
        {
            // Long lines carry full paths and mod names; scrolling beats
            // clipping, because a truncated path in the log is a support problem.
            HorizontalScrollbar = true;
            IntegralHeight = false;
        }

        /// <summary>
        /// Appends <paramref name="text"/>, or replaces the existing row when
        /// <paramref name="key"/> has been seen before.
        ///
        /// Call on the UI thread. Every caller reaches this through its form's
        /// <c>UpdateStatus</c>, which has already marshalled.
        /// </summary>
        public void Report(string text, string key = null)
        {
            if (string.IsNullOrEmpty(text)) return;

            if (key != null && _rowByKey.TryGetValue(key, out int index) && index < Items.Count)
            {
                Items[index] = text;
                return;
            }

            Items.Add(text);
            if (key != null) _rowByKey[key] = Items.Count - 1;

            // Keep the newest line in view, but only when a row was ADDED: an
            // updating row must not drag the view away from wherever the user is
            // reading. SelectedIndex is deliberately left alone — moving it would
            // make the screen reader read the item on top of the spoken status.
            TopIndex = Math.Max(0, Items.Count - 1);
        }
    }
}
