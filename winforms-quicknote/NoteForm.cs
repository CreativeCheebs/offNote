namespace QuickNote;

public sealed class NoteForm : Form
{
    private const int HotkeyId = 1;
    private const int CardPadding = 22;
    private const int FooterHeight = 44;

    private static readonly Color BackgroundColor = Color.FromArgb(25, 22, 34);
    private static readonly Color BorderColor = Color.FromArgb(72, 64, 85);
    private static readonly Color AccentColor = Color.FromArgb(209, 186, 255);
    private static readonly Color AccentHoverColor = Color.FromArgb(226, 207, 255);
    private static readonly Color TagColor = Color.FromArgb(178, 224, 204);
    private static readonly Color TextBoxBackColor = Color.FromArgb(39, 35, 49);
    private static readonly Color TextColor = Color.FromArgb(248, 244, 250);
    private static readonly Color HintColor = Color.FromArgb(169, 161, 180);
    private static readonly Color StatusOkColor = Color.FromArgb(140, 230, 170);
    private static readonly Color StatusDiscardColor = Color.FromArgb(240, 190, 120);
    private static readonly Color CelebrateGreen = Color.FromArgb(90, 224, 138);

    // A wide, saturated palette so the celebration reads as "many different
    // colors" rather than a two-tone accent flourish.
    private static readonly Color[] ConfettiPalette =
    {
        Color.FromArgb(255, 92, 92),
        Color.FromArgb(255, 159, 67),
        Color.FromArgb(255, 217, 61),
        Color.FromArgb(107, 203, 119),
        Color.FromArgb(78, 205, 196),
        Color.FromArgb(77, 150, 255),
        Color.FromArgb(160, 108, 255),
        Color.FromArgb(255, 110, 199),
        Color.FromArgb(255, 255, 255),
    };

    private const int DiscardCloseDelayMs = 450;
    private const float CelebrateDurationSeconds = 1.1f;

    private readonly Panel _textBoxBorder = new();
    private readonly RichTextBox _textBox = new();
    private bool _highlighting;
    private readonly Label _hintLabel = new();
    private readonly RoundedActionButton _saveButton = new();
    private readonly SparkleOverlay _sparkleOverlay = new();
    private readonly NotifyIcon _trayIcon = new();
    private readonly System.Windows.Forms.Timer _closeTimer = new() { Interval = DiscardCloseDelayMs };
    private readonly System.Windows.Forms.Timer _celebrateTimer = new() { Interval = 16 };
    private readonly List<Particle> _particles = new();
    private readonly Random _random = new();
    private Color _cardGlowColor;
    private float _celebrateElapsed;

    private const string HintText = "Alt / Ctrl + Enter to save   |   Esc to discard";

    private sealed class Particle
    {
        public PointF Position;
        public PointF Velocity;
        public float Age;
        public float LifeSeconds;
        public float Size;
        public Color Color;
        public float Rotation;
        public float Spin;
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

