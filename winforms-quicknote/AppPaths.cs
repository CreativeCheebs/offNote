namespace QuickNote;

/// The shared %APPDATA%\QuickNote directory used by BOTH the Tauri and WinForms
/// apps: it holds the single live config.yaml, the SQLite store, and any
/// relative note folders. Keeping these paths identical to the Tauri side
/// (src-tauri/src/lib.rs) is what makes "one config, one database" work.
public static class AppPaths
{
    public static string DataDir =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "QuickNote");

    public static string DbPath => Path.Combine(DataDir, "quicknote.db");

    /// The shared live config, seeded from the bundled template on first run
    /// (never clobbers an existing file).
    public static string LiveConfigPath()
    {
        Directory.CreateDirectory(DataDir);
        var live = Path.Combine(DataDir, "config.yaml");
        if (!File.Exists(live))
        {
            var template = Path.Combine(AppContext.BaseDirectory, "config.yaml");
            if (File.Exists(template))
            {
                File.Copy(template, live);
            }
        }
        return live;
    }
}
