package com.offnote.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Restarts the capture foreground service after a reboot (it's not persisted by the OS). */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED) {
            CaptureForegroundService.start(context.applicationContext)
        }
    }
}
