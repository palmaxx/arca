using Microsoft.UI.Xaml;

namespace Arca;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Plain CLI arg (unpackaged): arca.exe <file> auto-loads it.
        string[] cli = Environment.GetCommandLineArgs();
        string? startupFile = cli.Length > 1 && File.Exists(cli[1]) ? cli[1] : null;

        _window = new MainWindow(startupFile);
        _window.Activate();
    }
}
