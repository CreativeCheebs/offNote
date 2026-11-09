#include "delivery.h"

#include "router.h"
#include "store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QThreadPool>
#include <atomic>
#include <windows.h>
#include <winhttp.h>

namespace {

bool appendUtf8(const QString &filePath, const QByteArray &bytes, QString *error) {
    QFile file(filePath);
    // Append in binary mode so "\n" stays "\n", like the other clients' daily files.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error) *error = QStringLiteral("failed to open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("failed to write note: ") + file.errorString();
        return false;
    }
    return true;
}

QString winHttpMessage(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "host name not found";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "connection refused";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection lost";
    case ERROR_WINHTTP_SECURE_FAILURE: return "TLS/certificate problem";
    case ERROR_WINHTTP_INVALID_URL: return "invalid URL";
    default: return QStringLiteral("error %1").arg(code);
    }
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET h = nullptr) : h(h) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

// A plain authenticated JSON POST, off the Qt event loop and without QtNetwork.
bool postJson(const QString &url, const QString &token, const QByteArray &body, int *status, QString *responseBody,
              QString *error) {
    for (const QChar ch : token) {
        if (ch.unicode() < 0x21 || ch.unicode() > 0x7E) {
            *error = "failed to reach affine sidecar: the token contains characters that are not allowed in a header";
            return false;
        }
    }
    const std::wstring wurl = url.toStdWString();
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || !parts.lpszHostName) {
        *error = "failed to reach affine sidecar: invalid URL";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring target = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                std::wstring(parts.lpszExtraInfo ? parts.lpszExtraInfo : L"", parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    Handle session(WinHttpOpen(L"Offnote-Qt/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) { *error = "failed to reach affine sidecar: " + winHttpMessage(GetLastError()); return false; }
    WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 30000);
    // Never follow a redirect from https to http, so a note and token cannot be downgraded.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    Handle connect(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (!connect.h) { *error = "failed to reach affine sidecar: " + winHttpMessage(GetLastError()); return false; }
    Handle request(WinHttpOpenRequest(connect.h, L"POST", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.h) { *error = "failed to reach affine sidecar: " + winHttpMessage(GetLastError()); return false; }

    const std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + token.toStdWString() + L"\r\n";
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1), const_cast<char *>(body.constData()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        *error = "failed to reach affine sidecar: " + winHttpMessage(GetLastError());
        return false;
    }
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                        &size, WINHTTP_NO_HEADER_INDEX);
    *status = static_cast<int>(code);
    QByteArray data;
    DWORD available = 0;
    while (data.size() < 65536 && WinHttpQueryDataAvailable(request.h, &available) && available > 0) {
        QByteArray chunk(static_cast<qsizetype>(available), Qt::Uninitialized);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
        data.append(chunk.constData(), read);
    }
    *responseBody = QString::fromUtf8(data);
    return true;
}

bool writeAffine(const Connection &conn, const QString &text, QString *error) {
    if (conn.sidecarUrl.isEmpty()) { *error = QStringLiteral("connection '%1' missing 'sidecar_url'").arg(conn.name); return false; }
    if (conn.sidecarToken.isEmpty()) { *error = QStringLiteral("connection '%1' missing 'sidecar_token'").arg(conn.name); return false; }
    if (conn.sidecarConnector.isEmpty()) { *error = QStringLiteral("connection '%1' missing 'sidecar_connector'").arg(conn.name); return false; }

    QString base = conn.sidecarUrl;
    while (base.endsWith('/')) base.chop(1);
    const QJsonObject payload{{"connector", conn.sidecarConnector}, {"texts", QJsonArray{text}}};
    int status = 0;
    QString response;
    if (!postJson(base + "/append", conn.sidecarToken, QJsonDocument(payload).toJson(QJsonDocument::Compact), &status, &response, error))
        return false;
    if (status < 200 || status >= 300) {
        // Status only: a response body could echo the note or the Authorization header.
        Q_UNUSED(response);
        *error = QStringLiteral("sidecar returned HTTP %1").arg(status);
        return false;
    }
    return true;
}

}  // namespace

