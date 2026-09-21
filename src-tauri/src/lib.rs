mod store;

use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::Write as _;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

use chrono::Utc;
use serde::{Deserialize, Serialize};
use tauri::menu::{Menu, MenuItem};
use tauri::tray::TrayIconBuilder;
use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindowBuilder};
use tauri_plugin_global_shortcut::{GlobalShortcutExt, Shortcut, ShortcutState};

#[derive(Debug, Clone, Deserialize, Serialize)]
struct ShortcutsConfig {
    toggle_note: String,
    save_note: String,
}

/// A destination a note can be written to. `kind` selects the connector;
/// `path` is used by the file-based kinds (markdown/obsidian/logseq). The
/// `affine` kind never holds AFFiNE credentials here - those live
/// server-side in the sidecar's own connectors.json (see
/// connectors/affine-sidecar); this only names which sidecar connector to
/// call, matching the Android app's config shape.
#[derive(Debug, Clone, Deserialize, Serialize)]
struct Connection {
    name: String,
    #[serde(rename = "type")]
    kind: String,
    #[serde(default)]
    path: Option<String>,
    #[serde(default)]
    sidecar_url: Option<String>,
    #[serde(default)]
    sidecar_token: Option<String>,
    #[serde(default)]
    sidecar_connector: Option<String>,
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
    /// always empty: delivery is never attempted inline (see `save_note`)
    delivered: Vec<String>,
    /// all routed targets, queued in SQLite for the background retry worker
    pending: Vec<String>,
    /// aggregated delivery error, if any (note is still saved durably)
    error: Option<String>,
}

