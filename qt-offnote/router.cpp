#include "router.h"

#include <QChar>
#include <string>

QStringList extractTags(const QString &text) {
    QStringList tags;
    const QList<uint> chars = text.toUcs4();
    const qsizetype n = chars.size();
    qsizetype i = 0;
    while (i < n) {
        const bool atBoundary = i == 0 || QChar::isSpace(chars[i - 1]);
        if (chars[i] == '#' && atBoundary) {
            qsizetype j = i + 1;
            std::u32string word;
            while (j < n && !QChar::isSpace(chars[j]) && chars[j] != '#') word.push_back(chars[j++]);
            while (!word.empty()) {
                const char32_t c = word.back();
                if (QChar::isLetterOrNumber(c) || c == '_' || c == '-' || c == '/') break;
                word.pop_back();
            }
            if (!word.empty()) {
                const QString tag = QString::fromUcs4(word.data(), static_cast<qsizetype>(word.size()));
                if (!tags.contains(tag)) tags.append(tag);
                i = j;
                continue;
            }
        }
        ++i;
    }
    return tags;
}

QStringList resolveTargets(const AppConfig &config, const QStringList &tags) {
    QStringList names;
    for (const QString &tag : tags) {
        for (const TagRoute &route : config.tagRoutes) {
            if (route.tag == tag && !names.contains(route.connection)) names.append(route.connection);
        }
    }
    if (names.isEmpty() && !config.defaultConnection.isEmpty()) names.append(config.defaultConnection);
    return names;
}
