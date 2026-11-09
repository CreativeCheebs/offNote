#pragma once
#include "config.h"

// Same rules as the Tauri client (src-tauri/src/lib.rs): a tag is a '#' at the start of
// the text or after whitespace, followed by non-space, non-'#' characters; trailing
// punctuation is trimmed; duplicates are dropped.
QStringList extractTags(const QString &text);
// Connections mapped by the note's tags (deduplicated), or the default connection.
QStringList resolveTargets(const AppConfig &config, const QStringList &tags);
