using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace QuickNote;

/// Extracts #tags from a note, resolves which connection(s) it routes to, and
/// writes it there. Mirrors the routing/connector logic in the Tauri backend
/// (src-tauri/src/lib.rs) so both apps behave identically.
/// The result of a <see cref="NoteRouter.Save"/>: the note is always persisted;
/// these lists describe what happened during delivery.
public sealed class SaveOutcome
{
    public List<string> Targets { get; set; } = new();
    public List<string> Delivered { get; } = new();
    public List<string> Pending { get; } = new();
    public string? Error { get; set; }
}

public static class NoteRouter
{
    /// A tag is a `#` at the start or right after whitespace, immediately followed
    /// by non-space, non-`#` characters ("#work" is a tag; "# heading" is not).
    /// Returns tag words (without the leading `#`), in order, deduplicated.
    public static List<string> ExtractTags(string text)
    {
        var tags = new List<string>();
        for (var i = 0; i < text.Length; i++)
        {
            var atBoundary = i == 0 || char.IsWhiteSpace(text[i - 1]);
            if (text[i] != '#' || !atBoundary)
            {
                continue;
            }

            var j = i + 1;
            var word = new StringBuilder();
            while (j < text.Length && !char.IsWhiteSpace(text[j]) && text[j] != '#')
            {
                word.Append(text[j]);
                j++;
            }

            var raw = word.ToString();
            var end = raw.Length;
            while (end > 0 && IsTrailingPunctuation(raw[end - 1]))
            {
                end--;
            }
            var trimmed = raw[..end];
            if (trimmed.Length > 0)
            {
                if (!tags.Contains(trimmed))
                {
                    tags.Add(trimmed);
                }
                i = j - 1; // continue scanning after the tag
            }
        }
        return tags;
    }

    private static bool IsTrailingPunctuation(char c) =>
        !char.IsLetterOrDigit(c) && c != '_' && c != '-' && c != '/';

    /// Every connection mapped by one of the note's tags (deduped), or the
    /// `default` connection when there are no tags or none map to anything.
    public static List<string> ResolveTargets(AppConfig config, IReadOnlyList<string> tags)
    {
        var names = new List<string>();
        foreach (var tag in tags)
        {
            if (config.Routing.Tags.TryGetValue(tag, out var name) && !names.Contains(name))
            {
                names.Add(name);
            }
        }
        if (names.Count == 0)
        {
            names.Add(config.Routing.Default);
        }
        return names;
    }

    /// Persist a note durably, then attempt delivery to its routed connections.
    ///
    /// The note is written to SQLite FIRST, so it is never lost even if every
    /// connector is down. Connector failures are non-fatal: those targets are
    /// reported in <see cref="SaveOutcome.Pending"/> and the row stays at
    /// processed = 0 as backlog. Only a failure of the durable write itself
    /// throws (durability cannot be guaranteed in that case).
    public static SaveOutcome Save(AppConfig config, string text)
    {
        var tags = ExtractTags(text);
        var targetNames = ResolveTargets(config, tags);

        using var db = NoteStore.Open(AppPaths.DbPath);
        var id = NoteStore.Insert(db,
            DateTimeOffset.UtcNow.ToString("o"),
            text,
            string.Join(",", tags),
            string.Join(",", targetNames),
            "winforms");

        var outcome = new SaveOutcome { Targets = targetNames };
        var errors = new List<string>();
        foreach (var name in targetNames)
        {
            try
            {
                var conn = config.Connection(name)
                    ?? throw new InvalidOperationException(
                        $"routing points to connection '{name}' which is not defined");
                WriteToConnection(conn, text);
                outcome.Delivered.Add(name);
            }
            catch (Exception ex)
            {
                outcome.Pending.Add(name);
                errors.Add($"{name}: {ex.Message}");
            }
        }

        if (errors.Count == 0)
        {
            NoteStore.MarkProcessed(db, id, DateTimeOffset.UtcNow.ToString("o"));
        }
        else
        {
            outcome.Error = string.Join("; ", errors);
            NoteStore.MarkError(db, id, outcome.Error);
        }

        return outcome;
    }

