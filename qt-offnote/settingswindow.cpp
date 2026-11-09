#include "settingswindow.h"

#include "delivery.h"
#include "router.h"
#include "shortcut.h"
#include "store.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

// All text in this window is plain ASCII on purpose: one glyph missing from the UI font makes
// Windows load fallback fonts, which cost hundreds of MB of memory in an earlier experiment.

namespace {

const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char kRunValue[] = "OffnoteQt";

bool autostartEnabled() { return QSettings(kRunKey, QSettings::NativeFormat).contains(kRunValue); }

void setAutostart(bool on) {
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on) run.setValue(kRunValue, "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"");
    else run.remove(kRunValue);
}

void setInvalid(QWidget *w, bool invalid) {
    if (w->property("invalid").toBool() == invalid) return;
    w->setProperty("invalid", invalid);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void setTone(QLabel *label, const char *tone, const QString &text) {
    label->setProperty("tone", tone);
    label->setText(text);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QLabel *heading(const QString &title, const QString &hint) {
    auto *l = new QLabel("<b style='font-size:17px'>" + title + "</b><br><span style='color:#8d94a7'>" + hint + "</span>");
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    return l;
}

QPushButton *makeButton(const QString &text, const char *role = nullptr) {
    auto *b = new QPushButton(text);
    b->setAutoDefault(false);
    b->setCursor(Qt::PointingHandCursor);
    if (role) b->setProperty("role", role);
    return b;
}

const char kStyle[] =
    "SettingsWindow, QWidget#page { background: #15171d; }"
    "QWidget { color: #eef0f7; font-size: 13px; }"
    "QLabel { background: transparent; }"
    "QLabel[tone=\"error\"] { color: #ff8a7a; }"
    "QLabel[tone=\"ok\"] { color: #5ae08a; }"
    "QLabel[tone=\"muted\"] { color: #8d94a7; }"
    "QListWidget#nav { background: #101218; border: none; border-right: 1px solid #262a35; padding-top: 8px; outline: 0; }"
    "QListWidget#nav::item { padding: 10px 18px; color: #aab1c5; border-left: 3px solid transparent; }"
    "QListWidget#nav::item:selected { background: #1b1e27; color: #ffffff; border-left: 3px solid #8ba0f4; }"
    "QListWidget#nav::item:hover:!selected { background: #161922; }"
    "QListWidget#connList { background: #1b1e27; border: 1px solid #2e3340; border-radius: 8px; padding: 4px; outline: 0; }"
    "QListWidget#connList::item { padding: 8px 10px; border-radius: 6px; }"
    "QListWidget#connList::item:selected { background: #2b3350; }"
    "QLineEdit, QComboBox { background: #20242e; border: 1px solid #393f4f; border-radius: 7px; padding: 7px 9px;"
    " selection-background-color: #5d72c6; min-height: 18px; }"
    "QLineEdit:hover, QComboBox:hover { border-color: #596278; }"
    "QLineEdit:focus, QComboBox:focus { border: 1px solid #8ba0f4; }"
    "QLineEdit[invalid=\"true\"], QComboBox[invalid=\"true\"] { border: 1px solid #e5604f; }"
    "QLineEdit:read-only { color: #eef0f7; }"
    "QComboBox::drop-down { border: none; width: 22px; }"
    "QComboBox QAbstractItemView { background: #20242e; border: 1px solid #393f4f; selection-background-color: #2b3350; outline: 0; }"
    "QPushButton { background: #262b38; border: 1px solid #3a4152; border-radius: 7px; padding: 7px 14px; }"
    "QPushButton:hover { background: #2f3546; }"
    "QPushButton:pressed { background: #202431; }"
    "QPushButton:disabled { color: #6c7388; background: #1d2029; }"
    "QPushButton:focus { border: 1px solid #8ba0f4; }"
    "QCheckBox:focus { color: #ffffff; }"
    "QCheckBox::indicator:focus { border: 1px solid #8ba0f4; }"
    "QListWidget:focus { border: 1px solid #8ba0f4; }"
    "QPushButton[role=\"primary\"] { background: #5d72c6; border-color: #6f84d8; color: #ffffff; font-weight: 600; }"
    "QPushButton[role=\"primary\"]:hover { background: #6a80d6; }"
    "QPushButton[role=\"primary\"]:disabled { background: #2c3454; color: #8d94a7; border-color: #2c3454; }"
    "QPushButton[role=\"danger\"] { color: #ff8a7a; }"
    "QCheckBox { spacing: 8px; }"
    "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #4a5166; border-radius: 4px; background: #20242e; }"
    "QCheckBox::indicator:checked { background: #5d72c6; border-color: #8ba0f4; }"
    "QTableWidget { background: #1b1e27; border: 1px solid #2e3340; border-radius: 8px; gridline-color: #262a35;"
    " alternate-background-color: #1e2230; outline: 0; }"
    "QTableWidget::item { padding: 5px 8px; }"
    "QTableWidget::item:selected { background: #2b3350; color: #ffffff; }"
    "QHeaderView::section { background: #101218; color: #8d94a7; border: none; border-bottom: 1px solid #262a35; padding: 7px 8px; }"
    "QScrollArea { border: none; background: #15171d; }"
    "QScrollArea > QWidget > QWidget { background: #15171d; }"
    "QScrollBar:vertical { background: transparent; width: 10px; }"
    "QScrollBar::handle:vertical { background: #484f63; border-radius: 5px; min-height: 24px; }"
    "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
    "QFrame#footer { background: #101218; border-top: 1px solid #262a35; }"
    "QMessageBox { background: #15171d; }";

}  // namespace

// Click, then press a key combination: shows e.g. "Alt+Space".
class ShortcutEdit : public QLineEdit {
public:
    std::function<void()> onChanged;
    explicit ShortcutEdit(QWidget *parent = nullptr) : QLineEdit(parent) {
        setReadOnly(true);
        setPlaceholderText("Click here, then press the keys");
    }

protected:
    bool event(QEvent *e) override {
        if (e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }  // take Alt+Space etc. ourselves
        return QLineEdit::event(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        const int key = e->key();
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return;
        if (key == Qt::Key_Escape && e->modifiers() == Qt::NoModifier) { clearFocus(); return; }
        unsigned mods = 0;
        if (e->modifiers() & Qt::ControlModifier) mods |= kModControl;
        if (e->modifiers() & Qt::AltModifier) mods |= kModAlt;
        if (e->modifiers() & Qt::ShiftModifier) mods |= kModShift;
        if (e->modifiers() & Qt::MetaModifier) mods |= kModWin;
        const QString text = shortcutToString(mods, e->nativeVirtualKey());
        if (text.isEmpty()) return;  // a key we have no name for
        setText(text);
        if (onChanged) onChanged();
    }
};

SettingsWindow::SettingsWindow(const AppConfig &initial, std::function<QString()> appliedCallback, std::function<void()> retry)
    : draft(initial), applied(std::move(appliedCallback)), retryNow(std::move(retry)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Offnote Settings");
    resize(820, 600);
    setMinimumSize(700, 500);
    setStyleSheet(kStyle);
    autostartWanted = autostartEnabled();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *body = new QHBoxLayout();
    body->setSpacing(0);
    nav = new QListWidget();
    nav->setObjectName("nav");
    nav->setFixedWidth(170);
    nav->setAccessibleName("Settings sections");
    nav->setFocusPolicy(Qt::TabFocus);
    for (const char *name : {"Connections", "Routing", "Shortcuts", "General", "Outbox"}) nav->addItem(name);
    body->addWidget(nav);

    stack = new QStackedWidget();
    stack->setObjectName("page");
    stack->addWidget(buildConnectionsPage());
    stack->addWidget(buildRoutingPage());
    stack->addWidget(buildShortcutsPage());
    stack->addWidget(buildGeneralPage());
    stack->addWidget(buildOutboxPage());
    body->addWidget(stack, 1);
    outer->addLayout(body, 1);

    auto *footer = new QFrame();
    footer->setObjectName("footer");
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 12, 20, 12);
    auto *messages = new QVBoxLayout();
    errorLabel = new QLabel();
    errorLabel->setWordWrap(true);
    errorLabel->setProperty("tone", "error");
    errorLabel->hide();
    statusLabel = new QLabel();
    statusLabel->setProperty("tone", "muted");
    messages->addWidget(errorLabel);
    messages->addWidget(statusLabel);
    footerLayout->addLayout(messages, 1);
    saveButton = makeButton("Save", "primary");
    saveButton->setAccessibleName("Save settings");
    auto *closeButton = makeButton("Close");
    closeButton->setAccessibleName("Close settings");
    footerLayout->addWidget(saveButton);
    footerLayout->addWidget(closeButton);
    outer->addWidget(footer);

    connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    connect(nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row == Outbox) refreshOutbox();
    });
    connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    nav->setCurrentRow(Connections);
    if (!draft.connections.isEmpty()) connList->setCurrentRow(0);
    rebuildRouting();
    refreshConnectionState();
    saveButton->setEnabled(false);
}

