package com.ronin.kernel

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.core.content.FileProvider
import java.io.File

/**
 * Phase 6.0: Future-Proofing (Action Hooks)
 * Specialized handler for File Search operations and Android Intent integration.
 */
object FileSearchNodeHooks {
    private const val TAG = "Ronin_FileSearchHooks"

    /**
     * Logic stub to open or edit files discovered by the Ronin Kernel.
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
                return
            }

            // Phase 6.1: Secure File Uri via FileProvider (Requirement: manifest authority must match)
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

            val chooser = Intent.createChooser(intent, "Open file with...")
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(chooser)
            
            Log.i(TAG, "Intent dispatched for file: $filePath (Mode: $mode)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to dispatch file intent: ${e.message}")
        }
    }

    /**
     * Minimalist MIME-type resolver based on whitelisted extensions.
     */
    private fun getMimeType(path: String): String {
        return when {
            path.endsWith(".md") -> "text/markdown"
            path.endsWith(".py") -> "text/x-python"
            path.endsWith(".json") -> "application/json"
            path.endsWith(".yml") || path.endsWith(".yaml") -> "application/x-yaml"
            path.endsWith(".zig") -> "text/plain"
            path.endsWith(".cpp") || path.endsWith(".h") -> "text/x-c++src"
            else -> "text/plain"
        }
    }
}
