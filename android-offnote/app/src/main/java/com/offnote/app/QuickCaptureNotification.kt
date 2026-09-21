package com.offnote.app

import android.app.Notification
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.RemoteInput

const val EXTRA_REPLY_TEXT = "extra_reply_text"
const val KEY_QUICK_REPLY = "key_quick_reply"

/**
 * The always-there lockscreen capture notification. Its RemoteInput action
 * lets a note be typed and submitted from the notification shade/lockscreen
 * without unlocking or opening the app - the Android analogue of the desktop
 * apps' Alt+Space/Alt+Enter global-hotkey popup.
 *
 * build() is used by CaptureForegroundService.startForeground() - a plain
 * NotificationCompat.Builder().notify() is NOT swipe-proof and isn't
 * guaranteed to show on a secure lock screen (verified on a real Samsung
 * device); only a notification backing an active foreground service is.
 */
object QuickCaptureNotification {
    fun build(context: Context): Notification {
        val remoteInput = RemoteInput.Builder(KEY_QUICK_REPLY)
            .setLabel(context.getString(R.string.notif_reply_label))
            .build()

        val replyIntent = Intent(context, QuickCaptureReceiver::class.java)
        val replyPendingIntent = PendingIntent.getBroadcast(
            context,
            0,
            replyIntent,
            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        val replyAction = NotificationCompat.Action.Builder(
            android.R.drawable.ic_input_add,
            context.getString(R.string.notif_action_add),
            replyPendingIntent,
        ).addRemoteInput(remoteInput).build()

        // Fallback tap target: opens a full-screen capture activity, in case
        // the launcher/lockscreen doesn't surface the inline RemoteInput UI.
        val tapIntent = Intent(context, QuickCaptureActivity::class.java)
        val tapPendingIntent = PendingIntent.getActivity(
            context,
            0,
            tapIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        return NotificationCompat.Builder(context, NOTIF_CHANNEL_CAPTURE)
            .setSmallIcon(android.R.drawable.ic_menu_edit)
            .setContentTitle(context.getString(R.string.notif_title))
            .setContentText(context.getString(R.string.notif_text))
            .setContentIntent(tapPendingIntent)
            .addAction(replyAction)
            .setOngoing(true)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)
            .build()
    }

    /** Re-posts with fresh PendingIntents; the foreground service must already be running. */
    fun refresh(context: Context) {
        NotificationManagerCompat.from(context).notify(NOTIF_ID_CAPTURE, build(context))
    }
}
