namespace QuickNote;

public sealed class NoteForm : Form
{
    private const int HotkeyId = 1;
    private const int CardPadding = 18;
    private const int FooterHeight = 34;

    private static readonly Color BackgroundColor = Color.FromArgb(24, 24, 32);
    private static readonly Color BorderColor = Color.FromArgb(58, 58, 74);
    private static readonly Color AccentColor = Color.FromArgb(124, 108, 246);
    private static readonly Color AccentHoverColor = Color.FromArgb(144, 130, 250);
    private static readonly Color TagColor = Color.FromArgb(179, 157, 255);
    private static readonly Color TextBoxBackColor = Color.FromArgb(32, 32, 42);
    private static readonly Color TextColor = Color.FromArgb(236, 236, 242);
    private static readonly Color HintColor = Color.FromArgb(188, 188, 202);
    private static readonly Color StatusOkColor = Color.FromArgb(140, 230, 170);
    private static readonly Color StatusDiscardColor = Color.FromArgb(240, 190, 120);
    private static readonly Color CelebrateGreen = Color.FromArgb(90, 224, 138);
    private static readonly Color CelebrateGold = Color.FromArgb(255, 209, 102);

    private const int SaveCloseDelayMs = 900;
    private const int DiscardCloseDelayMs = 450;
    private const float CelebrateDurationSeconds = 0.6f;

    private readonly Panel _textBoxBorder = new();
    private readonly RichTextBox _textBox = new();
    private bool _highlighting;
    private readonly Label _hintLabel = new();
    private readonly Button _saveButton = new();
    private readonly SparkleOverlay _sparkleOverlay = new();
    private readonly NotifyIcon _trayIcon = new();
    private readonly System.Windows.Forms.Timer _closeTimer = new() { Interval = DiscardCloseDelayMs };
    private readonly System.Windows.Forms.Timer _celebrateTimer = new() { Interval = 16 };
    private readonly List<Particle> _particles = new();
    private readonly Random _random = new();
    private Color _cardGlowColor;
    private float _celebrateElapsed;

    private const string HintText = "Ctrl/Alt+Enter to save · Esc keeps a #discarded draft";

    private sealed class Particle
    {
        public PointF Position;
        public PointF Velocity;
        public float Age;
        public float LifeSeconds;
        public float Size;
        public Color Color;
    }

    private readonly string _configPath;
    private AppConfig _config = new();
    private Shortcut _toggleShortcut;
    private Shortcut _saveShortcut;
    private bool _hotkeyRegistered;
    private bool _exiting;

    public NoteForm()
    {
        // Shared live config in %APPDATA%\QuickNote, seeded from the bundled
        // template on first run (same file the Tauri app uses).
        _configPath = AppPaths.LiveConfigPath();

        SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint | ControlStyles.ResizeRedraw, true);

        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        ShowInTaskbar = false;
        TopMost = true;
        Size = new Size(480, 260);
        BackColor = BackgroundColor;
        Padding = new Padding(1);
        _cardGlowColor = BorderColor;

        BuildLayout();
        TryLoadIcon();
        SetupTrayIcon();
        ReloadConfig();

