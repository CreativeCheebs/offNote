using System.Drawing.Drawing2D;

namespace QuickNote;

internal sealed class RoundedActionButton : Button
{
    private bool _hovered;

    public Color FillColor { get; set; } = Color.FromArgb(195, 170, 255);
    public Color HoverFillColor { get; set; } = Color.FromArgb(216, 195, 255);
    public Color LabelColor { get; set; } = Color.FromArgb(38, 28, 62);

    public RoundedActionButton()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Cursor = Cursors.Hand;
        TabStop = true;
    }

    protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); _hovered = true; Invalidate(); }
    protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); _hovered = false; Invalidate(); }
    protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        var bounds = new Rectangle(1, 1, Width - 2, Height - 2);
        if (bounds.Width <= 0 || bounds.Height <= 0) return;
        var radius = Math.Min(bounds.Height / 2, 20);
        using var path = new GraphicsPath();
        path.AddArc(bounds.Left, bounds.Top, radius * 2, radius * 2, 180, 90);
        path.AddArc(bounds.Right - radius * 2, bounds.Top, radius * 2, radius * 2, 270, 90);
        path.AddArc(bounds.Right - radius * 2, bounds.Bottom - radius * 2, radius * 2, radius * 2, 0, 90);
        path.AddArc(bounds.Left, bounds.Bottom - radius * 2, radius * 2, radius * 2, 90, 90);
        path.CloseFigure();
        using var brush = new SolidBrush(Enabled ? (_hovered ? HoverFillColor : FillColor) : Color.FromArgb(82, 75, 95));
        e.Graphics.FillPath(brush, path);
        TextRenderer.DrawText(e.Graphics, Text, Font, bounds,
            Enabled ? LabelColor : Color.FromArgb(180, 174, 190),
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        if (Focused && ShowFocusCues)
        {
            using var focusPen = new Pen(Color.White, 1) { DashStyle = DashStyle.Dot };
            e.Graphics.DrawPath(focusPen, path);
        }
    }
}
