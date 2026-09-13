mod store;

use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::Write as _;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::Mutex;

use chrono::Utc;
use serde::{Deserialize, Serialize};
use tauri::menu::{Menu, MenuItem};
use tauri::tray::TrayIconBuilder;
use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{GlobalShortcutExt, Shortcut, ShortcutState};

#[derive(Debug, Clone, Deserialize, Serialize)]
struct ShortcutsConfig {
    toggle_note: String,
    save_note: String,
}

/// A destination a note can be written to. `kind` selects the connector;
/// `path` is used by the file-based kinds (markdown/obsidian/logseq) and the
/// remaining fields configure the `affine` connector.
#[derive(Debug, Clone, Deserialize, Serialize)]
struct Connection {
    name: String,
    #[serde(rename = "type")]
    kind: String,
    #[serde(default)]
    path: Option<String>,
    #[serde(default)]
    url: Option<String>,
    #[serde(default)]
    email: Option<String>,
    #[serde(default)]
    password: Option<String>,
    #[serde(default)]
    workspace_id: Option<String>,
    #[serde(default)]
    page_id: Option<String>,
    /// affine: append to today's journal (auto-created if missing). Ignored when
    /// `page_id` is set (an explicit pin wins).
    #[serde(default)]
    journal: Option<bool>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
struct RoutingConfig {
    /// tag -> connection name
    #[serde(default)]
    tags: HashMap<String, String>,
    /// connection used when a note has no tag (or only unmapped tags)
    default: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
struct AppConfig {
    shortcuts: ShortcutsConfig,
    #[serde(default)]
    connections: Vec<Connection>,
    routing: RoutingConfig,
}

impl AppConfig {
    fn connection(&self, name: &str) -> Option<&Connection> {
        self.connections.iter().find(|c| c.name == name)
    }
}

#[derive(Debug, Clone, Serialize)]
struct SaveResult {
    /// connection names the note was routed to
    targets: Vec<String>,
    /// targets the note was delivered to right now
    delivered: Vec<String>,
    /// targets that failed; the note stays queued in SQLite for these
    pending: Vec<String>,
    /// aggregated delivery error, if any (note is still saved durably)
    error: Option<String>,
}

struct AppState {
    toggle_shortcut: Mutex<Shortcut>,
}

/// The single data directory shared by both the Tauri and WinForms apps:
/// %APPDATA%\QuickNote. Holds the live config.yaml, the SQLite store, and any
/// relative note folders.
fn quicknote_data_dir() -> Result<PathBuf, String> {
    let appdata = std::env::var("APPDATA").map_err(|_| "APPDATA env var not set".to_string())?;
    Ok(PathBuf::from(appdata).join("QuickNote"))
}

fn db_path() -> Result<PathBuf, String> {
    Ok(quicknote_data_dir()?.join("quicknote.db"))
}

/// The bundled seed template (resource in release, source tree in dev).
fn template_config_path(app: &AppHandle) -> PathBuf {
    if let Ok(resource_path) = app
        .path()
        .resolve("config.yaml", tauri::path::BaseDirectory::Resource)
    {
        if resource_path.exists() {
            return resource_path;
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("config.yaml")
}

/// The shared live config at %APPDATA%\QuickNote\config.yaml, seeded from the
/// bundled template on first run (never clobbers an existing file).
fn config_path(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = quicknote_data_dir()?;
    let live = dir.join("config.yaml");
    if !live.exists() {
        fs::create_dir_all(&dir)
            .map_err(|e| format!("failed to create {}: {e}", dir.display()))?;
        let template = template_config_path(app);
        fs::copy(&template, &live).map_err(|e| {
            format!(
                "failed to seed config from {} to {}: {e}",
                template.display(),
                live.display()
            )
        })?;
    }
    Ok(live)
}

fn load_config(app: &AppHandle) -> Result<AppConfig, String> {
    let path = config_path(app)?;
    let raw = fs::read_to_string(&path)
        .map_err(|e| format!("failed to read config.yaml at {}: {e}", path.display()))?;
    serde_yaml::from_str(&raw).map_err(|e| format!("failed to parse config.yaml: {e}"))
}

/// Resolve a file-based connection's directory. Absolute paths are used as-is;
/// relative paths are anchored to the shared %APPDATA%\QuickNote dir.
fn resolve_dir(path: &str) -> Result<PathBuf, String> {
    let configured = PathBuf::from(path);
    if configured.is_absolute() {
        return Ok(configured);
    }
    Ok(quicknote_data_dir()?.join(configured))
}

/// Extract hashtags from note text. A tag is a `#` that sits at the start of the
/// text or right after whitespace and is immediately followed by non-space,
/// non-`#` characters (so "#work" is a tag but "# heading" is not). Returns the
/// tag words (without the leading `#`), in order, deduplicated.
fn extract_tags(text: &str) -> Vec<String> {
    let mut tags = Vec::new();
    let chars: Vec<char> = text.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        let at_boundary = i == 0 || chars[i - 1].is_whitespace();
        if c == '#' && at_boundary {
            let mut j = i + 1;
            let mut word = String::new();
            while j < chars.len() && !chars[j].is_whitespace() && chars[j] != '#' {
                word.push(chars[j]);
                j += 1;
            }
            // Trim trailing punctuation like "#work." or "#work,".
            let trimmed = word
                .trim_end_matches(|c: char| !c.is_alphanumeric() && c != '_' && c != '-' && c != '/')
                .to_string();
            if !trimmed.is_empty() {
                if !tags.iter().any(|t| t == &trimmed) {
                    tags.push(trimmed);
                }
                i = j;
                continue;
            }
        }
        i += 1;
    }
    tags
}

/// Decide which connections a note goes to: every connection mapped by one of
/// its tags (deduped), or the `default` connection when there are no tags or no
/// tag maps to anything.
fn resolve_targets(config: &AppConfig, tags: &[String]) -> Vec<String> {
    let mut names: Vec<String> = Vec::new();
    for tag in tags {
        if let Some(name) = config.routing.tags.get(tag) {
            if !names.iter().any(|n| n == name) {
                names.push(name.clone());
            }
        }
    }
    if names.is_empty() {
        names.push(config.routing.default.clone());
    }
    names
}

/// Path to the bundled AFFiNE Node connector (connectors/affine/affine-append.js).
fn affine_script_path(app: &AppHandle) -> Result<PathBuf, String> {
    // Bundled as a resource in release builds.
    if let Ok(res) = app.path().resolve(
        "connectors/affine/affine-append.js",
        tauri::path::BaseDirectory::Resource,
    ) {
        if res.exists() {
            return Ok(res);
        }
    }
    // Dev: repo root is the parent of src-tauri.
    let dev = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .map(|p| p.join("connectors/affine/affine-append.js"))
        .ok_or_else(|| "cannot resolve repo root for affine connector".to_string())?;
    if dev.exists() {
        return Ok(dev);
    }
    Err(format!(
        "affine connector script not found (looked for {})",
        dev.display()
    ))
}

/// Append `text` to a daily markdown file (used by markdown and obsidian kinds).
fn write_markdown(dir: &PathBuf, text: &str) -> Result<(), String> {
    fs::create_dir_all(dir).map_err(|e| format!("failed to create dir {}: {e}", dir.display()))?;
    let now = chrono::Local::now();
    let file_path = dir.join(format!("{}.md", now.format("%Y-%m-%d")));
    let is_new = !file_path.exists();
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&file_path)
        .map_err(|e| format!("failed to open {}: {e}", file_path.display()))?;
    if is_new {
        writeln!(file, "# {}\n", now.format("%A, %B %-d %Y")).map_err(|e| e.to_string())?;
    }
    writeln!(file, "## {}\n\n{}\n", now.format("%H:%M:%S"), text)
        .map_err(|e| format!("failed to write note: {e}"))
}

/// Append `text` as a bullet to a Logseq journal file (path/journals/YYYY_MM_DD.md).
fn write_logseq(dir: &PathBuf, text: &str) -> Result<(), String> {
    let journals = dir.join("journals");
    fs::create_dir_all(&journals)
        .map_err(|e| format!("failed to create dir {}: {e}", journals.display()))?;
    let now = chrono::Local::now();
    let file_path = journals.join(format!("{}.md", now.format("%Y_%m_%d")));
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&file_path)
        .map_err(|e| format!("failed to open {}: {e}", file_path.display()))?;
    // Logseq is an outliner: one block == one bullet. Keep the note as a single
    // block, indenting any continuation lines so they stay in the same block.
    let time = now.format("%H:%M");
    let body = text.replace('\n', "\n  ");
    writeln!(file, "- {time} {body}").map_err(|e| format!("failed to write note: {e}"))
}

/// Push `text` into an AFFiNE page by shelling out to the bundled Node connector.
fn write_affine(app: &AppHandle, conn: &Connection, text: &str) -> Result<(), String> {
    let email = conn
        .email
        .as_ref()
        .ok_or("affine connection missing 'email'")?;
    let password = conn
        .password
        .as_ref()
        .ok_or("affine connection missing 'password'")?;
    let workspace_id = conn
        .workspace_id
        .as_ref()
        .ok_or("affine connection missing 'workspace_id'")?;
    let journal = conn.journal.unwrap_or(false);
    if conn.page_id.is_none() && !journal {
        return Err("affine connection needs either 'page_id' or 'journal: true'".into());
    }

    let script = affine_script_path(app)?;
    let cwd = script
        .parent()
        .ok_or("cannot resolve affine connector directory")?;

    let mut job = serde_json::json!({
        "email": email,
        "password": password,
        "workspaceId": workspace_id,
        "texts": [text],
    });
    if let Some(url) = &conn.url {
        job["base"] = serde_json::Value::String(url.clone());
    }
    // page_id pins to a specific page; otherwise journal mode targets today's.
    if let Some(page_id) = &conn.page_id {
        job["pageId"] = serde_json::Value::String(page_id.clone());
    }
    if journal {
        job["journal"] = serde_json::Value::Bool(true);
    }

    let mut cmd = Command::new("node");
    cmd.arg(&script)
        .current_dir(cwd)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        cmd.creation_flags(CREATE_NO_WINDOW);
    }

