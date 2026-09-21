using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace QuickNote;

/// A destination a note can be written to. `Type` selects the connector;
/// `Path` is used by the file-based kinds (markdown/obsidian/logseq). The
/// `affine` kind never holds AFFiNE credentials here - those live
/// server-side in the sidecar's own connectors.json (see
/// connectors/affine-sidecar); this only names which sidecar connector to
/// call, matching the Android app's config shape.
public sealed class Connection
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "markdown";
    public string? Path { get; set; }
    public string? SidecarUrl { get; set; }
    public string? SidecarToken { get; set; }
    public string? SidecarConnector { get; set; }
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