        _closeTimer.Tick += (_, _) =>
        {
            _closeTimer.Stop();
            HideNote();
        };
        _celebrateTimer.Tick += CelebrateTimer_Tick;
    }

    private void BuildLayout()
    {
        var content = new Panel { Dock = DockStyle.Fill, Padding = new Padding(CardPadding), BackColor = BackgroundColor };
        Controls.Add(content);

        // Rows: textarea (fills remaining space) / spacer (equal to the card padding,
        // so the gap above the footer matches the gap on every other side) / footer.
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 3,
            BackColor = BackgroundColor,
        };
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, CardPadding));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, FooterHeight));
        content.Controls.Add(layout);

        _textBoxBorder.Dock = DockStyle.Fill;
        _textBoxBorder.BackColor = BorderColor;
        _textBoxBorder.Padding = new Padding(1);
        layout.Controls.Add(_textBoxBorder, 0, 0);

        // WinForms' native TextBox ignores Control.Padding entirely (it's a plain Win32
        // Edit control under the hood), so text/caret render flush against its edge. An
        // inner panel with real padding is the standard way to get visual breathing room.
        var textBoxInset = new Panel { Dock = DockStyle.Fill, BackColor = TextBoxBackColor, Padding = new Padding(10, 8, 10, 8) };
        _textBoxBorder.Controls.Add(textBoxInset);

        // RichTextBox (not TextBox) so #tags can be colored inline as you type.
        _textBox.Multiline = true;
        _textBox.Dock = DockStyle.Fill;
        _textBox.BorderStyle = BorderStyle.None;
        _textBox.BackColor = TextBoxBackColor;
        _textBox.ForeColor = TextColor;
        _textBox.Font = new Font("Segoe UI", 11f);
        _textBox.AcceptsTab = true;
        _textBox.KeyDown += TextBox_KeyDown;
        _textBox.TextChanged += (_, _) => HighlightTags();
        _textBox.Enter += (_, _) => _textBoxBorder.BackColor = AccentColor;
        _textBox.Leave += (_, _) => _textBoxBorder.BackColor = BorderColor;
        textBoxInset.Controls.Add(_textBox);

        var bottomBar = new Panel { Dock = DockStyle.Fill, BackColor = BackgroundColor };
        layout.Controls.Add(bottomBar, 0, 2);

        _saveButton.Text = "Save";
        _saveButton.Dock = DockStyle.Right;
        _saveButton.Width = 92;
        _saveButton.FlatStyle = FlatStyle.Flat;
        _saveButton.FlatAppearance.BorderSize = 0;
        _saveButton.BackColor = AccentColor;
        _saveButton.ForeColor = Color.White;
        _saveButton.Font = new Font("Segoe UI Semibold", 9.5f, FontStyle.Regular);
        _saveButton.Cursor = Cursors.Hand;
        _saveButton.MouseEnter += (_, _) => _saveButton.BackColor = AccentHoverColor;
        _saveButton.MouseLeave += (_, _) => _saveButton.BackColor = AccentColor;
        _saveButton.Click += (_, _) => SaveAndClose(discard: false);
        bottomBar.Controls.Add(_saveButton);

        _hintLabel.Dock = DockStyle.Fill;
        _hintLabel.TextAlign = ContentAlignment.MiddleLeft;
        _hintLabel.ForeColor = HintColor;
        _hintLabel.Font = new Font("Segoe UI", 8f);
        _hintLabel.Text = HintText;
        bottomBar.Controls.Add(_hintLabel);
        _hintLabel.BringToFront();

        // Sits above everything else in the card so sparkle particles can be drawn
        // over the textbox/footer without a container's OnPaint being clipped away
        // by those opaque sibling controls (see SparkleOverlay for why).
        _sparkleOverlay.Dock = DockStyle.Fill;
        _sparkleOverlay.Paint += SparkleOverlay_Paint;
        content.Controls.Add(_sparkleOverlay);
        _sparkleOverlay.BringToFront();
    }

    private void TryLoadIcon()
    {
        var iconPath = Path.Combine(AppContext.BaseDirectory, "app.ico");
        if (File.Exists(iconPath))
        {
            Icon = new Icon(iconPath);
        }
    }

    private void SetupTrayIcon()
    {
        _trayIcon.Icon = Icon ?? SystemIcons.Application;
        _trayIcon.Text = "QuickNote";
        _trayIcon.Visible = true;

        var menu = new ContextMenuStrip();
        menu.Items.Add("Reload Config", null, (_, _) => ReloadConfig());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitApp());
        _trayIcon.ContextMenuStrip = menu;
    }

    private void ReloadConfig()
    {
        _config = AppConfig.Load(_configPath);

        try
        {
            _toggleShortcut = Shortcut.Parse(_config.ToggleNote);
            _saveShortcut = Shortcut.Parse(_config.SaveNote);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Invalid shortcut in config.yaml: {ex.Message}", "QuickNote",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        if (_hotkeyRegistered)
        {
            NativeMethods.UnregisterHotKey(Handle, HotkeyId);
            _hotkeyRegistered = false;
        }

        if (IsHandleCreated)
        {
            RegisterToggleHotkey();
        }
    }

    private void RegisterToggleHotkey()
    {
        _hotkeyRegistered = NativeMethods.RegisterHotKey(
            Handle, HotkeyId, _toggleShortcut.Modifiers, (uint)_toggleShortcut.Key);

        if (!_hotkeyRegistered)
        {
            MessageBox.Show(
                $"Could not register global hotkey '{_config.ToggleNote}'. It may already be in use by another app.",
                "QuickNote", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        RegisterToggleHotkey();

        // Native, anti-aliased rounded corners (Windows 11's own compositor does the
        // clipping) instead of a hand-rolled GraphicsPath region, which always looked
        // slightly different from the border we draw ourselves.
        var preference = NativeMethods.DWMWCP_ROUND;
        NativeMethods.DwmSetWindowAttribute(Handle, NativeMethods.DWMWA_WINDOW_CORNER_PREFERENCE, ref preference, sizeof(int));
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using var pen = new Pen(_cardGlowColor, 1);
        e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
    }

    private void SparkleOverlay_Paint(object? sender, PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        foreach (var p in _particles)
        {
            var lifeT = p.Age / p.LifeSeconds;
            var alpha = (int)(255 * Math.Clamp(1f - lifeT, 0f, 1f));
            if (alpha <= 0)
            {
                continue;
            }
            var color = Color.FromArgb(alpha, p.Color);
            var size = p.Size * (1f - lifeT * 0.4f);
            DrawSparkle(e.Graphics, p.Position, size, color);
        }
    }

    private static void DrawSparkle(Graphics g, PointF center, float size, Color color)
    {
        if (size <= 0.4f)
        {
            return;
        }

        using var pen = new Pen(color, Math.Max(1f, size / 3f));
        g.DrawLine(pen, center.X - size, center.Y, center.X + size, center.Y);
        g.DrawLine(pen, center.X, center.Y - size, center.X, center.Y + size);

        var diag = size * 0.6f;
        g.DrawLine(pen, center.X - diag, center.Y - diag, center.X + diag, center.Y + diag);
        g.DrawLine(pen, center.X - diag, center.Y + diag, center.X + diag, center.Y - diag);
    }

    private static Color LerpColor(Color a, Color b, float t)
    {
        t = Math.Clamp(t, 0f, 1f);
        return Color.FromArgb(
            (int)(a.R + ((b.R - a.R) * t)),
            (int)(a.G + ((b.G - a.G) * t)),
            (int)(a.B + ((b.B - a.B) * t)));
    }

    private void TriggerCelebration()
    {
        _celebrateElapsed = 0f;
        _particles.Clear();

        var originScreen = _saveButton.PointToScreen(new Point(_saveButton.Width / 2, 0));
        var origin = _sparkleOverlay.PointToClient(originScreen);

        for (var i = 0; i < 16; i++)
        {
            var angle = (-MathF.PI / 2) + ((float)(_random.NextDouble() - 0.5) * MathF.PI * 0.9f);
            var speed = 60f + ((float)_random.NextDouble() * 90f);
            _particles.Add(new Particle
            {
                Position = origin,
                Velocity = new PointF(MathF.Cos(angle) * speed, MathF.Sin(angle) * speed),
                Age = 0f,
                LifeSeconds = 0.5f + ((float)_random.NextDouble() * 0.35f),
                Size = 2.5f + ((float)_random.NextDouble() * 2.5f),
                Color = _random.NextDouble() < 0.5 ? CelebrateGreen : CelebrateGold,
            });
        }

        _celebrateTimer.Start();
    }

    private void CelebrateTimer_Tick(object? sender, EventArgs e)
    {
        const float dt = 0.016f;
        _celebrateElapsed += dt;

        var t = Math.Clamp(_celebrateElapsed / CelebrateDurationSeconds, 0f, 1f);
        var glowT = t < 0.35f ? t / 0.35f : 1f - ((t - 0.35f) / 0.65f);
        _cardGlowColor = LerpColor(BorderColor, CelebrateGreen, Math.Clamp(glowT, 0f, 1f));
        _textBoxBorder.BackColor = _cardGlowColor;

        for (var i = _particles.Count - 1; i >= 0; i--)
        {
            var p = _particles[i];
            p.Age += dt;
            if (p.Age >= p.LifeSeconds)
            {
                _particles.RemoveAt(i);
                continue;
            }
            p.Velocity.Y += 40f * dt;
            p.Position.X += p.Velocity.X * dt;
            p.Position.Y += p.Velocity.Y * dt;
        }

        Invalidate();
        _sparkleOverlay.Invalidate();

        if (t >= 1f && _particles.Count == 0)
        {
            _celebrateTimer.Stop();
        }
    }

    private void ResetCelebration()
    {
        _celebrateTimer.Stop();
        _particles.Clear();
        _cardGlowColor = BorderColor;
        _textBoxBorder.BackColor = BorderColor;
        Invalidate();
        _sparkleOverlay.Invalidate();
    }

    // Re-color every #tag in the box. Runs on each TextChanged; changing
    // SelectionColor does not itself raise TextChanged, but we guard anyway.
    private void HighlightTags()
    {
        if (_highlighting)
        {
            return;
        }
        _highlighting = true;

        // Freeze painting to avoid flicker while we walk the runs.
        NativeMethods.SendMessage(_textBox.Handle, NativeMethods.WM_SETREDRAW, IntPtr.Zero, IntPtr.Zero);

        var selStart = _textBox.SelectionStart;
        var selLen = _textBox.SelectionLength;

        _textBox.SelectAll();
        _textBox.SelectionColor = TextColor;

        foreach (System.Text.RegularExpressions.Match match in
                 System.Text.RegularExpressions.Regex.Matches(_textBox.Text, @"(^|\s)(#[^\s#]+)"))
        {
            var tag = match.Groups[2];
            _textBox.Select(tag.Index, tag.Length);
            _textBox.SelectionColor = TagColor;
        }

        // Restore the caret/selection and make sure new typing uses the default color.
        _textBox.Select(selStart, selLen);
        _textBox.SelectionColor = TextColor;

        NativeMethods.SendMessage(_textBox.Handle, NativeMethods.WM_SETREDRAW, new IntPtr(1), IntPtr.Zero);
        _textBox.Invalidate();

        _highlighting = false;
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == NativeMethods.WM_HOTKEY && m.WParam.ToInt32() == HotkeyId)
        {
            ToggleNote();
            return;
        }
        base.WndProc(ref m);
    }

    private void ToggleNote()
    {
        if (Visible)
        {
            HideNote();
        }
        else
        {
            ShowNote();
        }
    }

    private void ShowNote()
    {
        _closeTimer.Stop();
        ResetCelebration();
        _textBox.Clear();
        _textBox.Enabled = true;
        _saveButton.Enabled = true;
        _hintLabel.Text = HintText;
        _hintLabel.ForeColor = HintColor;
        Show();
        Activate();
        _textBox.Focus();
    }

    private void HideNote()
    {
        Hide();
    }

    private void TextBox_KeyDown(object? sender, KeyEventArgs e)
    {
        var isSaveGesture = _saveShortcut.MatchesKeyEvent(e)
            || (e.KeyCode == Keys.Enter && e.Shift == false && (e.Control || e.Alt));

        if (isSaveGesture)
        {
            e.Handled = true;
            e.SuppressKeyPress = true;
            SaveAndClose(discard: false);
            return;
        }

        if (e.KeyCode == Keys.Escape)
        {
            e.Handled = true;
            SaveAndClose(discard: true);
        }
    }

    private void SaveAndClose(bool discard)
    {
        var text = _textBox.Text.Trim();
        if (text.Length == 0)
        {
            HideNote();
            return;
        }

        SaveOutcome outcome;
        try
        {
            // Discarded drafts carry a #discarded tag so they route through the
            // same tag rules as everything else (falling back to `default`).
            var toSave = discard ? text + " #discarded" : text;
            outcome = NoteRouter.Save(_config, toSave);
        }
        catch (Exception ex)
        {
            // Only reached if the durable SQLite write itself failed.
            _hintLabel.Text = "Error saving note";
            _hintLabel.ForeColor = StatusDiscardColor;
            MessageBox.Show($"Failed to save note: {ex.Message}", "QuickNote",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        // The note is safely in SQLite; `Pending` only means a connector was
        // unreachable, so it's queued for later, not lost.
        string where;
        if (outcome.Pending.Count > 0)
        {
            where = $" - pending: {string.Join(", ", outcome.Pending)}";
        }
        else if (outcome.Delivered.Count > 0)
        {
            where = $" -> {string.Join(", ", outcome.Delivered)}";
        }
        else
        {
            where = "";
        }
        _hintLabel.Text = discard ? $"Saved as #discarded{where}" : $"Saved{where}";
        _hintLabel.ForeColor = discard ? StatusDiscardColor : StatusOkColor;
        _textBox.Enabled = false;
        _saveButton.Enabled = false;

        if (!discard)
        {
            TriggerCelebration();
        }

        _closeTimer.Interval = discard ? DiscardCloseDelayMs : SaveCloseDelayMs;
        _closeTimer.Start();
    }

    private void ExitApp()
    {
        if (_hotkeyRegistered)
        {
            NativeMethods.UnregisterHotKey(Handle, HotkeyId);
        }
        _trayIcon.Visible = false;
        _trayIcon.Dispose();
        _exiting = true;
        Close();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!_exiting && e.CloseReason == CloseReason.UserClosing)
        {
            // No border/close button, but guard against Alt+F4 quitting silently.
            e.Cancel = true;
            HideNote();
            return;
        }
        base.OnFormClosing(e);
    }
}
