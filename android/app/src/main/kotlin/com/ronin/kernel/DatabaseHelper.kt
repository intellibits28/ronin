package com.ronin.kernel

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import android.content.ContentValues

/**
 * Phase 2.1: SQLite Evolution (Cognitive Memory Model)
 * Handles the persistent 'memories' table with EN translation and BGE embeddings.
 */
class DatabaseHelper(context: Context) : SQLiteOpenHelper(context, DATABASE_NAME, null, DATABASE_VERSION) {

    companion object {
        private const val DATABASE_NAME = "ronin_cognitive.db"
        private const val DATABASE_VERSION = 1

        const val TABLE_MEMORIES = "memories"
        const val COLUMN_ID = "id"
        const val COLUMN_TEXT_MM = "original_text_mm"
        const val COLUMN_SEGMENTED_MM = "segmented_text_mm"
        const val COLUMN_TEXT_EN = "translated_text_en"
        const val COLUMN_EMBEDDING = "embedding_vector"
        const val COLUMN_IMPORTANCE = "importance_score"
        const val COLUMN_RECALL_COUNT = "recall_count"
        const val COLUMN_CREATION_TIME = "creation_time"
        const val COLUMN_LAST_ACCESSED = "last_accessed_time"
        const val COLUMN_STATE = "state_enum"
    }

    override fun onCreate(db: SQLiteDatabase?) {
        val createMemoriesTable = """
            CREATE TABLE $TABLE_MEMORIES (
                $COLUMN_ID INTEGER PRIMARY KEY AUTOINCREMENT,
                $COLUMN_TEXT_MM TEXT,
                $COLUMN_SEGMENTED_MM TEXT,
                $COLUMN_TEXT_EN TEXT,
                $COLUMN_EMBEDDING BLOB,
                $COLUMN_IMPORTANCE REAL,
                $COLUMN_RECALL_COUNT INTEGER DEFAULT 0,
                $COLUMN_CREATION_TIME INTEGER,
                $COLUMN_LAST_ACCESSED INTEGER,
                $COLUMN_STATE INTEGER
            )
        """.trimIndent()
        db?.execSQL(createMemoriesTable)
    }

    override fun onUpgrade(db: SQLiteDatabase?, oldVersion: Int, newVersion: Int) {
        // Upgrade logic for future versions
    }

    fun storeMemory(mm: String, en: String = "", vector: FloatArray?, importance: Float = 1.0f) {
        val db = writableDatabase
        val values = ContentValues().apply {
            put(COLUMN_TEXT_MM, mm)
            if (en.isNotEmpty()) put(COLUMN_TEXT_EN, en)
            put(COLUMN_IMPORTANCE, importance)
            put(COLUMN_CREATION_TIME, System.currentTimeMillis() / 1000)
            put(COLUMN_LAST_ACCESSED, System.currentTimeMillis() / 1000)
            put(COLUMN_STATE, 0) // ACTIVE

            vector?.let {
                // Phase 2.1: INT8 Quantization for Episodic Bulk
                // Maps [-1.0, 1.0] to [-128, 127] to save space (1 byte per dim)
                val quantized = ByteArray(it.size)
                for (i in it.indices) {
                    quantized[i] = (it[i].coerceIn(-1.0f, 1.0f) * 127f).toInt().toByte()
                }
                put(COLUMN_EMBEDDING, quantized)
            }
        }
        db.insert(TABLE_MEMORIES, null, values)
    }
}
