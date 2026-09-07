namespace QuickNote;

// Minimal hand-rolled reader for our specific two-section config.yaml shape.
// Avoids pulling in a YAML NuGet package for three settings.
public sealed class AppConfig
{
    public string ToggleNote { get; private set; } = "Alt+Space";
    public string SaveNote { get; private set; } = "Alt+Enter";
    public string NotesDirectory { get; private set; } = "notes";

    public static AppConfig Load(string path)
    {
        var config = new AppConfig();
        if (!File.Exists(path))
        {
            return config;
        }

        string section = "";
        foreach (var rawLine in File.ReadAllLines(path))
        {
            var line = rawLine;
            var hashIndex = line.IndexOf('#');
            if (hashIndex >= 0)
            {
                line = line[..hashIndex];
            }
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            var indented = line.Length > 0 && char.IsWhiteSpace(line[0]);
            var trimmed = line.Trim();

            if (!indented)
            {
                section = trimmed.TrimEnd(':');
                continue;
            }

            var separator = trimmed.IndexOf(':');
            if (separator < 0)
            {
                continue;
            }

            var key = trimmed[..separator].Trim();
            var value = trimmed[(separator + 1)..].Trim().Trim('"');

            switch (section)
            {
                case "shortcuts" when key == "toggle_note":
                    config.ToggleNote = value;
                    break;
                case "shortcuts" when key == "save_note":
                    config.SaveNote = value;
                    break;
                case "notes" when key == "directory":
                    config.NotesDirectory = value;
                    break;
            }
        }

        return config;
    }
}
