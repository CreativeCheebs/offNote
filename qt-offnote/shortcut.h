#pragma once
#include <QString>

// Win32 modifier flags (same values as MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN),
// kept here so this header does not pull in windows.h.
enum : unsigned { kModAlt = 1, kModControl = 2, kModShift = 4, kModWin = 8 };

struct Shortcut {
    unsigned mods = 0;
    unsigned vk = 0;  // Win32 virtual-key code; 0 means "not valid"
    bool valid() const { return vk != 0; }
};

// Parses "Alt+Space", "Ctrl+Shift+N", ... (same syntax as config.yaml in the other clients).
Shortcut parseShortcut(const QString &text);
// Canonical text for a combination, e.g. "Ctrl+Alt+N". Empty if the key has no name.
QString shortcutToString(unsigned mods, unsigned vk);
QString keyNameForVk(unsigned vk);
