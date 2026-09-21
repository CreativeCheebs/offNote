package com.offnote.app

import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import kotlinx.coroutines.launch

/**
 * Visible-in-the-app status list for the local outbox, since delivery
 * failures (bad config, sidecar unreachable, wrong token, AFFiNE sign-in
 * errors) previously only ever landed silently in the Room DB's error
 * column with no UI to see them.
 */
class OutboxActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_outbox)

        findViewById<Button>(R.id.refreshButton).setOnClickListener {
            WorkManager.getInstance(applicationContext).enqueue(
                OneTimeWorkRequestBuilder<DeliveryWorker>().build(),
            )
            // Give the worker a moment to run before re-reading the DB.
            lifecycleScope.launch {
                kotlinx.coroutines.delay(1500)
                loadRows()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        loadRows()
    }

    private fun loadRows() {
        lifecycleScope.launch {
            val dao = NoteDatabase.get(applicationContext).noteDao()
            val rows = dao.recent(50)
            renderRows(rows)
        }
    }

    private fun renderRows(rows: List<NoteEntity>) {
        val container = findViewById<LinearLayout>(R.id.outboxList)
        container.removeAllViews()
        if (rows.isEmpty()) {
            container.addView(rowText("Nothing captured yet.", Color.GRAY))
            return
        }
        for (row in rows) {
            val status = when {
                row.error != null -> "FAILED: ${row.error}"
                row.processed -> "delivered -> ${row.targets}"
                else -> "pending -> ${row.targets}"
            }
            val color = when {
                row.error != null -> Color.parseColor("#C62828")
                row.processed -> Color.parseColor("#2E7D32")
                else -> Color.parseColor("#616161")
            }
            val text = "${row.createdAt}\n${row.data}\n$status\n"
            container.addView(rowText(text, color))
        }
    }

    private fun rowText(text: String, color: Int) = TextView(this).apply {
        this.text = text
        setTextColor(color)
        gravity = Gravity.START
        setPadding(0, 12, 0, 12)
    }
}
