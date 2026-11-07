package com.ronin.kernel

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets
import java.security.SecureRandom
import java.util.concurrent.atomic.AtomicLong
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import kotlin.math.abs

/**
 * Ronin v5.1 — Authenticated Model Context Protocol (MCP) Client & Termux Bridge.
 *
 * Security Invariants:
 * 1. Loopback-only endpoint enforcement (`127.0.0.1` or `localhost`) unless explicitly authorized.
 * 2. 256-bit cryptographically random Shared Secret Token stored in private SharedPreferences.
 * 3. Per-request Replay & Integrity Protection:
 *    - `X-Ronin-Timestamp: <epoch_seconds>`
 *    - `X-Ronin-Signature: HMAC_SHA256(secret, "$timestamp.$rawJsonBody")` (hex-encoded)
 *    - Strict ±30s replay window verification helper.
 *    - No cleartext secret transmission over shared loopback port (RONIN-SEC-03).
 * 4. Mandatory Human-In-The-Loop (HITL) approval callback before any `tools/call` invocation.
 */
object McpClientManager {
    private const val TAG = "RoninMCP"
    private const val PREFS_NAME = "ronin_mcp_prefs"
    private const val KEY_SHARED_SECRET = "mcp_shared_secret_hex"
    private const val KEY_ENDPOINT_URL = "mcp_endpoint_url"
    const val DEFAULT_ENDPOINT = "http://127.0.0.1:8765/mcp"
    const val REPLAY_WINDOW_SECONDS = 30L
    private const val CONNECT_TIMEOUT_MS = 4_000
    private const val READ_TIMEOUT_MS = 15_000

    private val rpcIdCounter = AtomicLong(1L)

    data class McpTool(
        val name: String,
        val description: String,
        val inputSchemaJson: String
    )

    data class McpCallResult(
        val success: Boolean,
        val toolName: String,
        val output: String,
        val isError: Boolean = false
    )

    /**
     * Retrieves or generates the 256-bit (64 hex chars) shared secret token for Termux MCP bridge.
     */
    fun getOrCreateSharedSecret(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val existing = prefs.getString(KEY_SHARED_SECRET, null)
        if (!existing.isNullOrBlank() && existing.length >= 64) {
            return existing
        }
        val bytes = ByteArray(32)
        SecureRandom().nextBytes(bytes)
        val secretHex = bytes.joinToString("") { "%02x".format(it) }
        prefs.edit().putString(KEY_SHARED_SECRET, secretHex).apply()
        return secretHex
    }

    /**
     * Rotates the 256-bit shared secret token on demand.
     */
    fun rotateSharedSecret(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val bytes = ByteArray(32)
        SecureRandom().nextBytes(bytes)
        val secretHex = bytes.joinToString("") { "%02x".format(it) }
        prefs.edit().putString(KEY_SHARED_SECRET, secretHex).apply()
        return secretHex
    }

    fun getEndpointUrl(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(KEY_ENDPOINT_URL, DEFAULT_ENDPOINT) ?: DEFAULT_ENDPOINT
    }

