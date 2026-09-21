package com.offnote.app

import android.os.Bundle
import android.view.WindowManager
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch

/**
 * Fallback capture surface shown over the lockscreen when the notification's
 * inline RemoteInput UI isn't available (some launchers/OEM skins hide it).
 * Saves on IME "Done" and finishes immediately - no waiting on delivery,
 * matching the desktop apps' instant-close-on-save behavior.
 */
class QuickCaptureActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(
            WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
                WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD,
        )
        setContentView(R.layout.activity_quick_capture)

        val input = findViewById<EditText>(R.id.captureInput)
        input.requestFocus()
        input.setOnEditorActionListener { _, _, _ ->
            saveAndClose(input.text.toString())
            true
        }
    }

    private fun saveAndClose(text: String) {
        lifecycleScope.launch {
            NoteRepository.save(applicationContext, text, source = "android_lockscreen")
        }
        finish()
    }
}
