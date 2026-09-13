using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace QuickNote;

/// A destination a note can be written to. `Type` selects the connector; `Path`
/// is used by the file-based kinds (markdown/obsidian/logseq) and the remaining
/// fields configure the `affine` connector.
public sealed class Connection
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "markdown";
    public string? Path { get; set; }
    public string? Url { get; set; }
    public string? Email { get; set; }
    public string? Password { get; set; }
    public string? WorkspaceId { get; set; }
    public string? PageId { get; set; }

    /// affine: append to today's journal (auto-created if missing). Ignored when
    /// PageId is set (an explicit pin wins).
    public bool? Journal { get; set; }
}

public sealed class RoutingConfig
{
    /// tag -> connection name
    public Dictionary<string, string> Tags { get; set; } = new();

    /// connection used when a note has no tag (or only unmapped tags)
    public string Default { get; set; } = "personal";
}

public sealed class ShortcutsConfig
{
    public string ToggleNote { get; set; } = "Alt+Space";
    public string SaveNote { get; set; } = "Alt+Enter";
}

public sealed class AppConfig
{
    public ShortcutsConfig Shortcuts { get; set; } = new();
    public List<Connection> Connections { get; set; } = new();
    public RoutingConfig Routing { get; set; } = new();

    // Convenience accessors used by the rest of the app.
    public string ToggleNote => Shortcuts.ToggleNote;
    public string SaveNote => Shortcuts.SaveNote;

    public Connection? Connection(string name) =>
        Connections.FirstOrDefault(c => c.Name == name);

    public static AppConfig Load(string path)
    {
        if (!File.Exists(path))
        {
            return new AppConfig();
        }

        // config.yaml uses snake_case keys (toggle_note, workspace_id, ...); map
        // them to our PascalCase properties. UnderscoredNamingConvention turns
        // "workspace_id" -> "WorkspaceId", "toggle_note" -> "ToggleNote", etc.
        var deserializer = new DeserializerBuilder()
            .WithNamingConvention(UnderscoredNamingConvention.Instance)
            .IgnoreUnmatchedProperties()
            .Build();

        var config = deserializer.Deserialize<AppConfig>(File.ReadAllText(path));
        return config ?? new AppConfig();
    }
}
