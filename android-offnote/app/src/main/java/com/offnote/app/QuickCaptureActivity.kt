package com.offnote.app

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.lifecycleScope
import com.offnote.shared.OffnoteTheme
import com.offnote.shared.QuickCaptureScreen
import kotlinx.coroutines.launch

class QuickCaptureActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(
            WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
                WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD,
        )
        setContent {
            OffnoteTheme {
                QuickCaptureScreen { text ->
                    lifecycleScope.launch {
                        NoteRepository.save(applicationContext, text, source = "android_lockscreen")
                        finish()
                    }
                }
            }
        }
    }
}
