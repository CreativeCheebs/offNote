#pragma once
#include "config.h"

#include <QDateTime>
#include <functional>
#include <memory>

// Writes `text` to one destination. Returns false and fills `error` on failure. Never
// logs the note text or the sidecar token.
bool writeToConnection(const Connection &conn, const QString &text, QString *error);

// Exposed for tests; output matches what the Tauri client writes byte for byte.
bool writeMarkdown(const QString &dir, const QString &text, const QDateTime &now, QString *error);
bool writeLogseq(const QString &dir, const QString &text, const QDateTime &now, QString *error);

struct RetryResult {
    bool skipped = false;  // another pass was already running
    int delivered = 0;     // notes fully delivered in this pass
    int failed = 0;        // notes that still have undelivered targets
    int pending = 0;       // backlog size after the pass
    QString error;         // config/database problem, if any
};

// Attempts delivery of every pending note. Each row's `targets` shrinks to what is still
// undelivered, so a retry never re-sends to a destination that already succeeded. A row
// with no targets (queued before routing existed) is routed from the current config
// instead of being dropped. Call from a worker thread; overlapping calls are skipped.
RetryResult retryPending();

// Runs retryPending() on a worker thread. kick() and onFinished are used on the UI thread.
class Delivery {
public:
    std::function<void(const RetryResult &)> onFinished;
    void kick();
    static void waitForIdle(int msecs);

private:
    bool running = false;
    bool again = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