bool writeMarkdown(const QString &dir, const QString &text, const QDateTime &now, QString *error) {
    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("failed to create dir ") + dir;
        return false;
    }
    const QLocale c = QLocale::c();
    const QString file = dir + "/" + now.toString("yyyy-MM-dd") + ".md";
    QByteArray out;
    if (!QFile::exists(file)) out += ("# " + c.toString(now, "dddd, MMMM d yyyy") + "\n\n").toUtf8();
    out += ("## " + now.toString("HH:mm:ss") + "\n\n" + text + "\n\n").toUtf8();
    return appendUtf8(file, out, error);
}

bool writeLogseq(const QString &dir, const QString &text, const QDateTime &now, QString *error) {
    const QString journals = dir + "/journals";
    if (!QDir().mkpath(journals)) {
        if (error) *error = QStringLiteral("failed to create dir ") + journals;
        return false;
    }
    QString body = text;
    body.replace('\n', "\n  ");  // keep continuation lines inside the same Logseq block
    const QString line = "- " + now.toString("HH:mm") + " " + body + "\n";
    return appendUtf8(journals + "/" + now.toString("yyyy_MM_dd") + ".md", line.toUtf8(), error);
}

bool writeToConnection(const Connection &conn, const QString &text, QString *error) {
    QString ignored;
    if (!error) error = &ignored;
    if (conn.type == "markdown" || conn.type == "obsidian" || conn.type == "logseq") {
        if (conn.path.isEmpty()) { *error = QStringLiteral("connection '%1' missing 'path'").arg(conn.name); return false; }
        const QString dir = resolveDir(conn.path);
        return conn.type == "logseq" ? writeLogseq(dir, text, QDateTime::currentDateTime(), error)
                                     : writeMarkdown(dir, text, QDateTime::currentDateTime(), error);
    }
    if (conn.type == "affine") return writeAffine(conn, text, error);
    *error = QStringLiteral("connection '%1' has unknown type '%2'").arg(conn.name, conn.type);
    return false;
}

RetryResult retryPending() {
    static std::atomic<bool> running{false};
    RetryResult result;
    if (running.exchange(true)) { result.skipped = true; return result; }
    struct Release { ~Release() { running = false; } } release;

    AppConfig config;
    QString error;
    if (!loadConfig(config, &error)) { result.error = error; return result; }
    Store store;
    if (!store.open(dbPath(), &error)) { result.error = error; return result; }

    for (const NoteRow &row : store.pending()) {
        QStringList targets;
        for (const QString &t : row.targets.split(',')) {
            if (!t.trimmed().isEmpty()) targets << t.trimmed();
        }
        if (targets.isEmpty()) {
            // Queued without routing (e.g. config was unreadable at save time): route it now.
            targets = resolveTargets(config, extractTags(row.data));
            if (targets.isEmpty()) {
                store.markError(row.id, "no destination: the config has no default connection");
                ++result.failed;
                continue;
            }
            store.updateTargets(row.id, targets.join(','));
        }

        QStringList remaining, errors;
        for (const QString &name : targets) {
            QString why;
            const Connection *conn = config.find(name);
            const bool ok = conn ? writeToConnection(*conn, row.data, &why)
                                 : (why = QStringLiteral("routing points to connection '%1' which is not defined").arg(name), false);
            if (!ok) {
                remaining << name;
                errors << name + ": " + why;
            }
        }
        if (remaining.isEmpty()) {
            store.markProcessed(row.id, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            ++result.delivered;
        } else {
            store.updateTargets(row.id, remaining.join(','));
            store.markError(row.id, errors.join("; "));
            ++result.failed;
        }
    }
    result.pending = store.pendingCount();
    return result;
}

void Delivery::kick() {
    if (running) { again = true; return; }
    running = true;
    const std::weak_ptr<bool> guard = alive;  // the worker may outlive this object on quit
    QThreadPool::globalInstance()->start([this, guard] {
        const RetryResult result = retryPending();
        QCoreApplication *app = QCoreApplication::instance();
        if (!app || guard.expired()) return;
        QMetaObject::invokeMethod(app, [this, guard, result] {
            if (guard.expired()) return;
            running = false;
            if (onFinished) onFinished(result);
            if (again) { again = false; kick(); }
        }, Qt::QueuedConnection);
    });
}

void Delivery::waitForIdle(int msecs) { QThreadPool::globalInstance()->waitForDone(msecs); }
