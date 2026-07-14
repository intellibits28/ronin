package com.ronin.kernel.shm

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import java.io.File

enum class ExportFormat {
    ENGINEERING_JSON,
    HUMAN_REPORT,
    DEVELOPER_DEBUG
}

/**
 * Requirement 2 & 5: Export Manager & Android Share Sheet Implementation
 * Generates structured files (.json, .txt, .log) and shares via FileProvider Content URI
 * to avoid clipboard truncation and JSON structure destruction.
 */
object ShmExportManager {

    fun exportToFile(
        context: Context,
        session: ShmSession,
        format: ExportFormat,
        options: ExportOptions = ExportOptions()
    ): File {
        try {
            val exportDir = File(context.filesDir, "shm_exports").apply {
                if (!exists()) mkdirs()
            }

            val fileName = when (format) {
                ExportFormat.ENGINEERING_JSON -> "ronin_shm_session_${session.timestamp}.json"
                ExportFormat.HUMAN_REPORT -> "ronin_shm_report_${session.timestamp}.txt"
                ExportFormat.DEVELOPER_DEBUG -> "ronin_shm_debug_${session.timestamp}.log"
            }

            val content = when (format) {
                ExportFormat.ENGINEERING_JSON -> session.toEngineeringJson(options)
                ExportFormat.HUMAN_REPORT -> session.toHumanReport(options)
                ExportFormat.DEVELOPER_DEBUG -> session.toDeveloperDebugLog()
            }

            val targetFile = File(exportDir, fileName)
            targetFile.writeText(content)
            return targetFile
        } catch (e: Exception) {
            throw ShmError.ExportError("Export generation failed for $format: ${e.message}", e)
        }
    }

    fun shareViaAndroidShareSheet(context: Context, file: File) {
        try {
            val authority = "${context.packageName}.fileprovider"
            val uri: Uri = FileProvider.getUriForFile(context, authority, file)

            val mimeType = when {
                file.name.endsWith(".json") -> "application/json"
                file.name.endsWith(".log") -> "text/plain"
                else -> "text/plain"
            }

            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                type = mimeType
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, "Ronin SHM Analysis Export (${file.name})")
                putExtra(Intent.EXTRA_TEXT, "Attached is the Ronin Structural Health Monitoring analysis export: ${file.name}")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }

            val chooser = Intent.createChooser(shareIntent, "Share Ronin SHM Analysis: ${file.name}").apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(chooser)
        } catch (e: Exception) {
            throw ShmError.ExportError("Android Share Sheet launch failed for '${file.name}': ${e.message}", e)
        }
    }
}
