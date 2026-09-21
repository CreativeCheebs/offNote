package com.offnote.app

import android.content.Context
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Shared "save" path for every capture entry point (main activity, lockscreen
 * quick-capture, RemoteInput reply): write to the durable Room outbox first,
 * then kick a background delivery attempt - mirrors save_note in
 * src-tauri/src/lib.rs, which never blocks the caller on network delivery.
 */
object NoteRepository {
    private fun nowIso(): String {
        val fmt = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US)
        fmt.timeZone = TimeZone.getTimeZone("UTC")
        return fmt.format(Date())
    }

    suspend fun save(context: Context, text: String, source: String = "android") {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return

        val config = ConfigStore.load(context)
        val tags = TagRouter.extractTags(trimmed)
        val targets = TagRouter.resolveTargets(config, tags)

        val dao = NoteDatabase.get(context).noteDao()
        dao.insert(
            NoteEntity(
                createdAt = nowIso(),
                data = trimmed,
                tags = tags.joinToString(","),
                targets = targets.joinToString(","),
                source = source,
            ),
        )

        WorkManager.getInstance(context).enqueue(
            OneTimeWorkRequestBuilder<DeliveryWorker>().build(),
        )
    }
}