    let mut child = cmd
        .spawn()
        .map_err(|e| format!("failed to launch node for affine connector: {e} (is Node installed and on PATH?)"))?;
    {
        let stdin = child
            .stdin
            .as_mut()
            .ok_or("failed to open affine connector stdin")?;
        stdin
            .write_all(job.to_string().as_bytes())
            .map_err(|e| format!("failed to send job to affine connector: {e}"))?;
    }
    let output = child
        .wait_with_output()
        .map_err(|e| format!("affine connector did not complete: {e}"))?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("affine connector failed: {}", stderr.trim()));
    }
    Ok(())
}

fn write_to_connection(app: &AppHandle, conn: &Connection, text: &str) -> Result<(), String> {
    match conn.kind.as_str() {
        "markdown" | "obsidian" => {
            let path = conn
                .path
                .as_ref()
                .ok_or_else(|| format!("connection '{}' missing 'path'", conn.name))?;
            let dir = resolve_dir(path)?;
            write_markdown(&dir, text)
        }
        "logseq" => {
            let path = conn
                .path
                .as_ref()
                .ok_or_else(|| format!("connection '{}' missing 'path'", conn.name))?;
            let dir = resolve_dir(path)?;
            write_logseq(&dir, text)
        }
        "affine" => write_affine(app, conn, text),
        other => Err(format!(
            "connection '{}' has unknown type '{}'",
            conn.name, other
        )),
    }
}

