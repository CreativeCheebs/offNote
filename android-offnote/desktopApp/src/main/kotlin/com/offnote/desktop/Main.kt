package com.offnote.desktop

import androidx.compose.runtime.*
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.isAltPressed
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.isMetaPressed
import androidx.compose.ui.input.key.isShiftPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.github.kwhat.jnativehook.GlobalScreen
import com.github.kwhat.jnativehook.NativeInputEvent
import com.github.kwhat.jnativehook.keyboard.NativeKeyEvent
import com.github.kwhat.jnativehook.keyboard.NativeKeyListener
import com.offnote.shared.CaptureScreen
import com.offnote.shared.OffnoteTheme
import com.offnote.shared.OutboxScreen
import com.offnote.shared.SettingsScreen
import java.awt.Color
import java.awt.EventQueue
import java.awt.MenuItem
import java.awt.PopupMenu
import java.awt.SystemTray
import java.awt.TrayIcon
import java.awt.image.BufferedImage
import kotlin.system.exitProcess
import java.util.concurrent.atomic.AtomicBoolean

private enum class Page { CAPTURE, SETTINGS, OUTBOX }

private data class Shortcut(val key: String, val alt: Boolean, val ctrl: Boolean, val shift: Boolean, val meta: Boolean) {
    companion object {
        fun parse(raw: String): Shortcut {
            val parts = raw.split('+').map { it.trim().lowercase() }
            return Shortcut(
                key = parts.lastOrNull().orEmpty(),
                alt = "alt" in parts || "option" in parts,
                ctrl = "ctrl" in parts || "control" in parts || ("cmdorctrl" in parts && !isMac()),
                shift = "shift" in parts,
                meta = "cmd" in parts || "command" in parts || "meta" in parts || "win" in parts || "super" in parts || ("cmdorctrl" in parts && isMac()),
            )
        }
    }
    fun matches(event: KeyEvent): Boolean =
        event.key == when (key) { "enter", "return" -> Key.Enter; "space" -> Key.Spacebar; "n" -> Key.N; else -> Key.Unknown } &&
            event.isAltPressed == alt && event.isCtrlPressed == ctrl && event.isShiftPressed == shift && event.isMetaPressed == meta

    fun matches(event: NativeKeyEvent): Boolean =
        event.keyCode == when (key) { "space" -> NativeKeyEvent.VC_SPACE; "enter", "return" -> NativeKeyEvent.VC_ENTER; "n" -> NativeKeyEvent.VC_N; else -> -1 } &&
            ((event.modifiers and NativeInputEvent.ALT_MASK) != 0) == alt &&
            ((event.modifiers and NativeInputEvent.CTRL_MASK) != 0) == ctrl &&
            ((event.modifiers and NativeInputEvent.SHIFT_MASK) != 0) == shift &&
            ((event.modifiers and NativeInputEvent.META_MASK) != 0) == meta
}

private fun isMac() = System.getProperty("os.name").lowercase().contains("mac")

private class DesktopController {
    var visible by mutableStateOf(false)
    var page by mutableStateOf(Page.CAPTURE)
    var status by mutableStateOf("")
    var settings by mutableStateOf(ConfigFile.raw())
    var outbox by mutableStateOf(emptyList<com.offnote.shared.OutboxItem>())
    var config by mutableStateOf(ConfigFile.load())
    var focusSequence by mutableIntStateOf(0)
    private var tray: TrayIcon? = null
    private var hook: NativeKeyListener? = null

    fun show(page: Page = Page.CAPTURE) {
        this.page = page
        if (page == Page.OUTBOX) refreshOutbox()
        if (page == Page.SETTINGS) settings = ConfigFile.raw()
        status = ""
        visible = true
        focusSequence++
    }
    fun hide() { visible = false }
    fun save(text: String): Boolean {
        return try {
            val trimmed = text.trim()
            if (trimmed.isEmpty()) false else {
                val tags = Tags.extract(trimmed)
                NoteDb.insert(trimmed, tags, Tags.targets(config, tags))
                Delivery.retry()
                hide()
                true
            }
        } catch (e: Exception) {
            status = "Save failed: ${e.message}"
            false
        }
    }
    fun saveSettings(raw: String) {
        try {
            ConfigFile.save(raw)
            config = ConfigFile.load()
            settings = raw
            status = "Configuration saved"
            Delivery.retry()
        } catch (e: Exception) { status = "Configuration error: ${e.message}" }
    }
    fun refreshOutbox() { try { outbox = NoteDb.recent() } catch (e: Exception) { status = "Outbox error: ${e.message}" } }
    fun retry() { Delivery.retry { EventQueue.invokeLater { refreshOutbox() } } }

