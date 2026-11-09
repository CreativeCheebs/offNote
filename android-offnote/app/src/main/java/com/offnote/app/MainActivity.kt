package com.offnote.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.offnote.shared.CaptureScreen
import com.offnote.shared.OffnoteTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private val notifPermissionLauncher = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) QuickCaptureNotification.refresh(applicationContext)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestNotificationPermissionIfNeeded()
        setContent {
            OffnoteTheme {
                CaptureScreen(
                    onSave = { text ->
                        lifecycleScope.launch {
                            NoteRepository.save(applicationContext, text, source = "android_app")
                            Toast.makeText(applicationContext, "Queued", Toast.LENGTH_SHORT).show()
                        }
                        true
                    },
                    onSettings = { startActivity(Intent(this, SettingsActivity::class.java)) },
                    onOutbox = { startActivity(Intent(this, OutboxActivity::class.java)) },
                )
            }
        }
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) notifPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
    }
}
