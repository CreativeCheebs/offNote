package com.offnote.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch

class MainActivity : AppCompatActivity() {
    private val notifPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            // The foreground service starts unconditionally in OffnoteApp.onCreate,
            // but its notification stays invisible without this permission on
            // Android 13+; once granted, refresh it now instead of waiting for
            // the next service restart.
            if (granted) QuickCaptureNotification.refresh(applicationContext)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        requestNotificationPermissionIfNeeded()

        val noteInput = findViewById<EditText>(R.id.noteInput)
        findViewById<Button>(R.id.saveButton).setOnClickListener {
            val text = noteInput.text.toString()
            lifecycleScope.launch {
                NoteRepository.save(applicationContext, text, source = "android_app")
                Toast.makeText(applicationContext, "Queued", Toast.LENGTH_SHORT).show()
            }
            noteInput.text.clear()
        }
        findViewById<Button>(R.id.settingsButton).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
        findViewById<Button>(R.id.outboxButton).setOnClickListener {
            startActivity(Intent(this, OutboxActivity::class.java))
        }
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val granted = ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.POST_NOTIFICATIONS,
            ) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                notifPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }
}
