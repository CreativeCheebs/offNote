package com.offnote.desktop

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class DesktopRoutingTest {
    private val config = """
        shortcuts:
          toggle_note: Alt+Space
          save_note: Alt+Enter
        connections:
          - name: personal
            type: markdown
            path: notes
          - name: work
            type: obsidian
            path: work-notes
        routing:
          tags:
            work: work
            todo: work
          default: personal
    """.trimIndent()

    @Test
    fun tagsMatchDesktopClientsAndRoutesAreDeduplicated() {
        val parsed = ConfigFile.parse(config)
        val tags = Tags.extract("#work #todo #work. # heading a#not-a-tag")
        assertEquals(listOf("work", "todo"), tags)
        assertEquals(listOf("work"), Tags.targets(parsed, tags))
        assertEquals(listOf("personal"), Tags.targets(parsed, Tags.extract("#unknown note")))
    }

    @Test
    fun invalidDefaultCannotReplaceWorkingConfig() {
        assertFailsWith<IllegalArgumentException> {
            ConfigFile.parse(config.replace("default: personal", "default: missing"))
        }
    }
}
