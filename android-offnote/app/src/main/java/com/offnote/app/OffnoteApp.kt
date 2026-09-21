package com.offnote.app

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Constraints
import java.util.concurrent.TimeUnit

// _v2: importance changed from LOW to DEFAULT (see below) - channel importance
// is immutable once created, so a new id is needed for the change to apply on
// devices that already installed the _v1 channel.
const val NOTIF_CHANNEL_CAPTURE = "offnote_capture_v2"
const val NOTIF_ID_CAPTURE = 1001

class OffnoteApp : Application() {
    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        CaptureForegroundService.start(this)
        schedulePeriodicRetry()
    }

    private fun createNotificationChannel() {
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        // DEFAULT, not LOW: One UI (and some other OEM skins) treat LOW/silent
        // channels as "silent notifications" and hide them from the lock
        // screen by default, independent of the per-app lock-screen toggle.
        val channel = NotificationChannel(
            NOTIF_CHANNEL_CAPTURE,
            getString(R.string.notif_channel_capture),
            NotificationManager.IMPORTANCE_DEFAULT,
        ).apply {
            description = getString(R.string.notif_channel_capture_desc)
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    /**
     * Android's WorkManager enforces a 15-minute floor on periodic work (unlike
     * the desktop apps' 30-second retry loop, which can run as a plain thread
     * sleep) - this is an OS battery-management limit, not a design choice.
     * A note that queues while offline still fires an immediate one-off retry
     * from NoteRepository.save(), so the 15-minute cadence only covers the
     * "stayed offline for a while" backlog case.
     */
    private fun schedulePeriodicRetry() {
        val constraints = Constraints.Builder()
            .setRequiredNetworkType(NetworkType.CONNECTED)
            .build()
        val request = PeriodicWorkRequestBuilder<DeliveryWorker>(15, TimeUnit.MINUTES)
            .setConstraints(constraints)
            .build()
        WorkManager.getInstance(this).enqueueUniquePeriodicWork(
            "offnote-retry",
            ExistingPeriodicWorkPolicy.KEEP,
            request,
        )
    }
}