// Ctrl+S saves and Esc closes, like most settings dialogs.
void SettingsWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_S && (event->modifiers() & Qt::ControlModifier)) { save(); return; }
    if (event->key() == Qt::Key_Escape) { close(); return; }
    QWidget::keyPressEvent(event);
}

void SettingsWindow::showPage(Page page) {
    nav->setCurrentRow(page);
    show();
    raise();
    activateWindow();
}

void SettingsWindow::markDirty() {
    dirty = true;
    saveButton->setEnabled(true);
    setWindowTitle("Offnote Settings (unsaved changes)");
    setTone(statusLabel, "muted", "");
}

void SettingsWindow::closeEvent(QCloseEvent *event) {
    if (dirty) {
        const auto answer = QMessageBox::question(this, "Offnote Settings", "Discard your unsaved changes?",
                                                  QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Discard) { event->ignore(); return; }
    }
    event->accept();
}

void SettingsWindow::save() {
    const QStringList errors = validateConfig(draft);
    if (!errors.isEmpty()) {
        setTone(errorLabel, "error", errors.join("\n"));
        errorLabel->show();
        setTone(statusLabel, "muted", "Not saved.");
        return;
    }
    errorLabel->hide();
    QString error;
    if (!saveConfig(draft, &error)) {
        setTone(errorLabel, "error", error);
        errorLabel->show();
        return;
    }
    if (autostartWanted != autostartEnabled()) setAutostart(autostartWanted);
    dirty = false;
    saveButton->setEnabled(false);
    setWindowTitle("Offnote Settings");
    const QString warning = applied ? applied() : QString();
    if (warning.isEmpty()) setTone(statusLabel, "ok", "Saved and applied.");
    else setTone(statusLabel, "error", "Saved. " + warning);
}

