using System;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Buffers installation progress and writes it to the user's Desktop.
    ///
    /// <para><b>The log is always written, at the end of every run.</b> It used
    /// to be written only if the user said yes to a prompt — and that prompt was
    /// only offered when something had called <see cref="Error(string)"/>. A run
    /// that succeeded with warnings therefore produced no log at all, which is
    /// exactly the run you need one for: a warning is the symptom you cannot
    /// reproduce from the outside. Diagnosing a real "K2CP warned about
    /// something" report took a repo audit because of this.</para>
    ///
    /// <para>The previous run is kept as <c>.previous.log</c>. One fixed
    /// filename meant every run overwrote the last, so an uninstall could
    /// destroy the install log that explained why it was needed. Two files is
    /// enough to cover "it broke, then I tried again" without turning the
    /// Desktop into an archive.</para>
    /// </summary>
    public static class Logger
    {
        private static readonly string LogPath;
        private static readonly string PreviousLogPath;
        private static readonly StringBuilder LogBuffer = new StringBuilder();
        private static bool _hasErrors = false;

        static Logger()
        {
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
            LogPath = Path.Combine(desktop, "KotorAccessibility_Install.log");
            PreviousLogPath = Path.Combine(desktop, "KotorAccessibility_Install.previous.log");
        }

        public static string GetLogPath() => LogPath;
        public static bool HasErrors => _hasErrors;

        public static void Info(string message) => Log("INFO", message);
        public static void Warning(string message) => Log("WARN", message);

        public static void Error(string message)
        {
            Log("ERROR", message);
            _hasErrors = true;
        }

        public static void Error(string message, Exception ex)
        {
            Log("ERROR", $"{message}: {ex.Message}");
            Log("ERROR", $"Stack trace: {ex.StackTrace}");
            _hasErrors = true;
        }

        private static void Log(string level, string message)
        {
            string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
            string logLine = $"[{timestamp}] [{level}] {message}";
            LogBuffer.AppendLine(logLine);
            Console.WriteLine(logLine);
        }

        /// <summary>
        /// Write the buffered log, rotating the last one aside first. Safe to
        /// call repeatedly — later calls in a run simply rewrite the same file
        /// with more content, and rotation only happens on the first.
        /// </summary>
        public static void Flush()
        {
            try
            {
                RotateOnce();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to rotate previous log: {ex.Message}");
            }

            try
            {
                var fullLog = new StringBuilder();
                fullLog.AppendLine("===========================================");
                fullLog.AppendLine("Voice of the Old Republic - Installation Log");
                fullLog.AppendLine($"Date: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
                fullLog.AppendLine("===========================================");
                fullLog.AppendLine();
                fullLog.Append(LogBuffer);
                fullLog.AppendLine();
                fullLog.AppendLine("===========================================");
                fullLog.AppendLine("End of log");
                fullLog.AppendLine("===========================================");

                File.WriteAllText(LogPath, fullLog.ToString());
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to write log file: {ex.Message}");
            }
        }

        private static bool _rotated;

        /// <summary>
        /// Move an existing log to <c>.previous.log</c>, once per process.
        /// </summary>
        private static void RotateOnce()
        {
            if (_rotated) return;
            _rotated = true;

            if (!File.Exists(LogPath)) return;
            File.Copy(LogPath, PreviousLogPath, overwrite: true);
        }

        /// <summary>
        /// Write the log unconditionally, then offer to open it when something
        /// went wrong. The save is no longer the user's decision — the file is
        /// small, it lives on the Desktop under a predictable name, and a
        /// missing log costs far more than an unwanted one.
        /// </summary>
        public static bool AskAndSave(bool alwaysAsk = false)
        {
            Flush();

            // Nothing failed: the log is on disk if it is ever wanted, and there
            // is no reason to interrupt a successful install to say so.
            if (!alwaysAsk && !_hasErrors) return false;

            string message = _hasErrors
                ? InstallerLocale.Get("Logger_SaveLog_Errors")
                : InstallerLocale.Get("Logger_SaveLog_Normal");

            var result = MessageBox.Show(
                message,
                InstallerLocale.Get("Logger_SaveLog_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);

            return result == DialogResult.Yes;
        }

        public static void OpenLogFile()
        {
            try
            {
                if (File.Exists(LogPath))
                {
                    var psi = new System.Diagnostics.ProcessStartInfo(LogPath) { UseShellExecute = true };
                    System.Diagnostics.Process.Start(psi);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to open log file: {ex.Message}");
            }
        }
    }
}