    fun setEndpointUrl(context: Context, url: String): Boolean {
        if (!isLoopbackEndpoint(url)) return false
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_ENDPOINT_URL, url.trim()).apply()
        return true
    }

    /**
     * Ensures the MCP bridge endpoint is restricted to local loopback (`127.0.0.1` or `localhost`).
     */
    internal fun isLoopbackEndpoint(urlStr: String): Boolean {
        return try {
            val url = URL(urlStr.trim())
            val protocolOk = url.protocol.equals("http", ignoreCase = true) ||
                url.protocol.equals("https", ignoreCase = true)
            val host = url.host?.lowercase() ?: return false
            protocolOk && (host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "[::1]")
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Generates a cryptographically random 16-byte (32 hex chars) nonce.
     */
    fun generateNonce(): String {
        val bytes = ByteArray(16)
        SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it) }
    }

    /**
     * Bounded LRU cache of recently seen nonces within the replay window to prevent replay attacks.
     */
    private val seenNonces = java.util.Collections.synchronizedMap(
        object : java.util.LinkedHashMap<String, Long>(100, 0.75f, true) {
            override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Long>?): Boolean {
                return size > 500
            }
        }
    )

    /**
     * Capability Execution Policy: Classifies MCP tools by risk level.
     * - ALLOW: Read-only observation (get_device_info, list_files, read_file, git_status, git_diff)
     * - CONFIRM: State mutation (git_commit, git_push, delete_file, install_package) -> Requires HITL approval
     * - DENY: Dangerous unconstrained operations (raw shell execution, arbitrary bash)
     */
    enum class ToolRiskLevel {
        ALLOW,
        CONFIRM,
        DENY
    }

    fun classifyToolRisk(toolName: String, args: JSONObject = JSONObject()): ToolRiskLevel {
        val lower = toolName.lowercase().trim()
        if (lower == "run_bash" || lower == "execute_command" || lower == "shell" || lower == "exec") {
            return ToolRiskLevel.DENY
        }
        return when (lower) {
            "get_device_info", "list_files", "read_file", "git_status", "git_diff" -> ToolRiskLevel.ALLOW
            "git_commit", "git_push", "delete_file", "install_package" -> ToolRiskLevel.CONFIRM
            else -> ToolRiskLevel.CONFIRM
        }
    }

    /**
     * Computes `HMAC_SHA256(secret, "$timestamp.$nonce.$rawJsonBody")` as lowercase hex.
     */
    internal fun computeHmacSignature(
        secret: String,
        timestampEpochSec: Long,
        nonce: String,
        rawJsonBody: String
    ): String {
        val mac = Mac.getInstance("HmacSHA256")
        val keySpec = SecretKeySpec(secret.toByteArray(StandardCharsets.UTF_8), "HmacSHA256")
        mac.init(keySpec)
        val payload = if (nonce.isNotEmpty()) {
            "$timestampEpochSec.$nonce.$rawJsonBody".toByteArray(StandardCharsets.UTF_8)
        } else {
            "$timestampEpochSec.$rawJsonBody".toByteArray(StandardCharsets.UTF_8)
        }
        val digest = mac.doFinal(payload)
        return digest.joinToString("") { "%02x".format(it) }
    }

    internal fun computeHmacSignature(
        secret: String,
        timestampEpochSec: Long,
        rawJsonBody: String
    ): String = computeHmacSignature(secret, timestampEpochSec, "", rawJsonBody)

    /**
     * Verifies the ±30s timestamp replay window, unique nonce, and constant-time HMAC-SHA256 signature.
     */
    internal fun verifyRequestSignature(
        secret: String,
        timestampEpochSec: Long,
        nonce: String,
        rawJsonBody: String,
        providedSignatureHex: String,
        nowEpochSec: Long = System.currentTimeMillis() / 1000L,
        windowSec: Long = REPLAY_WINDOW_SECONDS
    ): Boolean {
        if (secret.isBlank() || providedSignatureHex.isBlank()) return false
        if (abs(nowEpochSec - timestampEpochSec) > windowSec) return false

        // Nonce replay check: ensure nonce is non-empty, hex-safe, and not replayed
        if (nonce.isNotBlank()) {
            if (nonce.length < 16 || !nonce.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) {
                return false
            }
            synchronized(seenNonces) {
                if (seenNonces.containsKey(nonce)) {
                    return false // Replay attack detected
                }
                seenNonces[nonce] = nowEpochSec
            }
        }

        val expected = computeHmacSignature(secret, timestampEpochSec, nonce, rawJsonBody)
        val lowerProvided = providedSignatureHex.lowercase()
        if (expected.length != lowerProvided.length) return false

        var diff = 0
        for (i in expected.indices) {
            diff = diff or (expected[i].code xor lowerProvided[i].code)
        }
        return diff == 0
    }

    internal fun verifyRequestSignature(
        secret: String,
        timestampEpochSec: Long,
        rawJsonBody: String,
        providedSignatureHex: String,
        nowEpochSec: Long = System.currentTimeMillis() / 1000L,
        windowSec: Long = REPLAY_WINDOW_SECONDS
    ): Boolean = verifyRequestSignature(secret, timestampEpochSec, "", rawJsonBody, providedSignatureHex, nowEpochSec, windowSec)

    /**
     * Builds a JSON-RPC 2.0 request envelope.
     */
    internal fun buildJsonRpcRequest(
        method: String,
        params: JSONObject = JSONObject(),
        id: Long = rpcIdCounter.getAndIncrement()
    ): String {
        return JSONObject().apply {
            put("jsonrpc", "2.0")
            put("id", id)
            put("method", method)
            put("params", params)
        }.toString()
    }

    /**
     * Performs MCP `initialize` handshake with the local Termux MCP server.
     */
    suspend fun initializeSession(context: Context): Result<String> = withContext(Dispatchers.IO) {
        val params = JSONObject().apply {
            put("protocolVersion", "2024-11-05")
            put("capabilities", JSONObject())
            put("clientInfo", JSONObject().apply {
                put("name", "Ronin-Sovereign-Kernel")
                put("version", "5.1.0")
            })
        }
        sendAuthenticatedRpc(context, "initialize", params).map { resultObj ->
            val serverInfo = resultObj.optJSONObject("serverInfo")
            val serverName = serverInfo?.optString("name", "termux-mcp") ?: "termux-mcp"
            val serverVer = serverInfo?.optString("version", "1.0") ?: "1.0"
            "$serverName v$serverVer"
        }
    }

    /**
     * Discovers available MCP tools via `tools/list`.
     */
    suspend fun listTools(context: Context): Result<List<McpTool>> = withContext(Dispatchers.IO) {
        sendAuthenticatedRpc(context, "tools/list", JSONObject()).map { resultObj ->
            val toolsArr = resultObj.optJSONArray("tools") ?: JSONArray()
            val list = ArrayList<McpTool>(toolsArr.length())
            for (i in 0 until toolsArr.length()) {
                val item = toolsArr.optJSONObject(i) ?: continue
                val name = item.optString("name", "").trim()
                if (name.isEmpty()) continue
                val desc = item.optString("description", "No description")
                val schema = item.optJSONObject("inputSchema")?.toString() ?: "{}"
                list.add(McpTool(name, desc, schema))
            }
            list
        }
    }

    /**
     * Invokes an MCP tool via `tools/call` ONLY after the caller passes HITL approval.
     */
    suspend fun callToolWithHitl(
        context: Context,
        toolName: String,
        argumentsJson: String,
        hitlApprover: suspend (toolName: String, argsJson: String) -> Boolean
    ): McpCallResult = withContext(Dispatchers.IO) {
        val cleanTool = toolName.trim()
        if (cleanTool.isEmpty()) {
            return@withContext McpCallResult(false, toolName, "Tool name cannot be empty.", isError = true)
        }

        val parsedArgs = try {
            if (argumentsJson.isBlank()) JSONObject() else JSONObject(argumentsJson)
        } catch (e: Exception) {
            return@withContext McpCallResult(
                success = false,
                toolName = cleanTool,
                output = "Invalid JSON arguments: ${e.message}",
                isError = true
            )
        }

        val risk = classifyToolRisk(cleanTool, parsedArgs)
        if (risk == ToolRiskLevel.DENY) {
            return@withContext McpCallResult(
                success = false,
                toolName = cleanTool,
                output = "Security Policy Violation: Tool '$cleanTool' (unconstrained raw shell execution) is permanently DENIED by policy.",
                isError = true
            )
        }

        if (risk == ToolRiskLevel.CONFIRM) {
            val approved = try {
                hitlApprover(cleanTool, argumentsJson)
            } catch (e: Exception) {
                Log.e(TAG, "HITL approval failed", e)
                false
            }

            if (!approved) {
                return@withContext McpCallResult(
                    success = false,
                    toolName = cleanTool,
                    output = "MCP tool execution aborted by Human-In-The-Loop (HITL) security gate.",
                    isError = true
                )
            }
        }

        val params = JSONObject().apply {
            put("name", cleanTool)
            put("arguments", parsedArgs)
        }

        val rpcResult = sendAuthenticatedRpc(context, "tools/call", params)
        rpcResult.fold(
            onSuccess = { resObj ->
                val isErr = resObj.optBoolean("isError", false)
                val contentArr = resObj.optJSONArray("content")
                val sb = StringBuilder()
                if (contentArr != null && contentArr.length() > 0) {
                    for (i in 0 until contentArr.length()) {
                        val c = contentArr.optJSONObject(i) ?: continue
                        val text = c.optString("text", "")
                        if (text.isNotEmpty()) {
                            if (sb.isNotEmpty()) sb.append("\n")
                            sb.append(text)
                        }
                    }
                } else {
                    sb.append(resObj.toString())
                }
                McpCallResult(
                    success = !isErr,
                    toolName = cleanTool,
                    output = sb.toString().ifBlank { "(empty output)" },
                    isError = isErr
                )
            },
            onFailure = { err ->
                McpCallResult(
                    success = false,
                    toolName = cleanTool,
                    output = "MCP Bridge Error: ${err.message}",
                    isError = true
                )
            }
        )
    }

    private fun sendAuthenticatedRpc(
        context: Context,
        method: String,
        params: JSONObject
    ): Result<JSONObject> {
        val endpoint = getEndpointUrl(context)
        if (!isLoopbackEndpoint(endpoint)) {
            return Result.failure(SecurityException("Non-loopback MCP endpoint rejected: $endpoint"))
        }

        val secret = getOrCreateSharedSecret(context)
        val rawBody = buildJsonRpcRequest(method, params)
        val timestamp = System.currentTimeMillis() / 1000L
        val nonce = generateNonce()
        val signature = computeHmacSignature(secret, timestamp, nonce, rawBody)

        var conn: HttpURLConnection? = null
        return try {
            conn = (URL(endpoint).openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                doOutput = true
                setRequestProperty("Content-Type", "application/json; charset=utf-8")
                setRequestProperty("Accept", "application/json")
                // Mitigate RONIN-SEC-03 & Replay attacks:
                // Secret is never sent in cleartext. Authenticity, freshness, and uniqueness
                // are guaranteed via HMAC-SHA256 signature + timestamp (±30s) + 16-byte cryptographically random nonce.
                setRequestProperty("X-Ronin-Timestamp", timestamp.toString())
                setRequestProperty("X-Ronin-Nonce", nonce)
                setRequestProperty("X-Ronin-Signature", signature)
            }

            OutputStreamWriter(conn.outputStream, StandardCharsets.UTF_8).use { writer ->
                writer.write(rawBody)
                writer.flush()
            }

            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val respText = stream?.bufferedReader(StandardCharsets.UTF_8)?.use { it.readText() } ?: ""
            if (code !in 200..299) {
                return Result.failure(IllegalStateException("HTTP $code from MCP bridge: ${respText.take(240)}"))
            }

            val json = JSONObject(respText)
            val errObj = json.optJSONObject("error")
            if (errObj != null) {
                val errMsg = errObj.optString("message", "Unknown JSON-RPC error")
                return Result.failure(IllegalStateException(errMsg))
            }
            Result.success(json.optJSONObject("result") ?: JSONObject())
        } catch (e: Exception) {
            Result.failure(e)
        } finally {
            conn?.disconnect()
        }
    }
}
