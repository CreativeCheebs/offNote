using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace QuickNote;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var lifetime = new ClassicDesktopStyleApplicationLifetime
        {
            Args = args,
            ShutdownMode = ShutdownMode.OnExplicitShutdown,
        };
        AppBuilder.Configure<Application>()
            .UsePlatformDetect()
            // A single text box needs no GPU: software rendering avoids the ANGLE/D3D
            // device and DirectComposition surfaces, which dominate idle memory.
            .With(new Win32PlatformOptions
            {
                RenderingMode = [Win32RenderingMode.Software],
                CompositionMode = [Win32CompositionMode.RedirectionSurface],
            })
            .AfterSetup(builder => builder.Instance?.Styles.Add(new FluentTheme()))
            .SetupWithLifetime(lifetime);
        var window = new CaptureWindow();
        lifetime.MainWindow = window;
        var menu = new NativeMenu();
        var newNote = new NativeMenuItem("New note");
        newNote.Click += (_, _) => _ = window.ShowCaptureAsync();
        var quit = new NativeMenuItem("Quit");
        quit.Click += (_, _) => { window.Quit(); lifetime.Shutdown(); };
        menu.Items.Add(newNote);
        menu.Items.Add(quit);
        var tray = new TrayIcon
        {
            ToolTipText = "Offnote",
            Menu = menu,
            Icon = new WindowIcon(Path.Combine(AppContext.BaseDirectory, "app.ico")),
        };
        tray.Clicked += (_, _) => _ = window.ShowCaptureAsync();
        TrayIcon.SetIcons(Application.Current!, [tray]);
        lifetime.Start(args);
    }
}