// ---------------------------------------------------------------- Connections

QWidget *SettingsWindow::buildConnectionsPage() {
    auto *page = new QWidget();
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("Connections", "Where your notes are delivered. Each note is saved locally first, then sent to its connection."));

    auto *columns = new QHBoxLayout();
    columns->setSpacing(18);

    auto *left = new QVBoxLayout();
    connList = new QListWidget();
    connList->setObjectName("connList");
    connList->setFixedWidth(200);
    connList->setAccessibleName("Connection list");
    auto *addButton = makeButton("Add connection");
    addButton->setAccessibleName("Add connection");
    left->addWidget(connList, 1);
    left->addWidget(addButton);
    columns->addLayout(left);

    auto *right = new QVBoxLayout();
    connEmpty = new QLabel("No connections yet. Add one to choose where notes are sent.");
    connEmpty->setProperty("tone", "muted");
    connEmpty->setAlignment(Qt::AlignCenter);
    connEmpty->setWordWrap(true);

    connForm = new QWidget();
    auto *formRoot = new QVBoxLayout(connForm);
    formRoot->setContentsMargins(0, 0, 0, 0);
    form = new QFormLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    nameEdit = new QLineEdit();
    nameEdit->setAccessibleName("Connection name");
    nameEdit->setPlaceholderText("e.g. personal");
    typeCombo = new QComboBox();
    typeCombo->setAccessibleName("Connection type");
    typeCombo->addItem("Markdown folder", "markdown");
    typeCombo->addItem("Obsidian vault", "obsidian");
    typeCombo->addItem("Logseq graph", "logseq");
    typeCombo->addItem("AFFiNE (via sidecar)", "affine");

    pathRow = new QWidget();
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathEdit = new QLineEdit();
    pathEdit->setAccessibleName("Folder path");
    pathEdit->setPlaceholderText("e.g. notes, or a full path");
    auto *browse = makeButton("Browse...");
    browse->setAccessibleName("Browse for folder");
    pathLayout->addWidget(pathEdit, 1);
    pathLayout->addWidget(browse);

    urlEdit = new QLineEdit();
    urlEdit->setAccessibleName("Sidecar URL");
    urlEdit->setPlaceholderText("http://your-sidecar-host:8787");
    tokenRow = new QWidget();
    auto *tokenLayout = new QHBoxLayout(tokenRow);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenEdit = new QLineEdit();
    tokenEdit->setAccessibleName("Sidecar token");
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setPlaceholderText("token printed by the sidecar's install.sh");
    auto *showToken = new QCheckBox("Show");
    showToken->setAccessibleName("Show sidecar token");
    tokenLayout->addWidget(tokenEdit, 1);
    tokenLayout->addWidget(showToken);
    connectorEdit = new QLineEdit();
    connectorEdit->setAccessibleName("Sidecar connector name");
    connectorEdit->setPlaceholderText("a name from the sidecar's connectors.json");

    form->addRow("Name", nameEdit);
    form->addRow("Type", typeCombo);
    form->addRow("Folder", pathRow);
    form->addRow("Sidecar URL", urlEdit);
    form->addRow("Token", tokenRow);
    form->addRow("Connector", connectorEdit);
    formRoot->addLayout(form);

    connError = new QLabel();
    connError->setWordWrap(true);
    connError->setProperty("tone", "error");
    formRoot->addWidget(connError);

    auto *actions = new QHBoxLayout();
    testButton = makeButton("Send test note");
    testButton->setAccessibleName("Send test note");
    auto *removeButton = makeButton("Remove", "danger");
    removeButton->setAccessibleName("Remove connection");
    testResult = new QLabel();
    testResult->setWordWrap(true);
    actions->addWidget(testButton);
    actions->addWidget(removeButton);
    actions->addStretch(1);
    formRoot->addLayout(actions);
    formRoot->addWidget(testResult);
    formRoot->addStretch(1);

    right->addWidget(connEmpty, 1);
    right->addWidget(connForm, 1);
    columns->addLayout(right, 1);
    root->addLayout(columns, 1);

    connect(connList, &QListWidget::currentRowChanged, this, [this](int row) { selectConnection(row); });
    connect(addButton, &QPushButton::clicked, this, [this] {
        QStringList used;
        for (const Connection &c : draft.connections) used << c.name;
        QString name = "connection";
        for (int n = 1; used.contains(name); ++n) name = QStringLiteral("connection%1").arg(n + 1);
        Connection c;
        c.name = name;
        draft.connections.append(c);
        connList->addItem(name);
        connList->setCurrentRow(connList->count() - 1);
        markDirty();
        rebuildRouting();
        refreshConnectionState();
        nameEdit->setFocus();
        nameEdit->selectAll();
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = connList->currentRow();
        if (row < 0) return;
        const QString name = draft.connections[row].name;
        if (QMessageBox::question(this, "Remove connection", QStringLiteral("Remove \"%1\"?").arg(name)) != QMessageBox::Yes) return;
        draft.connections.removeAt(row);
        loading = true;
        delete connList->takeItem(row);
        loading = false;
        markDirty();
        rebuildRouting();
        selectConnection(connList->currentRow());
    });

    connect(nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        const int row = connList->currentRow();
        if (loading || row < 0) return;
        const QString old = draft.connections[row].name;
        draft.connections[row].name = text;
        // Keep routing pointing at the renamed connection.
        for (TagRoute &r : draft.tagRoutes) if (r.connection == old) r.connection = text;
        if (draft.defaultConnection == old) draft.defaultConnection = text;
        markDirty();
        rebuildRouting();
        refreshConnectionState();
    });
    connect(typeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const int row = connList->currentRow();
        if (loading || row < 0) return;
        draft.connections[row].type = typeCombo->currentData().toString();
        markDirty();
        loadConnectionForm();
        refreshConnectionState();
    });
    auto bind = [this](QLineEdit *edit, QString Connection::*field) {
        connect(edit, &QLineEdit::textEdited, this, [this, field](const QString &text) {
            const int row = connList->currentRow();
            if (loading || row < 0) return;
            draft.connections[row].*field = text;
            markDirty();
            refreshConnectionState();
        });
    };
    bind(pathEdit, &Connection::path);
    bind(urlEdit, &Connection::sidecarUrl);
    bind(tokenEdit, &Connection::sidecarToken);
    bind(connectorEdit, &Connection::sidecarConnector);
    connect(showToken, &QCheckBox::toggled, this, [this](bool on) {
        tokenEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = pathEdit->text().isEmpty() ? dataDir() : resolveDir(pathEdit->text());
        const QString chosen = QFileDialog::getExistingDirectory(this, "Choose a folder", start);
        const int row = connList->currentRow();
        if (chosen.isEmpty() || row < 0) return;
        draft.connections[row].path = chosen;
        pathEdit->setText(chosen);
        markDirty();
        refreshConnectionState();
    });
    connect(testButton, &QPushButton::clicked, this, [this] { runConnectionTest(); });

    for (const Connection &c : draft.connections) connList->addItem(c.name);
    return page;
}

