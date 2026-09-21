package com.offnote.app

/** Kotlin port of extract_tags/resolve_targets from src-tauri/src/lib.rs. */
object TagRouter {
    private val TAG_PATTERN = Regex("#([A-Za-z0-9_-]+)")

    fun extractTags(text: String): List<String> =
        TAG_PATTERN.findAll(text).map { it.groupValues[1] }.distinct().toList()

    /** Resolve which connection names a note should be delivered to. */
    fun resolveTargets(config: AppConfig, tags: List<String>): List<String> {
        val targets = LinkedHashSet<String>()
        for (tag in tags) {
            config.routing.tags[tag]?.let { targets.add(it) }
        }
        if (targets.isEmpty()) {
            config.routing.default?.let { targets.add(it) }
        }
        return targets.toList()
    }
}
