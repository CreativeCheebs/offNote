#include "shortcut.h"

#include <QStringList>

namespace {

struct KeyName {
    unsigned vk;
    const char *name;
};

const KeyName kNamedKeys[] = {
    {0x20, "Space"},  {0x0D, "Enter"},   {0x09, "Tab"},      {0x1B, "Escape"}, {0x08, "Backspace"},
    {0x2E, "Delete"}, {0x2D, "Insert"},  {0x24, "Home"},     {0x23, "End"},    {0x21, "PageUp"},
    {0x22, "PageDown"}, {0x26, "Up"},    {0x28, "Down"},     {0x25, "Left"},   {0x27, "Right"},
};

unsigned vkForName(const QString &lower) {
    if (lower.size() == 1) {
        QChar c = lower[0];
        if (c >= u'a' && c <= u'z') return 0x41 + (c.unicode() - u'a');
        if (c >= u'0' && c <= u'9') return 0x30 + (c.unicode() - u'0');
    }
    if (lower.size() >= 2 && lower[0] == u'f') {
        bool ok = false;
        int n = lower.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) return 0x70 + (n - 1);
    }
    for (const KeyName &k : kNamedKeys)
        if (lower == QString::fromLatin1(k.name).toLower()) return k.vk;
    if (lower == "return") return 0x0D;
    if (lower == "esc") return 0x1B;
    if (lower == "del") return 0x2E;
    if (lower == "ins") return 0x2D;
    if (lower == "pgup") return 0x21;
    if (lower == "pgdn") return 0x22;
    return 0;
}

}  // namespace

QString keyNameForVk(unsigned vk) {
    if (vk >= 0x41 && vk <= 0x5A) return QString(QChar(static_cast<char16_t>(vk)));
    if (vk >= 0x30 && vk <= 0x39) return QString(QChar(static_cast<char16_t>(vk)));
    if (vk >= 0x70 && vk <= 0x87) return QStringLiteral("F%1").arg(vk - 0x70 + 1);
    for (const KeyName &k : kNamedKeys)
        if (k.vk == vk) return QString::fromLatin1(k.name);
    return QString();
}

Shortcut parseShortcut(const QString &text) {
    Shortcut result;
    const QStringList parts = text.split('+');
    if (parts.size() < 1) return result;
    unsigned mods = 0;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString p = parts[i].trimmed().toLower();
        if (p == "alt" || p == "option") mods |= kModAlt;
        else if (p == "ctrl" || p == "control" || p == "cmdorctrl") mods |= kModControl;
        else if (p == "shift") mods |= kModShift;
        else if (p == "win" || p == "super" || p == "meta" || p == "cmd" || p == "command") mods |= kModWin;
        else return result;  // unknown modifier
    }
    const unsigned vk = vkForName(parts.last().trimmed().toLower());
    if (!vk) return result;
    result.mods = mods;
    result.vk = vk;
    return result;
}

QString shortcutToString(unsigned mods, unsigned vk) {
    const QString key = keyNameForVk(vk);
    if (key.isEmpty()) return QString();
    QStringList out;
    if (mods & kModControl) out << "Ctrl";
    if (mods & kModAlt) out << "Alt";
    if (mods & kModShift) out << "Shift";
    if (mods & kModWin) out << "Win";
    out << key;
    return out.join('+');
}