void SettingsWindow::selectConnection(int row) {
    if (loading) return;
    setTone(testResult, "muted", "");
    loadConnectionForm();
    refreshConnectionState();
    Q_UNUSED(row);
}

void SettingsWindow::loadConnectionForm() {
    const int row = connList->currentRow();
    const bool has = row >= 0 && row < draft.connections.size();
    connForm->setVisible(has);
    connEmpty->setVisible(!has);
    if (!has) return;
    const Connection &c = draft.connections[row];
    loading = true;
    nameEdit->setText(c.name);
    typeCombo->setCurrentIndex(qMax(0, typeCombo->findData(c.type)));
    pathEdit->setText(c.path);
    urlEdit->setText(c.sidecarUrl);
    tokenEdit->setText(c.sidecarToken);
    connectorEdit->setText(c.sidecarConnector);
    loading = false;
    const bool affine = c.type == "affine";
    form->setRowVisible(pathRow, !affine);
    form->setRowVisible(urlEdit, affine);
    form->setRowVisible(tokenRow, affine);
    form->setRowVisible(connectorEdit, affine);
    if (QLabel *label = qobject_cast<QLabel *>(form->labelForField(pathRow)))
        label->setText(c.type == "obsidian" ? "Vault" : c.type == "logseq" ? "Graph" : "Folder");
}

