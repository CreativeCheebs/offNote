#include <gtk/gtk.h>
#include <gdk/gdkwin32.h>
#include <sqlite3.h>
#include <windows.h>

static GtkWidget *window, *editor, *status;
static GtkStatusIcon *tray;
static HWND hotkey_window;
static gboolean hotkey_registered;

static void show_note(void) {
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(editor);
}

static void toggle_note(void) {
    if (gtk_widget_get_visible(window)) gtk_widget_hide(window);
    else show_note();
}

static gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
    gtk_widget_hide(widget);
    return TRUE;
}

static GdkFilterReturn hotkey_filter(GdkXEvent *event, GdkEvent *gdk_event, gpointer data) {
    MSG *message = (MSG *)event;
    if (message->message == WM_HOTKEY && message->wParam == 1) {
        toggle_note();
        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

static void save_note(GtkWidget *button, gpointer data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *note = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!g_strstrip(note)[0]) {
        gtk_label_set_text(GTK_LABEL(status), "Write a note first.");
        g_free(note);
        return;
    }
    const gchar *appdata = g_getenv("APPDATA");
    gchar *data_dir = g_build_filename(appdata ? appdata : g_get_user_config_dir(), "QuickNote", NULL);
    g_mkdir_with_parents(data_dir, 0700);
    gchar *db_path = g_build_filename(data_dir, "quicknote.db", NULL);
    sqlite3 *db = NULL;
    int rc = sqlite3_open(db_path, &db);
    if (rc == SQLITE_OK) {
        sqlite3_busy_timeout(db, 5000);
        rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT NOT NULL, data TEXT NOT NULL, tags TEXT, targets TEXT, processed INTEGER NOT NULL DEFAULT 0, processed_at TEXT, error TEXT, source TEXT);", NULL, NULL, NULL);
    }
    if (rc == SQLITE_OK) {
        sqlite3_stmt *stmt = NULL;
        rc = sqlite3_prepare_v2(db, "INSERT INTO notes (created_at,data,tags,targets,processed,source) VALUES (?1,?2,'','',0,'gtk')", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            GDateTime *now = g_date_time_new_now_utc();
            gchar *created = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
            sqlite3_bind_text(stmt, 1, created, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, note, -1, SQLITE_TRANSIENT);
            rc = sqlite3_step(stmt);
            g_free(created);
            g_date_time_unref(now);
        }
        sqlite3_finalize(stmt);
    }
    if (rc == SQLITE_DONE) {
        gtk_text_buffer_set_text(buffer, "", -1);
        gtk_label_set_text(GTK_LABEL(status), "Saved to local outbox.");
        gtk_widget_hide(window);
    } else {
        gtk_label_set_text(GTK_LABEL(status), db ? sqlite3_errmsg(db) : "Could not open database.");
    }
    if (db) sqlite3_close(db);
    g_free(db_path);
    g_free(data_dir);
    g_free(note);
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    if (event->keyval == GDK_KEY_Escape) { gtk_widget_hide(window); return TRUE; }
    if (event->keyval == GDK_KEY_Return && (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
        save_note(NULL, NULL); return TRUE;
    }
    return FALSE;
}

static void quit_app(GtkMenuItem *item, gpointer data) {
    if (hotkey_registered) UnregisterHotKey(hotkey_window, 1);
    gtk_main_quit();
}

static void tray_menu(GtkStatusIcon *icon, guint button, guint time, gpointer data) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *open = gtk_menu_item_new_with_label("New note");
    GtkWidget *quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect_swapped(open, "activate", G_CALLBACK(show_note), NULL);
    g_signal_connect(quit, "activate", G_CALLBACK(quit_app), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Offnote · GTK");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 300);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 18);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), box);
    GtkWidget *title = gtk_label_new("Capture a thought");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    editor = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scroll), editor);
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(box), footer, FALSE, FALSE, 0);
    status = gtk_label_new("Alt+Space to open · Ctrl+Enter to save");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(footer), status, TRUE, TRUE, 0);
    GtkWidget *save = gtk_button_new_with_label("Save note");
    gtk_style_context_add_class(gtk_widget_get_style_context(save), "suggested-action");
    g_signal_connect(save, "clicked", G_CALLBACK(save_note), NULL);
    gtk_box_pack_end(GTK_BOX(footer), save, FALSE, FALSE, 0);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, ".title { font-size: 20px; font-weight: bold; } textview { font-size: 15px; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_widget_realize(window);
    hotkey_window = GDK_WINDOW_HWND(gtk_widget_get_window(window));
    gdk_window_add_filter(NULL, hotkey_filter, NULL);
    hotkey_registered = RegisterHotKey(hotkey_window, 1, MOD_ALT | MOD_NOREPEAT, VK_SPACE);
    if (!hotkey_registered) gtk_label_set_text(GTK_LABEL(status), "Alt+Space unavailable; use tray to open.");

    tray = gtk_status_icon_new_from_icon_name("accessories-text-editor");
    gtk_status_icon_set_tooltip_text(tray, "Offnote GTK");
    gtk_status_icon_set_visible(tray, TRUE);
    g_signal_connect_swapped(tray, "activate", G_CALLBACK(show_note), NULL);
    g_signal_connect(tray, "popup-menu", G_CALLBACK(tray_menu), NULL);
    show_note();
    gtk_main();
    return 0;
}
