using System;
using Avalonia.Controls;
using Avalonia.Platform;

namespace KPatchLauncher.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        TrySetWindowIcon();
    }

    private void TrySetWindowIcon()
    {
        try
        {
            using var stream = AssetLoader.Open(new Uri("avares://KPatchLauncher/Assets/icon.ico"));
            Icon = new WindowIcon(stream);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to load window icon: {ex.Message}");
        }
    }
}