// Live validation: red list entries, red field borders, and one message for the open connection.
void SettingsWindow::refreshConnectionState() {
    QStringList seen;
    QList<bool> duplicate;
    for (const Connection &c : draft.connections) {
        duplicate.append(!c.name.isEmpty() && seen.contains(c.name));
        seen << c.name;
    }
    for (int i = 0; i < draft.connections.size() && i < connList->count(); ++i) {
        const Connection &c = draft.connections[i];
        QListWidgetItem *item = connList->item(i);
        const bool bad = !validateConnection(c).isEmpty() || duplicate[i];
        item->setText(c.name.isEmpty() ? "(unnamed)" : c.name);
        item->setForeground(bad ? QColor("#ff8a7a") : QColor("#eef0f7"));
    }
    const int row = connList->currentRow();
    if (row < 0 || row >= draft.connections.size()) return;
    const Connection &c = draft.connections[row];
    const bool affine = c.type == "affine";
    setInvalid(nameEdit, c.name.trimmed().isEmpty() || c.name.contains(',') || duplicate[row]);
    setInvalid(pathEdit, !affine && c.path.trimmed().isEmpty());
    setInvalid(urlEdit, affine && c.sidecarUrl.trimmed().isEmpty());
    setInvalid(tokenEdit, affine && c.sidecarToken.trimmed().isEmpty());
    setInvalid(connectorEdit, affine && c.sidecarConnector.trimmed().isEmpty());
    QString why = validateConnection(c);
    if (why.isEmpty() && duplicate[row]) why = "uses a name another connection already has";
    setTone(connError, "error", why.isEmpty() ? QString() : "This connection " + why + ".");
    testButton->setEnabled(!testing && why.isEmpty());
}

void SettingsWindow::runConnectionTest() {
    const int row = connList->currentRow();
    if (row < 0 || testing) return;
    const Connection conn = draft.connections[row];
    testing = true;
    testButton->setEnabled(false);
    setTone(testResult, "muted", "Sending...");
    QPointer<SettingsWindow> self(this);
    QThreadPool::globalInstance()->start([self, conn] {
        QString error;
        const bool ok = writeToConnection(conn, "current time testing - " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate), &error);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, conn, ok, error] {
            if (!self) return;
            self->testing = false;
            const int now = self->connList->currentRow();
            if (now >= 0 && now < self->draft.connections.size() && self->draft.connections[now].name == conn.name) {
                if (ok) setTone(self->testResult, "ok", "Sent a test note. Check the destination.");
                else setTone(self->testResult, "error", error);
            }
            self->refreshConnectionState();
        }, Qt::QueuedConnection);
    });
}

// -------------------------------------------------------------------- Routing

