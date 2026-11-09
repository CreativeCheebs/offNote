#pragma once
#include "config.h"

#include <QWidget>
#include <functional>

class QCheckBox;
class QKeyEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;
class QFormLayout;
class ShortcutEdit;

// The Settings window: connections, tag routing, shortcuts, general options and the outbox.
// Edits go into a working copy and are only written to config.yaml on Save.
class SettingsWindow : public QWidget {
public:
    enum Page { Connections, Routing, Shortcuts, General, Outbox };

    // `applied` runs after config.yaml is saved and returns a warning (empty if all is well),
    // e.g. when the new global hotkey could not be registered. `retryNow` kicks delivery.
    SettingsWindow(const AppConfig &initial, std::function<QString()> applied, std::function<void()> retryNow);
    void showPage(Page page);
    void refreshOutbox();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *buildConnectionsPage();
    QWidget *buildRoutingPage();
    QWidget *buildShortcutsPage();
    QWidget *buildGeneralPage();
    QWidget *buildOutboxPage();

    void markDirty();
    void save();
    void selectConnection(int row);
    void loadConnectionForm();
    void refreshConnectionState();
    void rebuildRouting();
    void refreshRoutePreview();
    void runConnectionTest();

    AppConfig draft;
    std::function<QString()> applied;
    std::function<void()> retryNow;
    bool dirty = false;
    bool loading = false;
    bool testing = false;
    bool autostartWanted = false;

    QListWidget *nav = nullptr;
    QStackedWidget *stack = nullptr;
    QLabel *errorLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *saveButton = nullptr;

    // Connections page
    QListWidget *connList = nullptr;
    QWidget *connForm = nullptr;
    QLabel *connEmpty = nullptr;
    QFormLayout *form = nullptr;
    QLineEdit *nameEdit = nullptr;
    QComboBox *typeCombo = nullptr;
    QWidget *pathRow = nullptr;
    QLineEdit *pathEdit = nullptr;
    QLineEdit *urlEdit = nullptr;
    QWidget *tokenRow = nullptr;
    QLineEdit *tokenEdit = nullptr;
    QLineEdit *connectorEdit = nullptr;
    QLabel *connError = nullptr;
    QLabel *testResult = nullptr;
    QPushButton *testButton = nullptr;

    // Routing page
    QComboBox *defaultCombo = nullptr;
    QVBoxLayout *rulesLayout = nullptr;
    QLineEdit *previewEdit = nullptr;
    QLabel *previewLabel = nullptr;

    // Shortcuts page
    ShortcutEdit *toggleEdit = nullptr;
    ShortcutEdit *saveEdit = nullptr;
    QLabel *shortcutError = nullptr;

    // Outbox page
    QTableWidget *outboxTable = nullptr;
    QLabel *outboxSummary = nullptr;
};
