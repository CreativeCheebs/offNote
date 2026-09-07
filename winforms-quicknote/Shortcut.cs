namespace QuickNote;

// Parses accelerator strings like "Alt+Space" or "Ctrl+Shift+N" from config.yaml.
public readonly struct Shortcut
{
    public const uint ModAlt = 0x0001;
    public const uint ModControl = 0x0002;
    public const uint ModShift = 0x0004;
    public const uint ModWin = 0x0008;

    public uint Modifiers { get; }
    public Keys Key { get; }

    private Shortcut(uint modifiers, Keys key)
    {
        Modifiers = modifiers;
        Key = key;
    }

    public bool HasAlt => (Modifiers & ModAlt) != 0;
    public bool HasControl => (Modifiers & ModControl) != 0;
    public bool HasShift => (Modifiers & ModShift) != 0;

    public static Shortcut Parse(string spec)
    {
        uint modifiers = 0;
        var key = Keys.None;

        foreach (var rawPart in spec.Split('+'))
        {
            var part = rawPart.Trim();
            switch (part.ToLowerInvariant())
            {
                case "alt":
                case "option":
                    modifiers |= ModAlt;
                    break;
                case "ctrl":
                case "control":
                    modifiers |= ModControl;
                    break;
                case "shift":
                    modifiers |= ModShift;
                    break;
                case "win":
                case "cmd":
                case "super":
                    modifiers |= ModWin;
                    break;
                default:
                    key = Enum.Parse<Keys>(part, ignoreCase: true);
                    break;
            }
        }

        return new Shortcut(modifiers, key);
    }

    public bool MatchesKeyEvent(KeyEventArgs e)
    {
        return e.KeyCode == Key
            && e.Alt == HasAlt
            && e.Control == HasControl
            && e.Shift == HasShift;
    }
}