QWidget *SettingsWindow::buildRoutingPage() {
    auto *page = new QWidget();
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("Routing", "A #tag in a note picks where it goes. Notes without a matching tag use the default connection."));

    auto *defaultRow = new QHBoxLayout();
    defaultRow->addWidget(new QLabel("Default connection"));
    defaultCombo = new QComboBox();
    defaultCombo->setAccessibleName("Default connection");
    defaultCombo->setMinimumWidth(240);
    defaultRow->addWidget(defaultCombo);
    defaultRow->addStretch(1);
    root->addLayout(defaultRow);

    root->addWidget(new QLabel("<b>Tag rules</b>"));
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto *rulesHost = new QWidget();
    rulesLayout = new QVBoxLayout(rulesHost);
    rulesLayout->setContentsMargins(0, 0, 4, 0);
    rulesLayout->setSpacing(8);
    scroll->setWidget(rulesHost);
    root->addWidget(scroll, 1);
    auto *addRule = makeButton("Add tag rule");
    addRule->setAccessibleName("Add tag rule");
    auto *addRow = new QHBoxLayout();
    addRow->addWidget(addRule);
    addRow->addStretch(1);
    root->addLayout(addRow);

    auto *tryBox = new QFrame();
    tryBox->setStyleSheet("QFrame { background: #1b1e27; border: 1px solid #2e3340; border-radius: 8px; }"
                          "QLabel { border: none; }");
    auto *tryLayout = new QVBoxLayout(tryBox);
    tryLayout->addWidget(new QLabel("<b>Try it</b> - type a sample note to see where it would go"));
    previewEdit = new QLineEdit();
    previewEdit->setAccessibleName("Sample note");
    previewEdit->setPlaceholderText("e.g. #work call the dentist");
    previewLabel = new QLabel();
    previewLabel->setWordWrap(true);
    tryLayout->addWidget(previewEdit);
    tryLayout->addWidget(previewLabel);
    root->addWidget(tryBox);

    connect(defaultCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading || index < 0) return;
        draft.defaultConnection = defaultCombo->itemData(index).toString();
        markDirty();
        refreshRoutePreview();
    });
    connect(addRule, &QPushButton::clicked, this, [this] {
        TagRoute r;
        r.connection = draft.connections.isEmpty() ? QString() : draft.connections.first().name;
        draft.tagRoutes.append(r);
        markDirty();
        rebuildRouting();
    });
    connect(previewEdit, &QLineEdit::textChanged, this, [this] { refreshRoutePreview(); });
    return page;
}

void SettingsWindow::rebuildRouting() {
    loading = true;
    QStringList names;
    for (const Connection &c : draft.connections)
        if (!c.name.trimmed().isEmpty() && !names.contains(c.name)) names << c.name;

    auto fill = [&names](QComboBox *combo, const QString &selected) {
        combo->clear();
        for (const QString &n : names) combo->addItem(n, n);
        if (!selected.isEmpty() && !names.contains(selected)) combo->addItem(selected + " (missing)", selected);
        combo->setCurrentIndex(combo->findData(selected));
        setInvalid(combo, combo->currentIndex() < 0 || !names.contains(selected));
    };
    fill(defaultCombo, draft.defaultConnection);
    if (defaultCombo->currentIndex() < 0 && !names.isEmpty()) {
        defaultCombo->setCurrentIndex(0);
        draft.defaultConnection = names.first();
    }

    while (QLayoutItem *item = rulesLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (draft.tagRoutes.isEmpty()) {
        auto *none = new QLabel("No tag rules. Every note goes to the default connection.");
        none->setProperty("tone", "muted");
        rulesLayout->addWidget(none);
    }
    for (int i = 0; i < draft.tagRoutes.size(); ++i) {
        auto *row = new QWidget();
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *hash = new QLabel("#");
        auto *tag = new QLineEdit(draft.tagRoutes[i].tag);
        tag->setAccessibleName("Tag");
        tag->setPlaceholderText("tag, without the #");
        tag->setMaximumWidth(220);
        auto *arrow = new QLabel("goes to");
        auto *target = new QComboBox();
        target->setAccessibleName("Connection for this tag");
        target->setMinimumWidth(200);
        fill(target, draft.tagRoutes[i].connection);
        auto *remove = makeButton("Remove", "danger");
        remove->setAccessibleName("Remove tag rule");
        layout->addWidget(hash);
        layout->addWidget(tag);
        layout->addWidget(arrow);
        layout->addWidget(target);
        layout->addWidget(remove);
        layout->addStretch(1);
        rulesLayout->addWidget(row);

        connect(tag, &QLineEdit::textEdited, this, [this, i](const QString &text) {
            draft.tagRoutes[i].tag = text.startsWith('#') ? text.mid(1) : text;
            markDirty();
            refreshRoutePreview();
        });
        connect(target, &QComboBox::currentIndexChanged, this, [this, i, target](int index) {
            if (loading || index < 0) return;
            draft.tagRoutes[i].connection = target->itemData(index).toString();
            markDirty();
            refreshRoutePreview();
        });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            draft.tagRoutes.removeAt(i);
            markDirty();
            rebuildRouting();
        });
    }
    rulesLayout->addStretch(1);
    loading = false;
    refreshRoutePreview();
}

