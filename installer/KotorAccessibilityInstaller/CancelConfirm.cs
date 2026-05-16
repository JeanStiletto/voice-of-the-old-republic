using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Helper for the standard "Are you sure you want to cancel the installation?"
    /// confirmation prompt. Used by every installer form so closing via the X
    /// button or a Cancel/Back button asks the user first instead of silently
    /// terminating the install flow.
    /// </summary>
    internal static class CancelConfirm
    {
        /// <summary>
        /// Show the cancel-confirm dialog. Returns true when the user confirms
        /// they want to cancel (so the caller should let the close proceed),
        /// false when the user wants to stay in the installer.
        ///
        /// Default button is No, so an absent-minded Enter press does NOT
        /// silently kill the installation.
        /// </summary>
        public static bool ConfirmCancel(IWin32Window owner = null)
        {
            var result = MessageBox.Show(
                owner,
                InstallerLocale.Get("Common_CancelConfirm_Text"),
                InstallerLocale.Get("Common_CancelConfirm_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question,
                MessageBoxDefaultButton.Button2);

            return result == DialogResult.Yes;
        }
    }
}
