package com.offnote.shared

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester

@Composable
fun OffnoteTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme(), content = content)
}

@Composable
fun CaptureScreen(
    onSave: (String) -> Boolean,
    onSettings: () -> Unit,
    onOutbox: () -> Unit,
    status: String = "",
    isSaveKey: (KeyEvent) -> Boolean = { false },
    isDismissKey: (KeyEvent) -> Boolean = { false },
    onDismiss: () -> Unit = {},
    focusToken: Int = 0,
) {
    var note by remember { mutableStateOf("") }
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(focusToken) { withFrameNanos { }; focusRequester.requestFocus() }
    Scaffold(topBar = { Text("Offnote", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(20.dp)) }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            OutlinedTextField(
                value = note, onValueChange = { note = it }, label = { Text("Quick note") },
                modifier = Modifier.fillMaxWidth().weight(1f).focusRequester(focusRequester).onPreviewKeyEvent { event ->
                    if (event.type != KeyEventType.KeyDown) false
                    else if (isSaveKey(event)) {
                        if (note.isNotBlank() && onSave(note.trim())) note = ""
                        true
                    } else if (isDismissKey(event)) {
                        onDismiss()
                        true
                    } else false
                },
                minLines = 5,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, keyboardType = KeyboardType.Text),
            )
            Button(onClick = { if (onSave(note.trim())) note = "" }, enabled = note.isNotBlank(), modifier = Modifier.fillMaxWidth()) { Text("Save note") }
            if (status.isNotBlank()) Text(status, color = MaterialTheme.colorScheme.error)
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(onClick = onOutbox) { Text("Outbox") }
                OutlinedButton(onClick = onSettings) { Text("Settings") }
            }
        }
    }
}

@Composable
fun QuickCaptureScreen(onSave: (String) -> Unit) {
    var note by remember { mutableStateOf("") }
    Column(Modifier.fillMaxWidth().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Quick capture", style = MaterialTheme.typography.headlineSmall)
        OutlinedTextField(
            note, onValueChange = { note = it }, label = { Text("Note") },
            modifier = Modifier.fillMaxWidth(), minLines = 3,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { if (note.isNotBlank()) onSave(note.trim()) }),
        )
        Button(onClick = { onSave(note.trim()) }, enabled = note.isNotBlank(), modifier = Modifier.fillMaxWidth()) { Text("Save note") }
    }
}

@Composable
fun SettingsScreen(initialYaml: String, onSave: (String) -> Unit, onBack: () -> Unit, status: String = "") {
    var yaml by remember(initialYaml) { mutableStateOf(initialYaml) }
    Scaffold { padding ->
        Column(Modifier.fillMaxSize().padding(padding).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Settings", style = MaterialTheme.typography.headlineMedium)
            Text("Connections and routing (YAML)")
            if (status.isNotBlank()) Text(status, color = MaterialTheme.colorScheme.onSurfaceVariant)
            OutlinedTextField(value = yaml, onValueChange = { yaml = it }, modifier = Modifier.fillMaxWidth().weight(1f), label = { Text("Configuration") })
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(onClick = { onSave(yaml) }) { Text("Save configuration") }
                OutlinedButton(onClick = onBack) { Text("Back") }
            }
        }
    }
}

data class OutboxItem(val id: Long, val createdAt: String, val text: String, val status: String, val failed: Boolean)

@Composable
fun OutboxScreen(items: List<OutboxItem>, onRetry: () -> Unit, onBack: () -> Unit, status: String = "") {
    Scaffold { padding ->
        Column(Modifier.fillMaxSize().padding(padding).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Outbox", style = MaterialTheme.typography.headlineMedium)
            if (status.isNotBlank()) Text(status, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(onClick = onRetry) { Text("Retry delivery") }
                OutlinedButton(onClick = onBack) { Text("Back") }
            }
            if (items.isEmpty()) Text("Nothing captured yet.")
            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(items, key = { it.id }) { item ->
                    Card(Modifier.fillMaxWidth()) {
                        Column(Modifier.padding(16.dp)) {
                            Text(item.createdAt, style = MaterialTheme.typography.labelSmall)
                            Text(item.text, style = MaterialTheme.typography.bodyLarge)
                            Text(item.status, color = if (item.failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }
}
