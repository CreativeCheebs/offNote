package com.offnote.app

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        val configInput = findViewById<EditText>(R.id.configInput)
        configInput.setText(ConfigStore.rawYaml(this))

        findViewById<Button>(R.id.saveConfigButton).setOnClickListener {
            try {
                ConfigStore.saveRawYaml(this, configInput.text.toString())
                Toast.makeText(this, "Config saved", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(this, "Invalid config: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }
    }
}
