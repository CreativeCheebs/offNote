using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using Avalonia.Win32;
using System.Runtime.InteropServices;

namespace QuickNote;

internal sealed class CaptureWindow : Window
{
    private readonly TextBox _note;
    private readonly TextBlock _status;
    private readonly AppConfig _config;
    private bool _saving;
    private const string HintText = "ALT + SPACE  TO OPEN    ·    CTRL + ENTER  TO SAVE    ·    ESC  TO HIDE";
    private bool _discardArmed;
    private bool _quitting;
    private bool _hotkeyRegistered;
    private const int HotkeyId = 1;
    private const int WmHotkey = 0x0312;
    private const uint ModAlt = 0x0001, ModNoRepeat = 0x4000, VkSpace = 0x20;

    [DllImport("user32.dll")] private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")] private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("psapi.dll")] private static extern bool EmptyWorkingSet(IntPtr process);

    public CaptureWindow()
    {
        _config = AppConfig.Load(AppPaths.LiveConfigPath());
        Title = "Offnote";
        Width = 540;
        Height = 290;
        MinWidth = 430;
        MinHeight = 250;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        WindowDecorations = Avalonia.Controls.WindowDecorations.None;
        Background = Brush("#15171D");

        var shell = new Grid();
        var content = new Grid
        {
            Margin = new Thickness(23, 21),
            RowDefinitions = new RowDefinitions("Auto,*,Auto"),
            RowSpacing = 14,
        };
        var header = new StackPanel { Spacing = 3 };
        header.Children.Add(new TextBlock
        {
            Text = "Catch the thought.",
            FontSize = 22,
            FontWeight = FontWeight.SemiBold,
            Foreground = Brush("#F6F7FB"),
        });
        header.Children.Add(new TextBlock
        {
            Text = "QUICK CAPTURE",
            FontSize = 11,
            FontWeight = FontWeight.SemiBold,
            Foreground = Brush("#8296E8"),
        });
        content.Children.Add(header);

        _note = new TextBox
        {
            AcceptsReturn = true,
            TextWrapping = TextWrapping.Wrap,
            PlaceholderText = "What's on your mind? Add #tags to route it.",
            FontSize = 16,
            Background = Brush("#20242E"),
            Foreground = Brush("#F2F3F7"),
            BorderBrush = Brush("#393F4F"),
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(10),
            Padding = new Thickness(11),
        };
        _note.Resources["TextControlBackgroundFocused"] = Brush("#20242E");
        _note.Resources["TextControlBackgroundPointerOver"] = Brush("#20242E");
        _note.Resources["TextControlBorderBrushFocused"] = Brush("#8BA0F4");
        _note.Resources["TextControlBorderBrushPointerOver"] = Brush("#596278");
        _note.GotFocus += (_, _) => _note.BorderBrush = Brush("#8BA0F4");
        _note.LostFocus += (_, _) => _note.BorderBrush = Brush("#393F4F");
        AutomationProperties.SetName(_note, "Quick note");
        // Tunnel, so Ctrl+Enter / Esc are seen before the TextBox turns Enter into a newline.
        _note.AddHandler(KeyDownEvent, NoteKeyDown, Avalonia.Interactivity.RoutingStrategies.Tunnel);
        Grid.SetRow(_note, 1);
        content.Children.Add(_note);

        var footer = new Grid();
        _status = new TextBlock
        {
            Text = HintText,
            FontSize = 12,
            Foreground = Brush("#8D94A7"),
            VerticalAlignment = VerticalAlignment.Center,
        };
        footer.Children.Add(_status);
        Grid.SetRow(footer, 2);
        content.Children.Add(footer);
        shell.Children.Add(content);
        shell.Background = Brushes.Transparent;
        shell.PointerPressed += (_, e) =>
        {
            if (e.Source is not TextBox && e.GetCurrentPoint(shell).Properties.IsLeftButtonPressed) BeginMoveDrag(e);
        };
        Content = shell;
        Opened += async (_, _) =>
        {
            RegisterHotkey();
            await FadeInAsync();
        };
    }

    // Alt+Space toggles the window, as in the WinForms and Qt clients. The tray icon and
    // hotkey keep the process alive, so closing the window only hides it.
    private void RegisterHotkey()
    {
        var handle = TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
        if (handle == IntPtr.Zero) return;
        Win32Properties.AddWndProcHookCallback(this, (IntPtr _, uint msg, IntPtr wParam, IntPtr _, ref bool handled) =>
        {
            if (msg == WmHotkey && wParam.ToInt32() == HotkeyId)
            {
                handled = true;
                Dispatcher.UIThread.Post(Toggle);
            }
            return IntPtr.Zero;
        });
        _hotkeyRegistered = RegisterHotKey(handle, HotkeyId, ModAlt | ModNoRepeat, VkSpace);
        if (!_hotkeyRegistered) _status.Text = "Alt+Space unavailable; use the tray icon to open.";
    }

