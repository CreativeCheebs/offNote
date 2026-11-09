// Plain-assert tests for the routing, config and delivery core. They run against a scratch
// %APPDATA% (never the real one) and, when OFFNOTE_TEST_SIDECAR is set, against a local
// stub of the AFFiNE sidecar (see sidecar_stub.py).
#include "../config.h"
#include "../delivery.h"
#include "../router.h"
#include "../shortcut.h"
#include "../store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static QByteArray slurp(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

static void testTags() {
    CHECK(extractTags("#work note") == QStringList{"work"});
    CHECK(extractTags("a #work b") == QStringList{"work"});
    CHECK(extractTags("# heading text").isEmpty());
    CHECK(extractTags("email a#b.com").isEmpty());
    CHECK(extractTags("#work and #work.") == QStringList{"work"});
    CHECK(extractTags("#idea, then #work!") == (QStringList{"idea", "work"}));
    CHECK(extractTags("#nested/tag ok") == QStringList{"nested/tag"});
    CHECK(extractTags("line one\n#todo later") == QStringList{"todo"});
}

static void testRouting() {
    AppConfig c;
    c.tagRoutes = {{"work", "w"}, {"idea", "i"}, {"todo", "w"}};
    c.defaultConnection = "personal";
    CHECK(resolveTargets(c, {}) == QStringList{"personal"});
    CHECK(resolveTargets(c, {"random"}) == QStringList{"personal"});
    CHECK(resolveTargets(c, {"work", "idea", "todo"}) == (QStringList{"w", "i"}));
}

static void testShortcuts() {
    Shortcut s = parseShortcut("Alt+Space");
    CHECK(s.valid() && s.mods == kModAlt && s.vk == 0x20);
    s = parseShortcut("Ctrl+Shift+N");
    CHECK(s.valid() && s.mods == (kModControl | kModShift) && s.vk == 'N');
    CHECK(parseShortcut("Alt+Enter").vk == 0x0D);
    CHECK(parseShortcut("Alt+F5").vk == 0x74);
    CHECK(!parseShortcut("Bogus+Space").valid());
    CHECK(!parseShortcut("Alt+").valid());
    CHECK(shortcutToString(kModAlt | kModControl, 'K') == "Ctrl+Alt+K");
}

static const char kUserShapedConfig[] =
    "# comment\n"
    "shortcuts:\n"
    "  toggle_note: \"Alt+Space\"\n"
    "  save_note: \"Alt+Enter\"\n"
    "connections:\n"
    "  - name: personal\n"
    "    type: markdown\n"
    "    path: \"notes\"\n"
    "  - name: cloud\n"
    "    type: affine\n"
    "    sidecar_url: http://host.example.ts.net:8787\n"
    "    sidecar_token: s3cret\n"
    "    sidecar_connector: cloud\n"
    "routing:\n"
    "  tags: {} # personalnote: personal\n"
    "  default: cloud\n";

static void testConfig() {
    AppConfig c;
    QString err;
    CHECK(parseConfig(kUserShapedConfig, c, &err));
    CHECK(c.connections.size() == 2 && c.connections[1].sidecarUrl == "http://host.example.ts.net:8787");
    CHECK(c.defaultConnection == "cloud" && c.tagRoutes.isEmpty());
    CHECK(validateConfig(c).isEmpty());

    c.tagRoutes = {{"work", "personal"}, {"123", "cloud"}};
    const QByteArray yaml = serializeConfig(c);
    AppConfig again;
    CHECK(parseConfig(yaml, again, &err));
    CHECK(again.tagRoutes.size() == 2 && again.tagRoutes[1].tag == "123" && again.connections[1].sidecarToken == "s3cret");
    CHECK(yaml.contains("tags:"));

    AppConfig empty = c;
    empty.tagRoutes.clear();
    CHECK(serializeConfig(empty).contains("tags: {}"));  // a map, never null: serde needs it

    AppConfig bad = c;
    bad.connections[0].name = "cloud";  // duplicate
    CHECK(!validateConfig(bad).isEmpty());
    bad = c;
    bad.defaultConnection = "missing";
    CHECK(!validateConfig(bad).isEmpty());
    bad = c;
    bad.toggleNote = "Space";  // no modifier would hijack typing
    CHECK(!validateConfig(bad).isEmpty());

    // Partial configs the Tauri client accepts (serde defaults) must load here too.
    for (const char *partial : {"routing:\n  default: personal\n", "shortcuts:\n  toggle_note: \"Alt+Space\"\n  save_note: \"Alt+Enter\"\nrouting:\n  tags:\n  default: x\n", "", "connections: []\n"}) {
        AppConfig p;
        CHECK(parseConfig(partial, p, &err));
    }
    AppConfig bad2 = c;
    bad2.connections[1].sidecarToken = "abc\r\nX-Evil: 1";
    CHECK(!validateConfig(bad2).isEmpty());
    bad2 = c;
    bad2.connections[1].sidecarUrl = "ftp://host";
    CHECK(!validateConfig(bad2).isEmpty());
    bad2 = c;
    bad2.saveNote = "Space";
    CHECK(!validateConfig(bad2).isEmpty());

    AppConfig broken;
    CHECK(!parseConfig("connections: [ unterminated", broken, &err));
}

