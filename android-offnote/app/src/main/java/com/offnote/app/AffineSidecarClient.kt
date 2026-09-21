package com.offnote.app

import org.json.JSONArray
import org.json.JSONObject
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets

/**
 * Delivers a note to AFFiNE via the HTTP sidecar in connectors/affine-sidecar.
 * The phone never holds AFFiNE credentials - it only sends a connector name;
 * the sidecar resolves that to real email/password/workspace_id from its own
 * server-side connectors.json and runs the actual socket.io + Yjs CRDT sync.
 */
object AffineSidecarClient {
    fun append(connection: Connection, text: String) {
        val base = connection.sidecarUrl
            ?: throw IllegalStateException("connection '${connection.name}' has no sidecar_url configured")
        val token = connection.sidecarToken
            ?: throw IllegalStateException("connection '${connection.name}' has no sidecar_token configured")
        val connectorName = connection.sidecarConnector
            ?: throw IllegalStateException("connection '${connection.name}' has no sidecar_connector configured")

        val body = JSONObject().apply {
            put("connector", connectorName)
            put("texts", JSONArray().put(text))
        }.toString()

        val url = URL(base.trimEnd('/') + "/append")
        val conn = url.openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.connectTimeout = 15_000
        conn.readTimeout = 20_000
        conn.setRequestProperty("Content-Type", "application/json")
        conn.setRequestProperty("Authorization", "Bearer $token")

        try {
            OutputStreamWriter(conn.outputStream, StandardCharsets.UTF_8).use { it.write(body) }

            val status = conn.responseCode
            val stream = if (status in 200..299) conn.inputStream else conn.errorStream
            val responseText = stream?.bufferedReader(StandardCharsets.UTF_8)?.readText().orEmpty()

            if (status !in 200..299) {
                val message = runCatching { JSONObject(responseText).optString("error") }.getOrNull()
                throw RuntimeException("sidecar returned $status: ${message ?: responseText}")
            }
        } finally {
            conn.disconnect()
        }
    }
}