    public void Toggle()
    {
        if (IsVisible && WindowState != WindowState.Minimized) _ = HideToTrayAsync();
        else _ = ShowCaptureAsync();
    }

    public async Task ShowCaptureAsync()
    {
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Opacity = 0;
        Show();
        Activate();
        _note.Focus();
        await FadeInAsync();
    }

    private async Task HideToTrayAsync()
    {
        _discardArmed = false;
        await FadeOutAsync();
        Hide();
        // Give the idle process back its pages while it sits in the tray.
        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        EmptyWorkingSet(GetCurrentProcess());
    }

    public void Quit()
    {
        _quitting = true;
        if (_hotkeyRegistered)
        {
            var handle = TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
            if (handle != IntPtr.Zero) UnregisterHotKey(handle, HotkeyId);
        }
        Close();
    }

    protected override void OnClosing(WindowClosingEventArgs e)
    {
        if (!_quitting)
        {
            e.Cancel = true;
            _ = HideToTrayAsync();
        }
        base.OnClosing(e);
    }

    private async Task FadeInAsync()
    {
        _note.Focus();
        Opacity = 0;
        for (var i = 1; i <= 10; i++) { Opacity = i / 10.0; await Task.Delay(12); }
    }

    private async Task FadeOutAsync()
    {
        for (var i = 9; i >= 0; i--) { Opacity = i / 10.0; await Task.Delay(10); }
    }

    private static IBrush Brush(string hex) => new SolidColorBrush(Color.Parse(hex));

    private async void NoteKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            e.Handled = true;
            // Never silently drop typed text: a non-empty note needs a second Esc.
            if (!string.IsNullOrWhiteSpace(_note.Text) && !_discardArmed)
            {
                _discardArmed = true;
                _status.Text = "Press Esc again to discard this note";
                return;
            }
            _note.Text = "";
            await HideToTrayAsync();
            return;
        }
        _discardArmed = false;
        if (e.Key == Key.Enter && (e.KeyModifiers.HasFlag(KeyModifiers.Alt) || e.KeyModifiers.HasFlag(KeyModifiers.Control)))
        {
            e.Handled = true;
            await SaveAsync();
        }
    }

    private async Task SaveAsync()
    {
        if (_saving) return;
        var text = _note.Text?.Trim();
        if (string.IsNullOrWhiteSpace(text)) return;
        _saving = true;
        _status.Text = "Saving locally and delivering…";
        try
        {
            var outcome = await Task.Run(() => NoteRouter.Save(_config, text));
            _note.Text = "";
            await ShowSavedFeedbackAsync(outcome.Pending.Count > 0);
        }
        catch (Exception ex)
        {
            _status.Text = $"Save failed: {ex.Message}";
            _status.Foreground = Brush("#E5707E");
            _note.BorderBrush = Brush("#E5707E");
        }
        finally
        {
            _saving = false;
            _note.Focus();
        }
    }

    // Small, cheap confirmation: the note box flashes green (amber if delivery is still
    // pending) with a tiny pop, the status line says so, then the window tucks away.
    private async Task ShowSavedFeedbackAsync(bool deliveryPending)
    {
        var accent = Brush(deliveryPending ? "#E0B050" : "#5AE08A");
        _note.Resources["TextControlBorderBrushFocused"] = accent;
        _note.Resources["TextControlBorderBrushPointerOver"] = accent;
        _note.BorderBrush = accent;
        _status.Foreground = accent;
        _status.Text = deliveryPending ? "✓ Saved locally · delivery pending" : "✓ Saved";
        var scale = new ScaleTransform(1, 1);
        _note.RenderTransform = scale;
        _note.RenderTransformOrigin = RelativePoint.Center;
        double[] pop = [1.006, 1.012, 1.016, 1.012, 1.006, 1.0];
        foreach (var s in pop) { scale.ScaleX = scale.ScaleY = s; await Task.Delay(18); }
        await Task.Delay(650);
        _note.RenderTransform = null;
        _note.Resources["TextControlBorderBrushFocused"] = Brush("#8BA0F4");
        _note.Resources["TextControlBorderBrushPointerOver"] = Brush("#596278");
        _note.BorderBrush = _note.IsFocused ? Brush("#8BA0F4") : Brush("#393F4F");
        _status.Foreground = Brush("#8D94A7");
        _status.Text = HintText;
        if (IsVisible) await HideToTrayAsync();
    }
}
