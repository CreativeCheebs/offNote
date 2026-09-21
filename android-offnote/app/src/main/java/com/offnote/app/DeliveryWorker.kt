package com.offnote.app

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Background retry pass, mirroring retry_pending() in src-tauri/src/lib.rs:
 * walk every unprocessed row, attempt delivery to each remaining target, and
 * narrow the row's targets to only what's still undelivered on partial
 * success - never re-post to a target that already succeeded.
 */
class DeliveryWorker(appContext: Context, params: WorkerParameters) :
    CoroutineWorker(appContext, params) {

    private fun nowIso(): String {
        val fmt = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US)
        fmt.timeZone = TimeZone.getTimeZone("UTC")
        return fmt.format(Date())
    }

    override suspend fun doWork(): Result {
        val dao = NoteDatabase.get(applicationContext).noteDao()
        val config = ConfigStore.load(applicationContext)

        for (note in dao.pending()) {
            val targets = note.targets.split(",").map { it.trim() }.filter { it.isNotEmpty() }
            if (targets.isEmpty()) {
                dao.markProcessed(note.id, nowIso())
                continue
            }

            val remaining = mutableListOf<String>()
            val errors = mutableListOf<String>()
            for (name in targets) {
                val connection = config.connection(name)
                val failure = if (connection == null) {
                    RuntimeException("routing points to connection '$name' which is not defined")
                } else {
                    deliver(connection, note.data)
                }
                if (failure != null) {
                    remaining.add(name)
                    errors.add("$name: ${failure.message}")
                }
            }

            if (remaining.isEmpty()) {
                dao.markProcessed(note.id, nowIso())
            } else {
                dao.updateTargets(note.id, remaining.joinToString(","))
                dao.markError(note.id, errors.joinToString("; "))
            }
        }
        return Result.success()
    }

    /** Returns null on success, or the failure to record otherwise. */
    private fun deliver(connection: Connection, data: String): Throwable? = try {
        when (connection.type) {
            "markdown" -> deliverMarkdown(connection, data)
            "affine" -> AffineSidecarClient.append(connection, data)
            else -> throw IllegalArgumentException("unknown connection type '${connection.type}'")
        }
        null
    } catch (e: Exception) {
        e
    }

    /** Appends to a markdown file under app-private storage (no SAF prompt needed). */
    private fun deliverMarkdown(connection: Connection, data: String) {
        val dir = File(applicationContext.filesDir, connection.path ?: "notes")
        dir.mkdirs()
        val file = File(dir, "offnote.md")
        file.appendText("\n- ${nowIso()}: $data\n")
    }
}