void SettingsWindow::refreshRoutePreview() {
    const QString text = previewEdit->text();
    if (text.trimmed().isEmpty()) { setTone(previewLabel, "muted", "Nothing typed yet."); return; }
    const QStringList tags = extractTags(text);
    const QStringList targets = resolveTargets(draft, tags);
    QStringList shown;
    for (const QString &t : tags) shown << "#" + t;
    setTone(previewLabel, "ok",
            (tags.isEmpty() ? QString("No tags found. ") : "Tags found: " + shown.join(", ") + ". ") + "Goes to: " +
                (targets.isEmpty() ? QString("nowhere (set a default connection)") : targets.join(", ")));
}

// ------------------------------------------------------------------ Shortcuts

QWidget *SettingsWindow::buildShortcutsPage() {
    auto *page = new QWidget();
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("Shortcuts", "Click a box and press the combination you want."));

    auto *grid = new QFormLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(14);
    toggleEdit = new ShortcutEdit();
    toggleEdit->setAccessibleName("Open note shortcut");
    toggleEdit->setText(draft.toggleNote);
    saveEdit = new ShortcutEdit();
    saveEdit->setAccessibleName("Save note shortcut");
    saveEdit->setText(draft.saveNote);
    auto *resetToggle = makeButton("Reset");
    auto *resetSave = makeButton("Reset");
    resetToggle->setAccessibleName("Reset open note shortcut");
    resetSave->setAccessibleName("Reset save note shortcut");

    auto rowFor = [](QWidget *edit, QPushButton *reset) {
        auto *w = new QWidget();
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        edit->setMaximumWidth(260);
        l->addWidget(edit);
        l->addWidget(reset);
        l->addStretch(1);
        return w;
    };
    grid->addRow("Open or hide the note (works anywhere)", rowFor(toggleEdit, resetToggle));
    grid->addRow("Save the note (inside the note window)", rowFor(saveEdit, resetSave));
    root->addLayout(grid);

    shortcutError = new QLabel();
    shortcutError->setWordWrap(true);
    shortcutError->setProperty("tone", "error");
    root->addWidget(shortcutError);
    auto *note = new QLabel("Ctrl+Enter and Alt+Enter always save, whatever you choose here. Esc hides the note window.");
    note->setProperty("tone", "muted");
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch(1);

    auto check = [this] {
        const Shortcut s = parseShortcut(draft.toggleNote);
        setInvalid(toggleEdit, !s.valid() || s.mods == 0);
        setTone(shortcutError, "error", (!s.valid() || s.mods == 0) ? "The open shortcut needs a modifier such as Alt or Ctrl plus a key." : QString());
    };
    toggleEdit->onChanged = [this, check] { draft.toggleNote = toggleEdit->text(); markDirty(); check(); };
    saveEdit->onChanged = [this] { draft.saveNote = saveEdit->text(); markDirty(); };
    connect(resetToggle, &QPushButton::clicked, this, [this, check] { toggleEdit->setText("Alt+Space"); draft.toggleNote = "Alt+Space"; markDirty(); check(); });
    connect(resetSave, &QPushButton::clicked, this, [this] { saveEdit->setText("Alt+Enter"); draft.saveNote = "Alt+Enter"; markDirty(); });
    return page;
}

// -------------------------------------------------------------------- General

