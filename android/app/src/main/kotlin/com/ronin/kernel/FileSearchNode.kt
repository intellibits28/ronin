package com.ronin.kernel

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import android.widget.Toast
import androidx.core.content.FileProvider
import java.io.File

/**
 * Phase 6.0: Future-Proofing (Action Hooks)
 * Specialized handler for File Search operations and Android Intent integration.
 */
object FileSearchNodeHooks {
    private const val TAG = "Ronin_FileSearchHooks"

    /**
     * Logic to open or edit files discovered by the Ronin Kernel.
     * Uses standard Android Intents to hand off files to specialized editors.
     * 
     * @param context The application context
     * @param filePath The absolute path to the file on storage
     * @param mode Intent mode: "view" or "edit"
     */
    fun performFileAction(context: Context, filePath: String, mode: String = "view") {
        try {
            val file = File(filePath)
            if (!file.exists()) {
                Log.e(TAG, "Cannot perform action: File does not exist at $filePath")
                Toast.makeText(context, "File does not exist: ${file.name}", Toast.LENGTH_SHORT).show()
                return
            }

            // Secure File Uri via FileProvider
            val uri: Uri = FileProvider.getUriForFile(
                context,
                "${context.packageName}.fileprovider",
                file
            )

            val intentAction = if (mode == "edit") Intent.ACTION_EDIT else Intent.ACTION_VIEW
            val intent = Intent(intentAction).apply {
                setDataAndType(uri, getMimeType(filePath))
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                if (mode == "edit") {
                    addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                }
            }

            val chooser = Intent.createChooser(intent, "Open with...")
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(chooser)
            
            Log.i(TAG, "Intent dispatched for file: $filePath (Mode: $mode)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to dispatch file intent: ${e.message}")
            Toast.makeText(context, "Cannot open file: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    /**
     * Shares or attaches a file to Email or other apps via Android ShareSheet.
     *
     * @param context The application context
     * @param filePath The absolute path to the file
     */
    fun shareFile(context: Context, filePath: String) {
        try {
            val file = File(filePath)
            if (!file.exists()) {
                Toast.makeText(context, "File does not exist: ${file.name}", Toast.LENGTH_SHORT).show()
                return
            }

            val uri: Uri = FileProvider.getUriForFile(
                context,
                "${context.packageName}.fileprovider",
                file
            )

            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                type = getMimeType(filePath)
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, file.name)
                putExtra(Intent.EXTRA_TEXT, "Sharing ${file.name} via Ronin Kernel.")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }

            val chooser = Intent.createChooser(shareIntent, "Share or Email file via...")
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(chooser)

            Log.i(TAG, "Share Intent dispatched for file: $filePath")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to share file: ${e.message}")
            Toast.makeText(context, "Cannot share file: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    /**
     * Formats file size into human-readable B, KB, MB, GB.
     */
    fun getFileSizeFormatted(file: File): String {
        return try {
            val bytes = file.length()
            when {
                bytes < 1024 -> "$bytes B"
                bytes < 1024 * 1024 -> String.format("%.1f KB", bytes / 1024.0)
                bytes < 1024 * 1024 * 1024 -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
                else -> String.format("%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0))
            }
        } catch (_: Exception) {
            "--"
        }
    }

    /**
     * Resolves an expressive emoji icon based on file extension.
     */
    fun getFileIcon(path: String): String {
        val lower = path.lowercase()
        return when {
            lower.endsWith(".pdf") -> "📕"
            lower.endsWith(".md") || lower.endsWith(".markdown") -> "📝"
            lower.endsWith(".txt") || lower.endsWith(".rtf") -> "📄"
            lower.endsWith(".doc") || lower.endsWith(".docx") -> "📘"
            lower.endsWith(".xls") || lower.endsWith(".xlsx") || lower.endsWith(".csv") -> "📊"
            lower.endsWith(".ppt") || lower.endsWith(".pptx") -> "📙"
            lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") || lower.endsWith(".webp") -> "🖼️"
            lower.endsWith(".py") || lower.endsWith(".cpp") || lower.endsWith(".h") || lower.endsWith(".kt") || lower.endsWith(".java") || lower.endsWith(".js") || lower.endsWith(".ts") || lower.endsWith(".html") || lower.endsWith(".css") || lower.endsWith(".json") || lower.endsWith(".xml") -> "💻"
            lower.endsWith(".zip") || lower.endsWith(".tar") || lower.endsWith(".gz") || lower.endsWith(".rar") || lower.endsWith(".7z") -> "📦"
            lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".ogg") || lower.endsWith(".m4a") -> "🎵"
            lower.endsWith(".mp4") || lower.endsWith(".mkv") || lower.endsWith(".webm") -> "🎬"
            lower.endsWith(".bin") || lower.endsWith(".dat") -> "⚙️"
            lower.endsWith(".apk") -> "🤖"
            else -> "📁"
        }
    }

    /**
     * MIME-type resolver based on file extensions.
     */
    fun getMimeType(path: String): String {
        val lower = path.lowercase()
        return when {
            lower.endsWith(".pdf") -> "application/pdf"
            lower.endsWith(".md") -> "text/markdown"
            lower.endsWith(".txt") -> "text/plain"
            lower.endsWith(".py") -> "text/x-python"
            lower.endsWith(".json") -> "application/json"
            lower.endsWith(".csv") -> "text/csv"
            lower.endsWith(".yml") || lower.endsWith(".yaml") -> "application/x-yaml"
            lower.endsWith(".zig") -> "text/plain"
            lower.endsWith(".cpp") || lower.endsWith(".h") -> "text/x-c++src"
            lower.endsWith(".kt") -> "text/x-kotlin"
            lower.endsWith(".java") -> "text/x-java-source"
            lower.endsWith(".jpg") || lower.endsWith(".jpeg") -> "image/jpeg"
            lower.endsWith(".png") -> "image/png"
            lower.endsWith(".webp") -> "image/webp"
            lower.endsWith(".gif") -> "image/gif"
            lower.endsWith(".doc") -> "application/msword"
            lower.endsWith(".docx") -> "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
            lower.endsWith(".xls") -> "application/vnd.ms-excel"
            lower.endsWith(".xlsx") -> "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
            lower.endsWith(".zip") -> "application/zip"
            lower.endsWith(".bin") -> "application/octet-stream"
            lower.endsWith(".mp3") -> "audio/mpeg"
            lower.endsWith(".wav") -> "audio/wav"
            lower.endsWith(".mp4") -> "video/mp4"
            else -> "*/*"
        }
    }
}
