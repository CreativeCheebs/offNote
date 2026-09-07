namespace QuickNote;

// A child control that paints on top of everything else in the card but is
// invisible to both painting (WS_EX_TRANSPARENT lets the siblings underneath
// show through instead of a solid background) and input (WM_NCHITTEST
// reports HTTRANSPARENT so clicks/hover fall through to the real controls).
// Plain Panel.BackColor = Color.Transparent does NOT achieve this on its own
// in WinForms — it just mirrors the parent's paint, it doesn't let you draw
// over already-painted sibling controls like the textbox or Save button.
internal sealed class SparkleOverlay : Control
{
    private const int WM_NCHITTEST = 0x0084;
    private const int HTTRANSPARENT = -1;
    private const int WS_EX_TRANSPARENT = 0x20;

    public SparkleOverlay()
    {
        SetStyle(
            ControlStyles.SupportsTransparentBackColor
            | ControlStyles.OptimizedDoubleBuffer
            | ControlStyles.AllPaintingInWmPaint
            | ControlStyles.UserPaint
            | ControlStyles.ResizeRedraw,
            true);
        BackColor = Color.Transparent;
        TabStop = false;
    }

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.ExStyle |= WS_EX_TRANSPARENT;
            return cp;
        }
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_NCHITTEST)
        {
            m.Result = (IntPtr)HTTRANSPARENT;
            return;
        }
        base.WndProc(ref m);
    }
}