fn toggle_note_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("note") {
        let visible = window.is_visible().unwrap_or(false);
        if visible {
            let _ = window.hide();
        } else {
            let _ = window.show();
            let _ = window.set_focus();
            let _ = window.emit("note-shown", ());
        }
    }
}

fn register_toggle_shortcut(app: &AppHandle, shortcut_str: &str) -> Result<Shortcut, String> {
    let shortcut: Shortcut = shortcut_str
        .parse()
        .map_err(|e| format!("invalid toggle_note shortcut '{shortcut_str}': {e}"))?;
    app.global_shortcut()
        .register(shortcut)
        .map_err(|e| format!("failed to register global shortcut '{shortcut_str}': {e}"))?;
    Ok(shortcut)
}

#[tauri::command]
fn get_config(app: AppHandle) -> Result<AppConfig, String> {
    load_config(&app)
}

#[tauri::command]
fn save_note(app: AppHandle, text: String) -> Result<SaveResult, String> {
    let trimmed = text.trim();
    if trimmed.is_empty() {
        return Ok(SaveResult {
            targets: vec![],
            delivered: vec![],
            pending: vec![],
            error: None,
        });
    }

    let config = load_config(&app)?;
    let tags = extract_tags(trimmed);
    let target_names = resolve_targets(&config, &tags);

    // Durable store FIRST: the note is safe even if every connector is down.
    // A failure here is the only fatal case, since we can't guarantee the note.
    let conn = store::open(&db_path()?)?;
    let id = store::insert(
        &conn,
        &Utc::now().to_rfc3339(),
        trimmed,
        &tags.join(","),
        &target_names.join(","),
        "tauri",
    )?;

    // Then attempt delivery. Connector failures are non-fatal: the row stays at
    // processed = 0 as backlog and we tell the UI what's still pending.
    let mut delivered = Vec::new();
    let mut pending = Vec::new();
    let mut errors = Vec::new();
    for name in &target_names {
        let result = match config.connection(name) {
            Some(c) => write_to_connection(&app, c, trimmed),
            None => Err(format!(
                "routing points to connection '{name}' which is not defined"
            )),
        };
        match result {
            Ok(()) => delivered.push(name.clone()),
            Err(e) => {
                pending.push(name.clone());
                errors.push(format!("{name}: {e}"));
            }
        }
    }

    if errors.is_empty() {
        let _ = store::mark_processed(&conn, id, &Utc::now().to_rfc3339());
    } else {
        let _ = store::mark_error(&conn, id, &errors.join("; "));
    }

    Ok(SaveResult {
        targets: target_names,
        delivered,
        pending,
        error: if errors.is_empty() {
            None
        } else {
            Some(errors.join("; "))
        },
    })
}