    fun startIntegrations() {
        Delivery.start()
        if (SystemTray.isSupported()) {
            val image = BufferedImage(32, 32, BufferedImage.TYPE_INT_ARGB)
            image.createGraphics().use { graphics ->
                graphics.color = Color(80, 55, 180)
                graphics.fillRoundRect(2, 2, 28, 28, 8, 8)
                graphics.color = Color.WHITE
                graphics.font = graphics.font.deriveFont(22f)
                graphics.drawString("O", 8, 24)
            }
            val menu = PopupMenu().apply {
                add(MenuItem("New note").apply { addActionListener { EventQueue.invokeLater { show() } } })
                add(MenuItem("Outbox").apply { addActionListener { EventQueue.invokeLater { show(Page.OUTBOX) } } })
                add(MenuItem("Settings").apply { addActionListener { EventQueue.invokeLater { show(Page.SETTINGS) } } })
                addSeparator()
                add(MenuItem("Quit").apply { addActionListener { exitProcess(0) } })
            }
            tray = TrayIcon(image, "Offnote", menu).apply { isImageAutoSize = true; addActionListener { EventQueue.invokeLater { show() } } }
            SystemTray.getSystemTray().add(tray)
        } else visible = true
        try {
            GlobalScreen.registerNativeHook()
            val pressedShortcut = AtomicBoolean(false)
            hook = object : NativeKeyListener {
                override fun nativeKeyPressed(event: NativeKeyEvent) {
                    if (Shortcut.parse(config.toggleShortcut).matches(event) && pressedShortcut.compareAndSet(false, true)) {
                        EventQueue.invokeLater { if (visible) hide() else show() }
                    }
                }
                override fun nativeKeyReleased(event: NativeKeyEvent) {
                    if (event.keyCode !in setOf(NativeKeyEvent.VC_ALT, NativeKeyEvent.VC_CONTROL, NativeKeyEvent.VC_SHIFT, NativeKeyEvent.VC_META)) pressedShortcut.set(false)
                }
                override fun nativeKeyTyped(event: NativeKeyEvent) = Unit
            }
            GlobalScreen.addNativeKeyListener(hook)
        } catch (e: Exception) {
            status = "Global shortcut unavailable: ${e.message}"
            visible = true
        }
        Runtime.getRuntime().addShutdownHook(Thread {
            hook?.let(GlobalScreen::removeNativeKeyListener)
            if (GlobalScreen.isNativeHookRegistered()) GlobalScreen.unregisterNativeHook()
            tray?.let { SystemTray.getSystemTray().remove(it) }
        })
    }
}

private inline fun <T : java.awt.Graphics2D> T.use(block: (T) -> Unit) { try { block(this) } finally { dispose() } }

fun main() {
    val controller = DesktopController()
    application {
        LaunchedEffect(Unit) { controller.startIntegrations() }
        Window(
            onCloseRequest = { controller.hide() },
            visible = controller.visible,
            title = "Offnote",
            state = rememberWindowState(width = 520.dp, height = 430.dp),
            alwaysOnTop = controller.page == Page.CAPTURE,
        ) {
            LaunchedEffect(controller.visible) { if (controller.visible) { window.toFront(); window.requestFocus() } }
            OffnoteTheme {
                when (controller.page) {
                    Page.CAPTURE -> CaptureScreen(
                        onSave = controller::save,
                        onSettings = { controller.show(Page.SETTINGS) },
                        onOutbox = { controller.show(Page.OUTBOX) },
                        status = controller.status,
                        isSaveKey = {
                            Shortcut.parse(controller.config.saveShortcut).matches(it) ||
                                Shortcut.parse("Ctrl+Enter").matches(it) || Shortcut.parse("Alt+Enter").matches(it)
                        },
                        isDismissKey = { it.key == Key.Escape },
                        onDismiss = controller::hide,
                        focusToken = controller.focusSequence,
                    )
                    Page.SETTINGS -> SettingsScreen(
                        initialYaml = controller.settings,
                        onSave = controller::saveSettings,
                        onBack = { controller.show() },
                        status = controller.status,
                    )
                    Page.OUTBOX -> OutboxScreen(
                        items = controller.outbox,
                        onRetry = controller::retry,
                        onBack = { controller.show() },
                        status = controller.status,
                    )
                }
            }
        }
    }
}
