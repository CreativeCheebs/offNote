package com.offnote.app

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.content.ContextCompat

/**
 * Keeps the quick-capture notification alive as a true foreground service.
 * Verified on a real device: a plain setOngoing(true) notification (not
 * backed by a running foreground service) is swipe-dismissible and is NOT
 * guaranteed to appear on a secure lock screen on modern Android/One UI -
 * only an active foreground service's notification gets that treatment.
 */
class CaptureForegroundService : Service() {
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = QuickCaptureNotification.build(this)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIF_ID_CAPTURE, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NOTIF_ID_CAPTURE, notification)
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, CaptureForegroundService::class.java))
        }
    }
}