QWidget *SettingsWindow::buildGeneralPage() {
    auto *page = new QWidget();
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("General", "Startup and where your data lives."));

    auto *startup = new QCheckBox("Start Offnote when I sign in to Windows");
    startup->setAccessibleName("Start with Windows");
    startup->setChecked(autostartWanted);
    root->addWidget(startup);
    connect(startup, &QCheckBox::toggled, this, [this](bool on) { autostartWanted = on; markDirty(); });

    auto *where = new QLabel(QStringLiteral("<b>Data folder</b><br><span style='color:#8d94a7'>%1</span><br><br>"
                                            "<b>Config file</b><br><span style='color:#8d94a7'>%2</span><br><br>"
                                            "<b>Note outbox</b><br><span style='color:#8d94a7'>%3</span>")
                                 .arg(QDir::toNativeSeparators(dataDir()).toHtmlEscaped(),
                                      QDir::toNativeSeparators(configPath()).toHtmlEscaped(),
                                      QDir::toNativeSeparators(dbPath()).toHtmlEscaped()));
    where->setTextFormat(Qt::RichText);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(where);

    auto *open = makeButton("Open data folder");
    open->setAccessibleName("Open data folder");
    auto *openRow = new QHBoxLayout();
    openRow->addWidget(open);
    openRow->addStretch(1);
    root->addLayout(openRow);
    connect(open, &QPushButton::clicked, this, [] {
        QDir().mkpath(dataDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(dataDir()));
    });

    auto *note = new QLabel("The Tauri and WinForms clients share this folder. Run only one client at a time so a "
                            "queued note is never delivered twice.");
    note->setProperty("tone", "muted");
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch(1);
    return page;
}

// --------------------------------------------------------------------- Outbox

QWidget *SettingsWindow::buildOutboxPage() {
    auto *page = new QWidget();
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("Outbox", "Every saved note and whether it has been delivered. Pending notes are retried every 30 seconds."));

    outboxSummary = new QLabel();
    outboxSummary->setProperty("tone", "muted");
    outboxTable = new QTableWidget(0, 4);
    outboxTable->setAccessibleName("Recent notes");
    outboxTable->setHorizontalHeaderLabels({"Saved", "Status", "Destination", "Note"});
    outboxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outboxTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    outboxTable->setAlternatingRowColors(true);
    outboxTable->verticalHeader()->hide();
    outboxTable->setShowGrid(false);
    outboxTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    outboxTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    outboxTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    outboxTable->horizontalHeader()->setStretchLastSection(true);

    auto *buttons = new QHBoxLayout();
    auto *refresh = makeButton("Refresh");
    refresh->setAccessibleName("Refresh outbox");
    auto *retry = makeButton("Retry pending now", "primary");
    retry->setAccessibleName("Retry pending notes now");
    buttons->addWidget(outboxSummary, 1);
    buttons->addWidget(refresh);
    buttons->addWidget(retry);

    root->addWidget(outboxTable, 1);
    root->addLayout(buttons);
    connect(refresh, &QPushButton::clicked, this, [this] { refreshOutbox(); });
    connect(retry, &QPushButton::clicked, this, [this] {
        if (retryNow) retryNow();
        setTone(outboxSummary, "muted", "Retrying...");
    });
    return page;
}

void SettingsWindow::refreshOutbox() {
    Store store;
    QString error;
    if (!store.open(dbPath(), &error)) { setTone(outboxSummary, "error", error); return; }
    const QList<NoteRow> rows = store.recent(200);
    const int pending = store.pendingCount();
    outboxTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const NoteRow &r = rows[i];
        const QDateTime when = QDateTime::fromString(r.createdAt, Qt::ISODate).toLocalTime();
        const QString status = r.processed ? "Delivered" : r.error.isEmpty() ? "Queued" : "Retrying";
        auto *time = new QTableWidgetItem(when.isValid() ? when.toString("yyyy-MM-dd HH:mm") : r.createdAt);
        auto *state = new QTableWidgetItem(status);
        if (!r.processed && !r.error.isEmpty()) state->setToolTip(r.error.left(300).toHtmlEscaped());
        state->setForeground(r.processed ? QColor("#5ae08a") : r.error.isEmpty() ? QColor("#8ba0f4") : QColor("#ff8a7a"));
        auto *dest = new QTableWidgetItem(r.targets.isEmpty() ? "-" : r.targets.split(',').join(", "));
        const QString firstLine = r.data.section('\n', 0, 0);
        auto *note = new QTableWidgetItem(firstLine);
        note->setToolTip("<p style='white-space:pre'>" + r.data.left(600).toHtmlEscaped() + "</p>");
        outboxTable->setItem(i, 0, time);
        outboxTable->setItem(i, 1, state);
        outboxTable->setItem(i, 2, dest);
        outboxTable->setItem(i, 3, note);
    }
    setTone(outboxSummary, pending ? "error" : "muted",
            pending ? QStringLiteral("%1 note(s) waiting for delivery").arg(pending)
                    : rows.isEmpty() ? QStringLiteral("No notes yet. Saved notes appear here.")
                                     : QStringLiteral("Everything delivered. Showing the latest %1.").arg(rows.size()));
}
