package com.offnote.app

import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import com.offnote.shared.OffnoteTheme
import com.offnote.shared.SettingsScreen

class SettingsActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            OffnoteTheme {
                SettingsScreen(
                    initialYaml = ConfigStore.rawYaml(this),
                    onSave = { yaml ->
                        try {
                            ConfigStore.saveRawYaml(this, yaml)
                            Toast.makeText(this, "Config saved", Toast.LENGTH_SHORT).show()
                        } catch (e: Exception) {
                            Toast.makeText(this, "Invalid config: ${e.message}", Toast.LENGTH_LONG).show()
                        }
                    },
                    onBack = { finish() },
                )
            }
        }
    }
}