        AutoScaleDimensions = new SizeF(96f, 96f);
        AutoScaleMode = AutoScaleMode.Dpi;
        FormBorderStyle = FormBorderStyle.None;
        Text = "Offnote";
        StartPosition = FormStartPosition.CenterScreen;
        ShowInTaskbar = false;
        TopMost = true;
        Size = new Size(540, 330);
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

        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Margin = Padding.Empty,
            ColumnCount = 1,
            RowCount = 4,
            BackColor = BackgroundColor,
        };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 14));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, FooterHeight));
        content.Controls.Add(layout);

        // Fixed-pixel label positions clipped the title once fonts scaled with DPI, so the
        // header is a table: badge | title + subtitle (auto height) | close.
        var header = new TableLayoutPanel
        {
            Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 3, RowCount = 1, BackColor = BackgroundColor,
            Margin = new Padding(0, 0, 0, 14), Padding = Padding.Empty,
        };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        header.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(header, 0, 0);
        header.Controls.Add(new Label
        {
            Text = "O", Width = 34, Height = 34, Margin = new Padding(0, 0, 13, 0),
            Anchor = AnchorStyles.Left, TextAlign = ContentAlignment.MiddleCenter,
            BackColor = AccentColor, ForeColor = BackgroundColor,
            Font = new Font("Segoe UI Semibold", 13f),
            AccessibleName = "Offnote",
        }, 0, 0);
        var titles = new TableLayoutPanel
        {
            AutoSize = true, ColumnCount = 1, RowCount = 2, Anchor = AnchorStyles.Left,
            BackColor = BackgroundColor, Margin = Padding.Empty,
        };
        titles.Controls.Add(new Label
        {
            Text = "Offnote", AutoSize = true, Margin = Padding.Empty,
            ForeColor = TextColor, Font = new Font("Segoe UI Semibold", 15f),
        }, 0, 0);
        titles.Controls.Add(new Label
        {
            Text = "CAPTURE A THOUGHT", AutoSize = true, Margin = Padding.Empty,
            ForeColor = HintColor, Font = new Font("Segoe UI Semibold", 8f),
        }, 0, 1);
        header.Controls.Add(titles, 1, 0);
        var close = new Label
        {
            Text = "×", AutoSize = false, Width = 32, Height = 32,
            Anchor = AnchorStyles.Right | AnchorStyles.Top, Margin = new Padding(0, 4, 0, 0),
            ForeColor = HintColor, TextAlign = ContentAlignment.MiddleCenter,
            Font = new Font("Segoe UI", 18f), Cursor = Cursors.Hand,
            AccessibleName = "Close", AccessibleRole = AccessibleRole.PushButton,
        };
        close.Click += (_, _) => HideNote();
        close.MouseEnter += (_, _) => close.ForeColor = TextColor;
        close.MouseLeave += (_, _) => close.ForeColor = HintColor;
        header.Controls.Add(close, 2, 0);

        _textBoxBorder.Dock = DockStyle.Fill;
        _textBoxBorder.Margin = Padding.Empty;
        _textBoxBorder.BackColor = BorderColor;
        _textBoxBorder.Padding = new Padding(1);
        layout.Controls.Add(_textBoxBorder, 0, 1);

        var textBoxInset = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = TextBoxBackColor,
            Padding = new Padding(15, 12, 15, 12),
        };
        _textBoxBorder.Controls.Add(textBoxInset);
        _textBox.Multiline = true;
        _textBox.Dock = DockStyle.Fill;
        _textBox.BorderStyle = BorderStyle.None;
        _textBox.BackColor = TextBoxBackColor;
        _textBox.ForeColor = TextColor;
        _textBox.Font = new Font("Segoe UI", 11.5f);
        _textBox.AcceptsTab = true;
        _textBox.AccessibleName = "Quick note";
        _textBox.KeyDown += TextBox_KeyDown;
        _textBox.TextChanged += (_, _) => HighlightTags();
        _textBox.Enter += (_, _) => _textBoxBorder.BackColor = AccentColor;
        _textBox.Leave += (_, _) => _textBoxBorder.BackColor = BorderColor;
        textBoxInset.Controls.Add(_textBox);

        var bottomBar = new Panel { Dock = DockStyle.Fill, BackColor = BackgroundColor, Margin = Padding.Empty };
        layout.Controls.Add(bottomBar, 0, 3);
        _saveButton.Text = "Save note";
        _saveButton.Dock = DockStyle.Right;
        _saveButton.Width = 126;
        _saveButton.FillColor = AccentColor;
        _saveButton.HoverFillColor = AccentHoverColor;
        _saveButton.LabelColor = BackgroundColor;
        _saveButton.Font = new Font("Segoe UI Semibold", 10f);
        _saveButton.Click += (_, _) => SaveAndClose(discard: false);
        bottomBar.Controls.Add(_saveButton);

        _hintLabel.Dock = DockStyle.Fill;
        _hintLabel.TextAlign = ContentAlignment.MiddleLeft;
        _hintLabel.ForeColor = HintColor;
        _hintLabel.Font = new Font("Segoe UI", 8.5f);
        _hintLabel.Text = HintText;
        bottomBar.Controls.Add(_hintLabel);
        _hintLabel.BringToFront();

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
        _trayIcon.Text = "Offnote";
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
            MessageBox.Show($"Invalid shortcut in config.yaml: {ex.Message}", "Offnote",
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
                "Offnote", MessageBoxButtons.OK, MessageBoxIcon.Warning);
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
            var size = p.Size * (1f - lifeT * 0.25f);
            DrawConfettiPiece(e.Graphics, p.Position, p.Rotation, size, color);
        }
    }

    private static void DrawConfettiPiece(Graphics g, PointF center, float rotation, float size, Color color)
    {
        if (size <= 0.4f)
        {
            return;
        }

        var state = g.Save();
        g.TranslateTransform(center.X, center.Y);
        g.RotateTransform(rotation * (180f / MathF.PI));
        using var brush = new SolidBrush(color);
        g.FillRectangle(brush, -size / 2f, -size / 3f, size, size * 0.66f);
        g.Restore(state);
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

        // Explode from the middle of the whole card, not just the save
        // button, so the burst fills the window edge to edge.
        var origin = new PointF(_sparkleOverlay.Width / 2f, _sparkleOverlay.Height * 0.35f);

        const int particleCount = 90;
        for (var i = 0; i < particleCount; i++)
        {
            var angle = (float)(_random.NextDouble() * MathF.PI * 2);
            var speed = 90f + ((float)_random.NextDouble() * 220f);
            _particles.Add(new Particle
            {
                Position = origin,
                Velocity = new PointF(MathF.Cos(angle) * speed, MathF.Sin(angle) * speed),
                Age = 0f,
                LifeSeconds = 0.7f + ((float)_random.NextDouble() * 0.5f),
                Size = 3f + ((float)_random.NextDouble() * 4f),
                Color = ConfettiPalette[_random.Next(ConfettiPalette.Length)],
                Rotation = (float)(_random.NextDouble() * MathF.PI * 2),
                Spin = (float)((_random.NextDouble() - 0.5) * 10),
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
            p.Velocity.Y += 220f * dt;
            p.Position.X += p.Velocity.X * dt;
            p.Position.Y += p.Velocity.Y * dt;
            p.Rotation += p.Spin * dt;
        }

        Invalidate();
        RepaintSparkles();

        if (t >= 1f && _particles.Count == 0)
        {
            _celebrateTimer.Stop();
        }
    }

    // The overlay no longer paints its own background (that hid the text box border), so
    // repaint the controls underneath first to wipe the previous frame's particles.
    private void RepaintSparkles() => _sparkleOverlay.Parent?.Invalidate(_sparkleOverlay.Bounds, true);

    private void ResetCelebration()
    {
        _celebrateTimer.Stop();
        _particles.Clear();
        _cardGlowColor = BorderColor;
        _textBoxBorder.BackColor = BorderColor;
        Invalidate();
        RepaintSparkles();
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
            MessageBox.Show($"Failed to save note: {ex.Message}", "Offnote",
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

        if (discard)
        {
            _closeTimer.Interval = DiscardCloseDelayMs;
            _closeTimer.Start();
            return;
        }

        // Close as soon as the note is saved - the confetti plays out on its
        // own timer regardless of whether the window is still open, so there's
        // no need to keep the popup around just to watch it.
        TriggerCelebration();
        HideNote();
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
