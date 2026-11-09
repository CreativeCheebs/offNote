#include "config.h"
#include "delivery.h"
#include "router.h"
#include "settingswindow.h"
#include "shortcut.h"
#include "store.h"

#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <windows.h>

class CaptureWindow : public QWidget {
public:
    std::function<void()> onSaved;  // called after a note has been written to the outbox

    CaptureWindow() {
        setWindowTitle("Offnote");
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        resize(480, 250);
        setMinimumSize(420, 210);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(1, 1, 1, 1);
        layout->setSpacing(0);

        auto *content = new QFrame(this);
        content->setObjectName("content");
        auto *body = new QVBoxLayout(content);
        body->setContentsMargins(23, 21, 23, 21);
        body->setSpacing(12);
        auto *eyebrow = new QLabel("QUICK CAPTURE");
        eyebrow->setObjectName("eyebrow");
        body->addWidget(eyebrow);
        editor = new QTextEdit();
        editor->setPlaceholderText("What’s on your mind?");
        body->addWidget(editor, 1);
        auto *footer = new QHBoxLayout();
        status = new QLabel();
        status->setObjectName("status");
        footer->addWidget(status, 1);
        body->addLayout(footer);
        layout->addWidget(content, 1);
        setHotkey("Alt+Space");
        saveTimer.setSingleShot(true);
        connect(&saveTimer, &QTimer::timeout, this, [this] {
            editor->setStyleSheet(QString());
            status->setStyleSheet(QString());
            status->setText(hint);
            hideNote();
        });
        editor->installEventFilter(this);
        editor->setAccessibleName("Quick note");
        fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(150);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(fade, &QPropertyAnimation::finished, this, [this] {
            if (hiding) { hide(); setWindowOpacity(1.0); hiding = false; }
        });
        setStyleSheet(
            "CaptureWindow { background: #15171d; border: 1px solid #363b48; }"
            "QFrame#content { background: #15171d; }"
            "QLabel { background: transparent; color: #eef0f7; }"
            "QLabel#eyebrow { color: #8296e8; font-size: 10px; font-weight: 800; letter-spacing: 2px; }"
            "QLabel#status { color: #8d94a7; font-size: 9px; font-weight: 600; }"
            "QTextEdit { background: #20242e; color: #f2f3f7; border: 1px solid #393f4f;"
            " border-radius: 10px; padding: 11px; font-size: 15px; selection-background-color: #5d72c6; }"
            "QTextEdit:hover { border-color: #596278; }"
            "QTextEdit:focus { border: 1px solid #8ba0f4; }"
            "QScrollBar:vertical { background: #20242e; width: 9px; }"
            "QScrollBar::handle:vertical { background: #555d70; border-radius: 4px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background: #7a849c; }"
        );
    }
    // The footer hint follows the configured shortcut; empty text means no global hotkey.
    void setHotkey(const QString &text, const QString &saveText = "Alt+Enter") {
        const QString open = text.isEmpty() ? QString("OPEN FROM THE TRAY ICON")
                                            : text.toUpper().replace("+", " + ") + "  TO OPEN";
        QString save = "CTRL + ENTER";  // always works
        if (saveText != "Alt+Enter" && saveText != "Ctrl+Enter") save += " OR " + saveText.toUpper().replace("+", " + ");
        hint = open + "    ·    " + save + "  TO SAVE    ·    ESC  TO HIDE";
        status->setText(hint);
        status->setStyleSheet(QString());
    }
    void setSaveShortcut(const Shortcut &s) { saveShortcut = s; }
    void showNotice(const QString &text) { status->setText(text); }
    void openNote() {
        saveTimer.stop();  // a reopen must not be hidden by the previous save's timer
        editor->setStyleSheet(QString());
        status->setStyleSheet(QString());
        status->setText(hint);
        fade->stop();
        hiding = false;
        setWindowOpacity(0.0);
        showNormal();
        raise();
        activateWindow();
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start();
        editor->setFocus();
    }
    void toggleNote() { isVisible() && !isMinimized() ? hideNote() : openNote(); }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && windowHandle()) windowHandle()->startSystemMove();
        else QWidget::mousePressEvent(event);
    }
    void closeEvent(QCloseEvent *event) override { hideNote(); event->ignore(); }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == editor && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) { hideNote(); return true; }
            if (isSaveKey(key)) { saveNote(); return true; }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    bool isSaveKey(const QKeyEvent *key) const {
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) return true;  // always work
        if (!saveShortcut.valid() || key->nativeVirtualKey() != saveShortcut.vk) return false;
        unsigned mods = 0;
        if (key->modifiers() & Qt::ControlModifier) mods |= kModControl;
        if (key->modifiers() & Qt::AltModifier) mods |= kModAlt;
        if (key->modifiers() & Qt::ShiftModifier) mods |= kModShift;
        if (key->modifiers() & Qt::MetaModifier) mods |= kModWin;
        return mods == saveShortcut.mods;
    }
    // Small confirmation: the note box flashes green and the status says so, then the
    // window fades away. Cleared again before the next open.
    void showSaved(const QString &message, int holdMs) {
        editor->setStyleSheet("QTextEdit, QTextEdit:hover, QTextEdit:focus { border: 1px solid #5ae08a; }");
        status->setStyleSheet("QLabel#status { color: #5ae08a; }");
        status->setText(message);
        saveTimer.start(holdMs);
    }
    void showError(const QString &message) {
        status->setStyleSheet("QLabel#status { color: #ff8a7a; }");
        status->setText(message);
    }
    void hideNote() {
        if (!isVisible()) return;
        fade->stop();
        hiding = true;
        fade->setStartValue(windowOpacity());
        fade->setEndValue(0.0);
        fade->start();
    }
    // The note is written to the local outbox first, with its tags and routed destinations;
    // delivery happens in the background, so saving never waits on the network.
    void saveNote() {
        const QString note = editor->toPlainText().trimmed();
        if (note.isEmpty()) { showError("Write a note first."); return; }

        AppConfig config;
        QString configError;
        const bool configOk = loadConfig(config, &configError);
        const QStringList tags = extractTags(note);
        // With an unreadable config the note is still saved (no targets); delivery routes it later.
        const QStringList targets = configOk ? resolveTargets(config, tags) : QStringList();

        Store store;
        QString error;
        if (!store.open(dbPath(), &error) ||
            !store.insert(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), note, tags.join(','), targets.join(','), "qt", &error)) {
            showError(error.isEmpty() ? QString("Could not save the note.") : error);
            return;  // the text stays in the box
        }
        editor->clear();
        if (onSaved) onSaved();
        if (!configOk) showSaved("Saved locally. config.yaml has a problem: open Settings from the tray", 1800);
        else if (targets.isEmpty()) showSaved("Saved locally, but no default connection is set: open Settings", 1800);
        else showSaved("Saved to local outbox", 700);
    }
    QTextEdit *editor;
    QLabel *status;
    QPropertyAnimation *fade;
    QTimer saveTimer;
    QString hint;
    Shortcut saveShortcut;
    bool hiding = false;
};

