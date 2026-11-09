package com.offnote.desktop

import com.offnote.shared.OutboxItem
import java.io.File
import java.net.HttpURLConnection
import java.net.URI
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.sql.Connection as SqlConnection
import java.sql.DriverManager
import java.time.LocalDateTime
import java.time.OffsetDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.yaml.snakeyaml.Yaml

internal data class Destination(
    val name: String,
    val type: String,
    val path: String?,
    val sidecarUrl: String?,
    val sidecarToken: String?,
    val sidecarConnector: String?,
)
internal data class DesktopConfig(
    val toggleShortcut: String,
    val saveShortcut: String,
    val connections: Map<String, Destination>,
    val tagRoutes: Map<String, String>,
    val defaultTarget: String,
)

internal object DesktopFiles {
    val directory: File = File(
        System.getenv("APPDATA")?.takeIf { it.isNotBlank() }
            ?: File(System.getProperty("user.home"), ".config").path,
        "QuickNote",
    )
    val config = File(directory, "config.yaml")
    val database = File(directory, "quicknote.db")

    fun ensureConfig() {
        directory.mkdirs()
        if (!config.exists()) {
            val seed = requireNotNull(javaClass.getResourceAsStream("/config.yaml")) { "Bundled config.yaml is missing" }
            seed.use { Files.copy(it, config.toPath()) }
        }
    }
}

internal object ConfigFile {
    private fun Map<*, *>.section(name: String): Map<*, *> = this[name] as? Map<*, *> ?: emptyMap<Any, Any>()
    private fun Map<*, *>.value(name: String): String? = this[name]?.toString()

    fun raw(): String { DesktopFiles.ensureConfig(); return DesktopFiles.config.readText() }

    fun parse(raw: String): DesktopConfig {
        val root = Yaml().load<Any?>(raw) as? Map<*, *> ?: error("Configuration must be a YAML mapping")
        val shortcuts = root.section("shortcuts")
        val connections = (root["connections"] as? List<*>)?.map { value ->
            val item = value as? Map<*, *> ?: error("Each connection must be a mapping")
            val name = item.value("name")?.trim().orEmpty()
            val type = item.value("type")?.trim().orEmpty()
            require(name.isNotBlank()) { "Connection needs a name" }
            require(type in setOf("markdown", "obsidian", "logseq", "affine")) { "Unknown connection type: $type" }
            if (type == "affine") {
                require(!item.value("sidecar_url").isNullOrBlank() && !item.value("sidecar_token").isNullOrBlank() && !item.value("sidecar_connector").isNullOrBlank()) { "$name needs sidecar settings" }
            } else require(!item.value("path").isNullOrBlank()) { "$name needs a path" }
            Destination(name, type, item.value("path"), item.value("sidecar_url"), item.value("sidecar_token"), item.value("sidecar_connector"))
        }.orEmpty()
        require(connections.map { it.name }.distinct().size == connections.size) { "Duplicate connection name" }
        val routing = root.section("routing")
        val tags = routing.section("tags").entries.associate { (key, value) -> key.toString() to value.toString() }
        val default = routing.value("default")?.trim().orEmpty()
        val byName = connections.associateBy { it.name }
        require(byName.containsKey(default)) { "Default route must name a connection" }
        require(tags.values.all(byName::containsKey)) { "A tag route names an unknown connection" }
        return DesktopConfig(
            toggleShortcut = shortcuts.value("toggle_note") ?: "Alt+Space",
            saveShortcut = shortcuts.value("save_note") ?: "Alt+Enter",
            connections = byName,
            tagRoutes = tags,
            defaultTarget = default,
        )
    }

    fun load(): DesktopConfig = parse(raw())

