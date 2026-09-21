package com.offnote.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.app.RemoteInput
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * Handles the RemoteInput reply from the persistent lockscreen notification.
 * Runs headless (no activity, no unlock required) - the text goes straight to
 * the durable outbox, then the notification is re-posted so it's ready for
 * the next note.
 */
class QuickCaptureReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val text = RemoteInput.getResultsFromIntent(intent)
            ?.getCharSequence(KEY_QUICK_REPLY)
            ?.toString()
            ?.trim()
            .orEmpty()

        val pendingResult = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                if (text.isNotEmpty()) {
                    NoteRepository.save(context.applicationContext, text, source = "android_notification")
                }
                QuickCaptureNotification.refresh(context.applicationContext)
            } finally {
                pendingResult.finish()
            }
        }
    }
}
