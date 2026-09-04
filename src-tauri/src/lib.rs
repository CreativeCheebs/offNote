use std::fs::{self, OpenOptions};
use std::io::Write as _;
use std::path::PathBuf;
use std::sync::Mutex;

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

#[derive(Debug, Clone, Deserialize, Serialize)]
struct NotesConfig {
    directory: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
struct AppConfig {
    shortcuts: ShortcutsConfig,
    notes: NotesConfig,
}

struct AppState {
    toggle_shortcut: Mutex<Shortcut>,
}

fn config_path(app: &AppHandle) -> PathBuf {
    if let Ok(resource_path) = app
        .path()
        .resolve("config.yaml", tauri::path::BaseDirectory::Resource)
    {
        if resource_path.exists() {
            return resource_path;
        }
    }
    // Dev fallback: read straight from the src-tauri source tree.
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("config.yaml")
}

fn load_config(app: &AppHandle) -> Result<AppConfig, String> {
    let path = config_path(app);
    let raw = fs::read_to_string(&path)
        .map_err(|e| format!("failed to read config.yaml at {}: {e}", path.display()))?;
    serde_yaml::from_str(&raw).map_err(|e| format!("failed to parse config.yaml: {e}"))
}

fn notes_dir(app: &AppHandle, config: &AppConfig) -> Result<PathBuf, String> {
    let configured = PathBuf::from(&config.notes.directory);
    if configured.is_absolute() {
        return Ok(configured);
    }
    let base = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("failed to resolve app data dir: {e}"))?;
    Ok(base.join(configured))
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
fn save_note(app: AppHandle, text: String) -> Result<(), String> {
    let trimmed = text.trim();
    if trimmed.is_empty() {
        return Ok(());
    }

    let config = load_config(&app)?;
    let dir = notes_dir(&app, &config)?;
    fs::create_dir_all(&dir).map_err(|e| format!("failed to create notes dir: {e}"))?;

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

    writeln!(file, "## {}\n\n{}\n", now.format("%H:%M:%S"), trimmed)
        .map_err(|e| format!("failed to write note: {e}"))?;

    Ok(())
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