    fun save(raw: String) {
        parse(raw)
        DesktopFiles.ensureConfig()
        val temp = File(DesktopFiles.directory, "config.yaml.tmp")
        temp.writeText(raw)
        try {
            Files.move(temp.toPath(), DesktopFiles.config.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
        } catch (_: java.nio.file.AtomicMoveNotSupportedException) {
            Files.move(temp.toPath(), DesktopFiles.config.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
    }
}

internal object Tags {
    fun extract(text: String): List<String> {
        val result = mutableListOf<String>()
        val chars = text.toList()
        var i = 0
        while (i < chars.size) {
            if (chars[i] == '#' && (i == 0 || chars[i - 1].isWhitespace())) {
                var j = i + 1
                while (j < chars.size && !chars[j].isWhitespace() && chars[j] != '#') j++
                val tag = text.substring(i + 1, j).trimEnd { !it.isLetterOrDigit() && it != '_' && it != '-' && it != '/' }
                if (tag.isNotEmpty()) {
                    if (tag !in result) result.add(tag)
                    i = j
                    continue
                }
            }
            i++
        }
        return result
    }

    fun targets(config: DesktopConfig, tags: List<String>): List<String> =
        tags.mapNotNull(config.tagRoutes::get).distinct().ifEmpty { listOf(config.defaultTarget) }
}

internal data class PendingNote(val id: Long, val text: String, val targets: List<String>)

internal object NoteDb {
    private fun open(): SqlConnection {
        DesktopFiles.directory.mkdirs()
        val db = DriverManager.getConnection("jdbc:sqlite:${DesktopFiles.database.absolutePath}")
        db.createStatement().use { statement ->
            statement.execute("PRAGMA busy_timeout=5000")
            statement.execute("PRAGMA journal_mode=WAL")
            statement.execute("""CREATE TABLE IF NOT EXISTS notes (
                id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT NOT NULL, data TEXT NOT NULL,
                tags TEXT, targets TEXT, processed INTEGER NOT NULL DEFAULT 0,
                processed_at TEXT, error TEXT, source TEXT)""")
        }
        return db
    }

    fun insert(text: String, tags: List<String>, targets: List<String>) {
        open().use { db ->
            db.prepareStatement("INSERT INTO notes (created_at,data,tags,targets,processed,source) VALUES (?,?,?,?,0,?)").use { statement ->
                statement.setString(1, OffsetDateTime.now().toString())
                statement.setString(2, text)
                statement.setString(3, tags.joinToString(","))
                statement.setString(4, targets.joinToString(","))
                statement.setString(5, "compose_desktop")
                statement.executeUpdate()
            }
        }
    }

    fun recent(): List<OutboxItem> = open().use { db ->
        db.createStatement().use { statement ->
            statement.executeQuery("SELECT id,created_at,data,targets,processed,error FROM notes ORDER BY id DESC LIMIT 50").use { result ->
                buildList {
                    while (result.next()) {
                        val error = result.getString("error")
                        val targets = result.getString("targets").orEmpty()
                        add(OutboxItem(result.getLong("id"), result.getString("created_at"), result.getString("data"),
                            when { error != null -> "Failed: $error"; result.getInt("processed") != 0 -> "Delivered"; else -> "Pending for $targets" }, error != null))
                    }
                }
            }
        }
    }

    fun pending(): List<PendingNote> = open().use { db ->
        db.createStatement().use { statement ->
            statement.executeQuery("SELECT id,data,targets FROM notes WHERE processed=0").use { result ->
                buildList {
                    while (result.next()) add(PendingNote(result.getLong(1), result.getString(2), result.getString(3).orEmpty().split(',').filter(String::isNotBlank)))
                }
            }
        }
    }

    fun markDone(id: Long) = update("UPDATE notes SET processed=1,processed_at=?,error=NULL WHERE id=?", id, OffsetDateTime.now().toString())
    fun markFailed(id: Long, targets: List<String>, error: String) {
        update("UPDATE notes SET targets=? WHERE id=?", id, targets.joinToString(","))
        update("UPDATE notes SET error=? WHERE id=?", id, error)
    }
    private fun update(sql: String, id: Long, value: String) {
        open().use { db -> db.prepareStatement(sql).use { statement -> statement.setString(1, value); statement.setLong(2, id); statement.executeUpdate() } }
    }
}

internal object Delivery {
    private val worker = Executors.newSingleThreadScheduledExecutor { task -> Thread(task, "offnote-delivery").apply { isDaemon = true } }
    fun start() { worker.scheduleWithFixedDelay({ retrySafely() }, 0, 30, TimeUnit.SECONDS) }
    fun retry(onComplete: (() -> Unit)? = null) { worker.execute { retrySafely(); onComplete?.invoke() } }
    private fun retrySafely() { try { retryPending() } catch (e: Exception) { System.err.println("Delivery retry failed: ${e.message}") } }

    private fun retryPending() {
        val config = ConfigFile.load()
        for (note in NoteDb.pending()) {
            val remaining = mutableListOf<String>()
            val errors = mutableListOf<String>()
            for (name in note.targets) {
                try {
                    val destination = config.connections[name] ?: error("Unknown connection: $name")
                    deliver(destination, note.text)
                } catch (e: Exception) { remaining.add(name); errors.add("$name: ${e.message}") }
            }
            if (remaining.isEmpty()) NoteDb.markDone(note.id) else NoteDb.markFailed(note.id, remaining, errors.joinToString("; "))
        }
    }

    private fun deliver(destination: Destination, text: String) {
        when (destination.type) {
            "markdown", "obsidian" -> {
                val now = LocalDateTime.now()
                val dir = directory(destination.path!!)
                dir.mkdirs()
                val file = File(dir, now.format(DateTimeFormatter.ISO_LOCAL_DATE) + ".md")
                if (!file.exists()) file.appendText("# ${now.format(DateTimeFormatter.ofPattern("EEEE, MMMM d yyyy"))}\n\n")
                file.appendText("## ${now.format(DateTimeFormatter.ofPattern("HH:mm:ss"))}\n\n$text\n\n")
            }
            "logseq" -> {
                val now = LocalDateTime.now()
                val dir = File(directory(destination.path!!), "journals")
                dir.mkdirs()
                File(dir, now.format(DateTimeFormatter.ofPattern("yyyy_MM_dd")) + ".md")
                    .appendText("- ${now.format(DateTimeFormatter.ofPattern("HH:mm"))} ${text.replace("\n", "\n  ")}\n")
            }
            "affine" -> {
                val url = URI.create(destination.sidecarUrl!!.trimEnd('/') + "/append").toURL()
                val body = "{\"connector\":${json(destination.sidecarConnector!!)},\"texts\":[${json(text)}]}"
                val connection = url.openConnection() as HttpURLConnection
                try {
                    connection.requestMethod = "POST"
                    connection.connectTimeout = 10000
                    connection.readTimeout = 20000
                    connection.setRequestProperty("Authorization", "Bearer ${destination.sidecarToken}")
                    connection.setRequestProperty("Content-Type", "application/json")
                    connection.doOutput = true
                    connection.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                    if (connection.responseCode !in 200..299) error("Sidecar returned ${connection.responseCode}")
                } finally { connection.disconnect() }
            }
        }
    }

    private fun directory(path: String): File = File(path).let { if (it.isAbsolute) it else File(DesktopFiles.directory, path) }
    private fun json(value: String): String = buildString {
        append('"')
        for (char in value) when (char) {
            '"' -> append("\\\"")
            '\\' -> append("\\\\")
            '\n' -> append("\\n")
            '\r' -> append("\\r")
            '\t' -> append("\\t")
            else -> if (char.code < 32) append("\\u%04x".format(char.code)) else append(char)
        }
        append('"')
    }
}
