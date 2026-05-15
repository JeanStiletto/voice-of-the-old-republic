using System;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Simple logger that buffers installation progress and optionally writes to file.
    /// </summary>
    public static class Logger
    {
        private static readonly string LogPath;
        private static readonly StringBuilder LogBuffer = new StringBuilder();
        private static bool _hasErrors = false;

        static Logger()
        {
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
            LogPath = Path.Combine(desktop, "KotorAccessibility_Install.log");
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

        public static void Flush()
        {
            try
            {
                var fullLog = new StringBuilder();
                fullLog.AppendLine("===========================================");
                fullLog.AppendLine("KOTOR Accessibility - Installation Log");
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

        public static bool AskAndSave(bool alwaysAsk = false)
        {
            if (!alwaysAsk && !_hasErrors) return false;

            string message = _hasErrors
                ? InstallerLocale.Get("Logger_SaveLog_Errors")
                : InstallerLocale.Get("Logger_SaveLog_Normal");

            var result = MessageBox.Show(
                message,
                InstallerLocale.Get("Logger_SaveLog_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);

            if (result == DialogResult.Yes)
            {
                Flush();
                return true;
            }
            return false;
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