class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    explicit HotkeyFilter(CaptureWindow *window) : window(window) {}
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == 1) { window->toggleNote(); return true; }
        return false;
    }
private:
    CaptureWindow *window;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const QIcon icon(":/app.png");
    app.setWindowIcon(icon);

    CaptureWindow window;
    HotkeyFilter filter(&window);
    app.installNativeEventFilter(&filter);
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());

    Delivery delivery;
    QPointer<SettingsWindow> settings;
    Shortcut registered;
    bool hotkeyOn = false;
    int pendingCount = 0;

    QSystemTrayIcon tray(icon, &app);
    QMenu menu;
    QAction *outboxAction = nullptr;

    auto updateTray = [&] {
        tray.setToolTip(pendingCount > 0 ? QStringLiteral("Offnote - %1 note(s) waiting to be delivered").arg(pendingCount)
                                         : QStringLiteral("Offnote"));
        if (outboxAction) outboxAction->setText(pendingCount > 0 ? QStringLiteral("Outbox (%1 waiting)").arg(pendingCount) : QStringLiteral("Outbox"));
    };

    // Reads config.yaml and applies it: save shortcut, hint text and the global hotkey. Returns
    // a warning for the caller to show, or an empty string when everything took effect.
    auto applyConfig = [&]() -> QString {
        AppConfig config;  // defaults (Alt+Space) if the file cannot be read
        QString error;
        const bool loaded = loadConfig(config, &error);
        const QString problem = loaded ? QString() : "Config problem: " + error;
        if (!loaded && hotkeyOn) return problem;  // keep what is registered
        const Shortcut want = parseShortcut(config.toggleNote);
        if (!want.valid() || want.mods == 0) return "The open-note shortcut in config.yaml is not valid; keeping the previous one.";
        window.setSaveShortcut(parseShortcut(config.saveNote));
        if (hotkeyOn && want.mods == registered.mods && want.vk == registered.vk) {
            window.setHotkey(config.toggleNote, config.saveNote);
            return problem;
        }
        const bool had = hotkeyOn;
        if (had) UnregisterHotKey(hwnd, 1);
        if (RegisterHotKey(hwnd, 1, want.mods | MOD_NOREPEAT, want.vk)) {
            registered = want;
            hotkeyOn = true;
            window.setHotkey(config.toggleNote, config.saveNote);
            return problem;
        }
        hotkeyOn = had && RegisterHotKey(hwnd, 1, registered.mods | MOD_NOREPEAT, registered.vk);
        if (!hotkeyOn) window.setHotkey(QString());
        return QStringLiteral("%1 is already used by another program; the previous shortcut was kept.").arg(config.toggleNote);
    };

    auto openSettings = [&](SettingsWindow::Page page) {
        if (!settings) {
            AppConfig config;
            QString error;
            if (!loadConfig(config, &error)) {
                // Never open the editor on an unreadable file: saving would replace it with an empty config.
                QMessageBox::warning(nullptr, "Offnote Settings",
                                     "config.yaml could not be read, so Settings was not opened.\n\n" + error +
                                         "\n\nFix the file by hand (" + QDir::toNativeSeparators(configPath()) + "), then try again.");
                return;
            }
            settings = new SettingsWindow(config, [&] { return applyConfig(); }, [&] { delivery.kick(); });
        }
        settings->showPage(page);
    };

    delivery.onFinished = [&](const RetryResult &result) {
        if (!result.skipped) {
            pendingCount = result.pending;
            updateTray();
        }
        if (settings) settings->refreshOutbox();
    };
    window.onSaved = [&] {
        ++pendingCount;  // until the background pass reports the real number
        updateTray();
        delivery.kick();
    };

    menu.addAction("New note", &window, &CaptureWindow::openNote);
    outboxAction = menu.addAction("Outbox");
    QObject::connect(outboxAction, &QAction::triggered, [&] { openSettings(SettingsWindow::Outbox); });
    QObject::connect(menu.addAction("Settings..."), &QAction::triggered, [&] { openSettings(SettingsWindow::Connections); });
    QObject::connect(menu.addAction("Retry pending now"), &QAction::triggered, [&] { delivery.kick(); });
    QObject::connect(menu.addAction("Reload config"), &QAction::triggered, [&] {
        const QString warning = applyConfig();
        tray.showMessage("Offnote", warning.isEmpty() ? QString("Config reloaded.") : warning);
        delivery.kick();
    });
    menu.addSeparator();
    menu.addAction("Quit", &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &window, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) window.openNote();
    });
    tray.show();

    const QString warning = applyConfig();
    if (!warning.isEmpty()) window.showNotice(warning);
    {
        Store store;
        QString ignored;
        if (store.open(dbPath(), &ignored)) pendingCount = store.pendingCount();
        updateTray();
    }

    // Deliver anything left over from last time, then sweep every 30 seconds so notes queued
    // while a destination was unreachable still go out on their own.
    QTimer sweep;
    QObject::connect(&sweep, &QTimer::timeout, [&] { delivery.kick(); });
    sweep.start(30000);
    delivery.kick();

    QObject::connect(&app, &QApplication::aboutToQuit, [&] {
        tray.hide();
        Delivery::waitForIdle(5000);
    });

    window.openNote();
    if (app.arguments().contains("--settings")) openSettings(SettingsWindow::Connections);  // also handy for screenshots
    const int result = app.exec();
    if (hotkeyOn) UnregisterHotKey(hwnd, 1);
    return result;
}
