using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Animation;

namespace Offnote.WinUI;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Title = "Offnote";
        AppWindow.Resize(new Windows.Graphics.SizeInt32(480, 338));
        if (AppWindow.Presenter is OverlappedPresenter presenter)
            presenter.SetBorderAndTitleBar(false, false);
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBar);
        SystemBackdrop = new Microsoft.UI.Xaml.Media.MicaBackdrop();
        Activated += (_, _) => NoteBox.Focus(Microsoft.UI.Xaml.FocusState.Programmatic);
        SaveButton.PointerEntered += (_, _) => SaveButton.Background = Brush("#8CA0FB");
        SaveButton.PointerExited += (_, _) => SaveButton.Background = Brush("#7389E8");
        SaveButton.PointerPressed += (_, _) => SaveButton.Background = Brush("#4E66C5");
        SaveButton.PointerReleased += (_, _) => SaveButton.Background = Brush("#8CA0FB");
        CloseButton.PointerEntered += (_, _) => CloseButton.Background = Brush("#B84758");
        CloseButton.PointerExited += (_, _) => CloseButton.Background = Brush("#1E212A");
    }

    private static Microsoft.UI.Xaml.Media.SolidColorBrush Brush(string hex) =>
        new(Windows.UI.Color.FromArgb(255,
            Convert.ToByte(hex.Substring(1, 2), 16),
            Convert.ToByte(hex.Substring(3, 2), 16),
            Convert.ToByte(hex.Substring(5, 2), 16)));

    private void Root_Loaded(object sender, RoutedEventArgs e) => Fade(0, 1, 180, null);

    private void Fade(double from, double to, int milliseconds, Action? finished)
    {
        var animation = new DoubleAnimation
        {
            From = from, To = to,
            Duration = new Duration(TimeSpan.FromMilliseconds(milliseconds)),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
        };
        Storyboard.SetTarget(animation, Root);
        Storyboard.SetTargetProperty(animation, "Opacity");
        var storyboard = new Storyboard();
        storyboard.Children.Add(animation);
        if (finished != null) storyboard.Completed += (_, _) => finished();
        storyboard.Begin();
    }

    private void Minimize_Click(object sender, RoutedEventArgs e)
    {
        if (AppWindow.Presenter is OverlappedPresenter presenter) presenter.Minimize();
    }
    private void Close_Click(object sender, RoutedEventArgs e) => Fade(1, 0, 130, Close);
    private void Save_Click(object sender, RoutedEventArgs e)
    {
        Status.Text = "Visual benchmark only — no note was saved";
    }
}