struct AppState {
    toggle_shortcut: Mutex<Shortcut>,
    /// Guards `retry_pending` against overlapping runs: the immediate
    /// post-save attempt and the periodic sweep can otherwise land at the
    /// same moment and double-attempt the same backlog row.
    retry_running: AtomicBool,
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

/// Push `text` into AFFiNE via the HTTP sidecar (connectors/affine-sidecar).
/// The sidecar - not this app - holds the actual AFFiNE credentials and runs
/// the socket.io + Yjs CRDT sync; this is a plain authenticated POST.
fn write_affine(_app: &AppHandle, conn: &Connection, text: &str) -> Result<(), String> {
    let sidecar_url = conn
        .sidecar_url
        .as_ref()
        .ok_or_else(|| format!("connection '{}' missing 'sidecar_url'", conn.name))?;
    let sidecar_token = conn
        .sidecar_token
        .as_ref()
        .ok_or_else(|| format!("connection '{}' missing 'sidecar_token'", conn.name))?;
    let sidecar_connector = conn
        .sidecar_connector
        .as_ref()
        .ok_or_else(|| format!("connection '{}' missing 'sidecar_connector'", conn.name))?;

    let url = format!("{}/append", sidecar_url.trim_end_matches('/'));
    let body = serde_json::json!({
        "connector": sidecar_connector,
        "texts": [text],
    });

    let response = ureq::post(&url)
        .set("Authorization", &format!("Bearer {sidecar_token}"))
        .send_json(body);

    match response {
        Ok(_) => Ok(()),
        Err(ureq::Error::Status(status, resp)) => {
            let body = resp.into_string().unwrap_or_default();
            Err(format!("sidecar returned {status}: {body}"))
        }
        Err(e) => Err(format!("failed to reach affine sidecar: {e}")),
    }
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

/// Fires the confetti celebration in its own always-on-top overlay window,
/// independent of the note popup - so the popup can close the instant a note
/// is saved without cutting the celebration off with it.
#[tauri::command]
fn trigger_celebration(app: AppHandle) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("confetti") {
        window.show().map_err(|e| e.to_string())?;
        // Both this window and "note" are always-on-top; show() alone doesn't
        // reorder within that band, so without this the note popup (raised
        // more recently, when it was opened) stays stacked above the confetti
        // that just appeared underneath it.
        window.set_focus().map_err(|e| e.to_string())?;
        window.emit("celebrate", ()).map_err(|e| e.to_string())?;
    }
    Ok(())
}

/// Attempts delivery for every backlog row (processed = 0), so this single
/// function serves both the immediate post-save push and the periodic sweep.
/// A row's `targets` column is narrowed to whatever remains undelivered after
/// each attempt, so a retry never re-posts to a target that already
/// succeeded - important since a note can route to several connections and
/// only some of them may be unreachable at any given moment.
fn retry_pending(app: &AppHandle) {
    let state = app.state::<AppState>();
    if state.retry_running.swap(true, Ordering::SeqCst) {
        return; // another pass (immediate or periodic) is already in flight
    }

    let outcome: Result<(), String> = (|| {
        let config = load_config(app)?;
        let conn = store::open(&db_path()?)?;
        for (id, data, targets_csv) in store::pending(&conn)? {
            let targets: Vec<String> = targets_csv
                .split(',')
                .map(|s| s.trim().to_string())
                .filter(|s| !s.is_empty())
                .collect();
            if targets.is_empty() {
                let _ = store::mark_processed(&conn, id, &Utc::now().to_rfc3339());
                continue;
            }

            let mut remaining = Vec::new();
            let mut errors = Vec::new();
            for name in &targets {
                let result = match config.connection(name) {
                    Some(c) => write_to_connection(app, c, &data),
                    None => Err(format!(
                        "routing points to connection '{name}' which is not defined"
                    )),
                };
                if let Err(e) = result {
                    remaining.push(name.clone());
                    errors.push(format!("{name}: {e}"));
                }
            }

            if remaining.is_empty() {
                let _ = store::mark_processed(&conn, id, &Utc::now().to_rfc3339());
            } else {
                let _ = store::update_targets(&conn, id, &remaining.join(","));
                let _ = store::mark_error(&conn, id, &errors.join("; "));
            }
        }
        Ok(())
    })();

    if let Err(e) = outcome {
        eprintln!("retry_pending failed: {e}");
    }
    state.retry_running.store(false, Ordering::SeqCst);
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

    // Durable store, and ONLY that, before returning: delivery (which may
    // block on a slow or unreachable connector, e.g. affine over the network)
    // happens in the background so the caller can close the note popup
    // immediately regardless of network state. `retry_pending` below picks
    // this row up right away, and again every 30s until it succeeds.
    let conn = store::open(&db_path()?)?;
    store::insert(
        &conn,
        &Utc::now().to_rfc3339(),
        trimmed,
        &tags.join(","),
        &target_names.join(","),
        "tauri",
    )?;

    let handle = app.clone();
    std::thread::spawn(move || retry_pending(&handle));

    Ok(SaveResult {
        targets: target_names.clone(),
        delivered: vec![],
        pending: target_names,
        error: None,
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
            reload_config,
            trigger_celebration
        ])
        .setup(|app| {
            let handle = app.handle().clone();
            let config = load_config(&handle)?;
            let shortcut = register_toggle_shortcut(&handle, &config.shortcuts.toggle_note)?;
            app.manage(AppState {
                toggle_shortcut: Mutex::new(shortcut),
                retry_running: AtomicBool::new(false),
            });

            // Periodic sweep so a note queued while offline (or while a
            // connector is down) still goes out on its own once the network
            // or server recovers, without requiring another save to trigger it.
            {
                let retry_handle = handle.clone();
                std::thread::spawn(move || loop {
                    std::thread::sleep(std::time::Duration::from_secs(30));
                    retry_pending(&retry_handle);
                });
            }

            // A separate, click-through, full-screen overlay for the save
            // celebration - kept apart from the note window so the popup can
            // hide the instant a note is saved without cutting the confetti
            // off with it.
            let confetti_builder =
                WebviewWindowBuilder::new(&handle, "confetti", WebviewUrl::App("confetti.html".into()))
                    .title("Offnote Celebration")
                    .decorations(false)
                    .transparent(true)
                    .always_on_top(true)
                    .skip_taskbar(true)
                    .visible(false)
                    .resizable(false)
                    .focused(false)
                    .shadow(false);
            let confetti_builder = match handle.primary_monitor() {
                Ok(Some(monitor)) => {
                    let scale = monitor.scale_factor();
                    let size = monitor.size();
                    let position = monitor.position();
                    confetti_builder
                        .inner_size(size.width as f64 / scale, size.height as f64 / scale)
                        .position(position.x as f64 / scale, position.y as f64 / scale)
                }
                _ => confetti_builder.inner_size(1024.0, 768.0),
            };
            confetti_builder.build()?;

            let reload_item = MenuItem::with_id(app, "reload", "Reload Config", true, None::<&str>)?;
            let exit_item = MenuItem::with_id(app, "exit", "Exit", true, None::<&str>)?;
            let tray_menu = Menu::with_items(app, &[&reload_item, &exit_item])?;

            TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .tooltip("Offnote")
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