    private static void WriteToConnection(Connection conn, string text)
    {
        switch (conn.Type)
        {
            case "markdown":
            case "obsidian":
                WriteMarkdown(ResolveDir(RequirePath(conn)), text);
                break;
            case "logseq":
                WriteLogseq(ResolveDir(RequirePath(conn)), text);
                break;
            case "affine":
                WriteAffine(conn, text);
                break;
            default:
                throw new InvalidOperationException(
                    $"connection '{conn.Name}' has unknown type '{conn.Type}'");
        }
    }

    private static string RequirePath(Connection conn) =>
        conn.Path ?? throw new InvalidOperationException($"connection '{conn.Name}' missing 'path'");

    private static string ResolveDir(string path)
    {
        if (Path.IsPathRooted(path))
        {
            return path;
        }
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        return Path.Combine(appData, "QuickNote", path);
    }

    private static void WriteMarkdown(string dir, string text)
    {
        Directory.CreateDirectory(dir);
        var now = DateTime.Now;
        var filePath = Path.Combine(dir, $"{now:yyyy-MM-dd}.md");
        var isNew = !File.Exists(filePath);

        using var writer = new StreamWriter(filePath, append: true);
        if (isNew)
        {
            writer.WriteLine($"# {now:dddd, MMMM d yyyy}");
            writer.WriteLine();
        }
        writer.WriteLine($"## {now:HH:mm:ss}");
        writer.WriteLine();
        writer.WriteLine(text);
        writer.WriteLine();
    }

    private static void WriteLogseq(string dir, string text)
    {
        var journals = Path.Combine(dir, "journals");
        Directory.CreateDirectory(journals);
        var now = DateTime.Now;
        var filePath = Path.Combine(journals, $"{now:yyyy_MM_dd}.md");

        // Logseq is an outliner: one block == one bullet. Keep the note as a
        // single block, indenting continuation lines so they stay in the block.
        var body = text.Replace("\n", "\n  ");
        using var writer = new StreamWriter(filePath, append: true);
        writer.WriteLine($"- {now:HH:mm} {body}");
    }

    private static void WriteAffine(Connection conn, string text)
    {
        var journal = conn.Journal ?? false;
        if (string.IsNullOrEmpty(conn.Email) || string.IsNullOrEmpty(conn.Password)
            || string.IsNullOrEmpty(conn.WorkspaceId))
        {
            throw new InvalidOperationException(
                "affine connection requires email, password, workspace_id");
        }
        if (string.IsNullOrEmpty(conn.PageId) && !journal)
        {
            throw new InvalidOperationException(
                "affine connection needs either page_id or journal: true");
        }

        var script = FindAffineScript();
        var jobFields = new Dictionary<string, object?>
        {
            ["base"] = conn.Url,
            ["email"] = conn.Email,
            ["password"] = conn.Password,
            ["workspaceId"] = conn.WorkspaceId,
            ["texts"] = new[] { text },
        };
        // page_id pins to a specific page; otherwise journal mode targets today's.
        if (!string.IsNullOrEmpty(conn.PageId)) jobFields["pageId"] = conn.PageId;
        if (journal) jobFields["journal"] = true;
        var job = JsonSerializer.Serialize(jobFields);

        var psi = new ProcessStartInfo
        {
            FileName = "node",
            ArgumentList = { script },
            WorkingDirectory = Path.GetDirectoryName(script)!,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        Process proc;
        try
        {
            proc = Process.Start(psi) ?? throw new InvalidOperationException("failed to start node");
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                $"failed to launch node for affine connector: {ex.Message} (is Node installed and on PATH?)");
        }

        proc.StandardInput.Write(job);
        proc.StandardInput.Close();
        var stderr = proc.StandardError.ReadToEnd();
        proc.WaitForExit();
        if (proc.ExitCode != 0)
        {
            throw new InvalidOperationException($"affine connector failed: {stderr.Trim()}");
        }
    }

    /// Walk up from the exe directory to find connectors/affine/affine-append.js.
    private static string FindAffineScript()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "connectors", "affine", "affine-append.js");
            if (File.Exists(candidate))
            {
                return candidate;
            }
            dir = dir.Parent;
        }
        throw new InvalidOperationException(
            "affine connector script not found (looked for connectors/affine/affine-append.js above the exe)");
    }
}