static void testWriters() {
    QTemporaryDir dir;
    const QDateTime now(QDate(2026, 9, 13), QTime(14, 5, 9));
    QString err;
    CHECK(writeMarkdown(dir.filePath("md"), "hello", now, &err));
    CHECK(writeMarkdown(dir.filePath("md"), "again", now, &err));
    CHECK(slurp(dir.filePath("md/2026-09-13.md")) ==
          "# Sunday, September 13 2026\n\n## 14:05:09\n\nhello\n\n## 14:05:09\n\nagain\n\n");
    CHECK(writeLogseq(dir.filePath("lg"), "one\ntwo", now, &err));
    CHECK(slurp(dir.filePath("lg/journals/2026_09_13.md")) == "- 14:05 one\n  two\n");
}

static void testRetry(const QString &sidecar) {
    QTemporaryDir dir;
    qputenv("APPDATA", dir.path().toLocal8Bit());
    QFile blocker(dir.filePath("QuickNote/blocker"));
    QDir().mkpath(dir.filePath("QuickNote"));
    CHECK(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    AppConfig c;
    c.defaultConnection = "personal";
    c.connections = {{"personal", "markdown", "notes", {}, {}, {}},
                     {"ideas", "logseq", "graph", {}, {}, {}},
                     {"broken", "markdown", "blocker/sub", {}, {}, {}}};
    c.tagRoutes = {{"idea", "ideas"}};
    if (!sidecar.isEmpty()) c.connections.append({"cloud", "affine", {}, sidecar, "test-token", "c1"});
    QString err;
    CHECK(saveConfig(c, &err));

    {
        Store s;
        CHECK(s.open(dbPath(), &err));
        CHECK(s.insert("2026-09-13T00:00:00Z", "plain note", "", "personal", "qt", &err));
        CHECK(s.insert("2026-09-13T00:00:01Z", "#idea queued before routing existed", "", "", "qt", &err));
        CHECK(s.insert("2026-09-13T00:00:02Z", "to two places", "", "personal,broken", "qt", &err));
        if (!sidecar.isEmpty()) CHECK(s.insert("2026-09-13T00:00:03Z", "to the sidecar", "", "cloud", "qt", &err));
    }
    const RetryResult r = retryPending();
    CHECK(r.error.isEmpty());
    CHECK(r.delivered == (sidecar.isEmpty() ? 2 : 3));
    CHECK(r.failed == 1 && r.pending == 1);

    Store s;
    CHECK(s.open(dbPath(), &err));
    const QList<NoteRow> left = s.pending();
    CHECK(left.size() == 1);
    if (left.size() == 1) {
        CHECK(left[0].targets == "broken");  // "personal" already succeeded: never re-sent
        CHECK(!left[0].error.isEmpty());
        CHECK(!left[0].error.contains("test-token"));
    }
    // The routed-late note went to logseq, not silently marked done.
    CHECK(QDir(dir.filePath("QuickNote/graph/journals")).exists());
    CHECK(QDir(dir.filePath("QuickNote/notes")).exists());

    const RetryResult second = retryPending();  // "broken" is still broken; nothing re-sent
    CHECK(second.delivered == 0 && second.failed == 1);
    const QByteArray daily = slurp(QDir(dir.filePath("QuickNote/notes")).entryInfoList(QDir::Files).value(0).filePath());
    CHECK(daily.count("to two places") == 1 && daily.count("plain note") == 1);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testTags();
    testRouting();
    testShortcuts();
    testConfig();
    testWriters();
    testRetry(qEnvironmentVariable("OFFNOTE_TEST_SIDECAR"));
    std::printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