#[tauri::command]
fn hide_note_window(app: AppHandle) {
    if let Some(window) = app.get_webview_window("note") {
        let _ = window.hide();
    }
}

#[tauri::command]
fn reload_config(app: AppHandle) -> Result<(), String> {
    let config = load_config(&app)?;
    let state = app.state::<AppState>();

    let old_shortcut = *state.toggle_shortcut.lock().unwrap();
    let _ = app.global_shortcut().unregister(old_shortcut);

    let new_shortcut = register_toggle_shortcut(&app, &config.shortcuts.toggle_note)?;
    *state.toggle_shortcut.lock().unwrap() = new_shortcut;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg(tags: &[(&str, &str)], default: &str) -> AppConfig {
        AppConfig {
            shortcuts: ShortcutsConfig {
                toggle_note: "Alt+Space".into(),
                save_note: "Alt+Enter".into(),
            },
            connections: vec![],
            routing: RoutingConfig {
                tags: tags.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect(),
                default: default.into(),
            },
        }
    }

    #[test]
    fn tags_require_no_space_after_hash() {
        assert_eq!(extract_tags("#work note"), vec!["work"]);
        assert_eq!(extract_tags("a #work b"), vec!["work"]);
        // "# heading" is markdown, not a tag.
        assert!(extract_tags("# heading text").is_empty());
        // mid-word # is not a tag boundary
        assert!(extract_tags("email a#b.com").is_empty());
    }

    #[test]
    fn tags_are_deduped_and_trimmed() {
        assert_eq!(extract_tags("#work and #work."), vec!["work"]);
        assert_eq!(extract_tags("#idea, then #work!"), vec!["idea", "work"]);
        assert_eq!(extract_tags("#nested/tag ok"), vec!["nested/tag"]);
    }

    #[test]
    fn no_tag_routes_to_default() {
        let c = cfg(&[("work", "work_conn")], "personal");
        assert_eq!(resolve_targets(&c, &[]), vec!["personal"]);
    }

    #[test]
    fn unmapped_tag_falls_back_to_default() {
        let c = cfg(&[("work", "work_conn")], "personal");
        assert_eq!(resolve_targets(&c, &["random".into()]), vec!["personal"]);
    }

    #[test]
    fn multiple_tags_hit_each_mapped_connection_deduped() {
        let c = cfg(&[("work", "w"), ("idea", "i"), ("todo", "w")], "personal");
        let targets = resolve_targets(&c, &["work".into(), "idea".into(), "todo".into()]);
        assert_eq!(targets, vec!["w", "i"]); // "w" not duplicated
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, shortcut, event| {
                    if event.state() != ShortcutState::Pressed {
                        return;
                    }
                    let state = app.state::<AppState>();
                    let current = *state.toggle_shortcut.lock().unwrap();
                    if shortcut == &current {
                        toggle_note_window(app);
                    }
                })
                .build(),
        )
        .invoke_handler(tauri::generate_handler![
            get_config,
            save_note,
            hide_note_window,
            reload_config
        ])
        .setup(|app| {
            let handle = app.handle().clone();
            let config = load_config(&handle)?;
            let shortcut = register_toggle_shortcut(&handle, &config.shortcuts.toggle_note)?;
            app.manage(AppState {
                toggle_shortcut: Mutex::new(shortcut),
            });

            let reload_item = MenuItem::with_id(app, "reload", "Reload Config", true, None::<&str>)?;
            let exit_item = MenuItem::with_id(app, "exit", "Exit", true, None::<&str>)?;
            let tray_menu = Menu::with_items(app, &[&reload_item, &exit_item])?;

            TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .tooltip("QuickNote")
                .menu(&tray_menu)
                .show_menu_on_left_click(false)
                .on_menu_event(|app, event| match event.id().as_ref() {
                    "reload" => {
                        if let Err(e) = reload_config(app.clone()) {
                            eprintln!("reload_config failed: {e}");
                        }
                    }
                    "exit" => {
                        app.exit(0);
                    }
                    _ => {}
                })
                .build(app)?;

            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                // Hitting Escape/close on the note popup should just hide it, not quit the app.
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
