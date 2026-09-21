package com.offnote.app

import android.content.Context
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.yaml.snakeyaml.DumperOptions
import org.yaml.snakeyaml.Yaml

/**
 * A `connections[]` entry. Unlike the desktop apps' config.yaml, this never
 * holds AFFiNE credentials - those live server-side in the sidecar's own
 * connectors.json (see connectors/affine-sidecar). The phone only knows
 * where the sidecar is, how to authenticate to it, and which of its
 * server-side connectors to target by name.
 */
data class Connection(
    val name: String,
    val type: String, // "markdown" | "affine"
    val path: String? = null, // markdown only
    // affine only: the sidecar (connectors/affine-sidecar) to call, and which
    // of its server-side connector entries (connectors.json) to use.
    val sidecarUrl: String? = null,
    val sidecarToken: String? = null,
    val sidecarConnector: String? = null,
)

/** Mirrors `routing` in config.yaml: per-tag overrides plus a default target. */
data class RoutingConfig(
    val tags: Map<String, String> = emptyMap(),
    val default: String? = null,
)

data class AppConfig(
    val connections: List<Connection> = emptyList(),
    val routing: RoutingConfig = RoutingConfig(),
) {
    fun connection(name: String): Connection? = connections.find { it.name == name }
}

/**
 * Encrypted on-device equivalent of %APPDATA%\QuickNote\config.yaml. Stored
 * as YAML text, same format and field names as the desktop apps' config.yaml
 * (minus the shortcuts section, which doesn't apply on Android), so the same
 * mental model and snippets carry over between phone and desktop.
 */
object ConfigStore {
    private const val PREFS_NAME = "offnote_config"
    private const val KEY_CONFIG = "config_yaml"

    private fun yaml(): Yaml {
        val options = DumperOptions().apply {
            defaultFlowStyle = DumperOptions.FlowStyle.BLOCK
        }
        return Yaml(options)
    }

    private fun prefs(context: Context) = EncryptedSharedPreferences.create(
        context,
        PREFS_NAME,
        MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build(),
        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
    )

    fun rawYaml(context: Context): String =
        prefs(context).getString(KEY_CONFIG, null) ?: defaultYaml()

    fun saveRawYaml(context: Context, text: String) {
        val sanitized = sanitizeYamlText(text)
        // Validate before persisting so a typo doesn't brick the delivery worker.
        parse(sanitized)
        prefs(context).edit().putString(KEY_CONFIG, sanitized).apply()
    }

    fun load(context: Context): AppConfig = parse(rawYaml(context))

    /**
     * Mobile keyboards (Samsung's especially) apply "smart punctuation" that
     * silently swaps plain ASCII for lookalike Unicode - curly quotes,
     * en/em-dashes for hyphens, non-breaking spaces - even with autocorrect
     * suggestions disabled on the field. Those are invisible in the UI but
     * break YAML's block-sequence ("- ") and quoting syntax, producing
     * confusing parser errors. Normalize them back before parsing. Written
     * with \u escapes (not literal characters) so this fix can't itself fall
     * victim to the same kind of silent encoding mangling.
     */
    private fun sanitizeYamlText(text: String): String = text
        .replace('‘', '\'').replace('’', '\'')
        .replace('“', '"').replace('”', '"')
        .replace('–', '-').replace('—', '-')
        .replace(' ', ' ')

    private fun defaultYaml(): String = yaml().dump(
        mapOf(
            "connections" to emptyList<Any>(),
            "routing" to mapOf("tags" to emptyMap<String, String>(), "default" to null),
        ),
    )

    @Suppress("UNCHECKED_CAST")
    private fun parse(text: String): AppConfig {
        val root = (yaml().load<Any?>(text) as? Map<String, Any?>) ?: emptyMap()

        val connections = (root["connections"] as? List<Map<String, Any?>>).orEmpty().map { c ->
            Connection(
                name = c["name"] as? String ?: error("connection missing 'name'"),
                type = c["type"] as? String ?: error("connection '${c["name"]}' missing 'type'"),
                path = c["path"] as? String,
                sidecarUrl = c["sidecar_url"] as? String,
                sidecarToken = c["sidecar_token"] as? String,
                sidecarConnector = c["sidecar_connector"] as? String,
            )
        }

        val routingMap = root["routing"] as? Map<String, Any?> ?: emptyMap()
        val tags = (routingMap["tags"] as? Map<String, Any?>).orEmpty()
            .mapValues { it.value as String }
        val default = routingMap["default"] as? String

        return AppConfig(connections, RoutingConfig(tags, default))
    }
}
