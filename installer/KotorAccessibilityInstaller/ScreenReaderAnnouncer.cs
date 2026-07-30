using System;
using System.Windows.Forms;
using System.Windows.Forms.Automation;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Speaks a status update through the screen reader.
    ///
    /// WinForms Labels are not UIA live regions, so writing `label.Text` is
    /// invisible to NVDA / JAWS / Narrator until the user navigates to the
    /// label. For an installer that is the whole game: progress, errors and
    /// "why is Install disabled" all land in a status label the user has no
    /// reason to be sitting on. Raising a UIA notification is what makes them
    /// audible.
    ///
    /// This block used to be copy-pasted into five forms, byte-identical every
    /// time. The copies that were never made are what caused bugs F3 and F5 —
    /// MainForm.ValidatePath wrote a red "wrong folder" message and silently
    /// disabled Install, and UninstallForm reported no progress at all. A form
    /// that forgets to duplicate a block gets silence, and silence is exactly
    /// the failure a blind user cannot notice. One call site is harder to
    /// forget than five.
    ///
    /// The per-form UpdateStatus wrappers are deliberately NOT folded in here:
    /// they differ in ways that matter — Invoke (synchronous) vs BeginInvoke,
    /// whether the message is also written to the install log, and whether the
    /// caller can suppress the announcement.
    /// </summary>
    public static class ScreenReaderAnnouncer
    {
        /// <summary>
        /// Announce <paramref name="message"/> on <paramref name="source"/>.
        ///
        /// MostRecent processing means a fresh update interrupts a pending one,
        /// which matters during the K1CP step where heartbeat and stdout
        /// forwards can fire in quick succession.
        ///
        /// Never throws: on older Windows, or with no UIA provider at runtime,
        /// it degrades to a log line. A missing announcement must not take down
        /// an install or an uninstall.
        /// </summary>
        public static void Announce(Control source, string message)
        {
            if (source == null) return;
            try
            {
                source.AccessibilityObject?.RaiseAutomationNotification(
                    AutomationNotificationKind.ActionCompleted,
                    AutomationNotificationProcessing.MostRecent,
                    message);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not raise automation notification: {ex.Message}");
            }
        }
    }
}
