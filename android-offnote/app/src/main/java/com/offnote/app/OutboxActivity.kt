package com.offnote.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.lifecycle.lifecycleScope
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import com.offnote.shared.OffnoteTheme
import com.offnote.shared.OutboxItem
import com.offnote.shared.OutboxScreen
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class OutboxActivity : ComponentActivity() {
    private val rows = mutableStateListOf<OutboxItem>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            OffnoteTheme {
                OutboxScreen(rows, onRetry = {
                    WorkManager.getInstance(applicationContext).enqueue(OneTimeWorkRequestBuilder<DeliveryWorker>().build())
                    lifecycleScope.launch { delay(1500); loadRows() }
                }, onBack = { finish() })
            }
        }
    }

    override fun onResume() {
        super.onResume()
        lifecycleScope.launch { loadRows() }
    }

    private suspend fun loadRows() {
        val notes = NoteDatabase.get(applicationContext).noteDao().recent(50)
        rows.clear()
        rows.addAll(notes.map { note ->
            OutboxItem(
                id = note.id,
                createdAt = note.createdAt,
                text = note.data,
                status = when {
                    note.error != null -> "Failed: ${note.error}"
                    note.processed -> "Delivered to ${note.targets}"
                    else -> "Pending for ${note.targets}"
                },
                failed = note.error != null,
            )
        })
    }
}
