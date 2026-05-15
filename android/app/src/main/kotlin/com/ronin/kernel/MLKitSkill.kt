package com.ronin.kernel

import android.util.Log
import com.google.mlkit.common.model.DownloadConditions
import com.google.mlkit.common.model.RemoteModelManager
import com.google.mlkit.nl.translate.TranslateLanguage
import com.google.mlkit.nl.translate.Translation
import com.google.mlkit.nl.translate.TranslatorOptions
import kotlinx.coroutines.tasks.await
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Phase 2.1: Burmese-English Bridge (ML Kit Translate)
 * Handles local translation for semantic memory normalization.
 */
class MLKitSkill {
    private val TAG = "Ronin_MLKit"
    
    // Burmese to English options
    // Note: Using "my" directly as BURMESE might be missing in some SDK versions
    private val options = TranslatorOptions.Builder()
        .setSourceLanguage("my")
        .setTargetLanguage(TranslateLanguage.ENGLISH)
        .build()
        
    private val translator = Translation.getClient(options)
    private var isModelDownloaded = false

    suspend fun ensureModelDownloaded(): Boolean {
        if (isModelDownloaded) return true
        
        return try {
            val conditions = DownloadConditions.Builder()
                .requireWifi()
                .build()
            
            Log.i(TAG, "Downloading Burmese-English translation models...")
            translator.downloadModelIfNeeded(conditions).await()
            isModelDownloaded = true
            Log.i(TAG, "Translation models ready.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Model download failed: ${e.message}")
            false
        }
    }

    suspend fun translate(text: String): String? {
        if (!ensureModelDownloaded()) return null
        
        return try {
            translator.translate(text).await()
        } catch (e: Exception) {
            Log.e(TAG, "Translation failed: ${e.message}")
            null
        }
    }
    
    fun close() {
        translator.close()
    }
}
