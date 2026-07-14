package com.ronin.kernel

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import android.content.ContentValues

/**
 * Phase 11.0: SQLite Evolution (Lexical Memory Model)
 * Simplified to rely purely on FTS5 keyword matching via C++ layer.
 * Kotlin side handles basic metadata and auditing.
 */
class DatabaseHelper(context: Context) : SQLiteOpenHelper(context, DATABASE_NAME, null, DATABASE_VERSION) {

    companion object {
        private const val DATABASE_NAME = "ronin_cognitive.db"
        private const val DATABASE_VERSION = 3 // Incremented for SHM session table

        const val TABLE_MEMORIES = "memories"
        const val COLUMN_ID = "id"
        const val COLUMN_TEXT_MM = "original_text_mm"
        const val COLUMN_SEGMENTED_MM = "segmented_text_mm"
        const val COLUMN_IMPORTANCE = "importance_score"
        const val COLUMN_RECALL_COUNT = "recall_count"
        const val COLUMN_CREATION_TIME = "creation_time"
        const val COLUMN_LAST_ACCESSED = "last_accessed_time"
        const val COLUMN_STATE = "state_enum"

        const val TABLE_SHM_SESSIONS = "shm_sessions"
        const val COLUMN_SESSION_ID = "session_id"
        const val COLUMN_SESSION_TIMESTAMP = "session_timestamp"
        const val COLUMN_SESSION_JSON = "session_json"
    }

    override fun onCreate(db: SQLiteDatabase?) {
        val createMemoriesTable = """
            CREATE TABLE $TABLE_MEMORIES (
                $COLUMN_ID INTEGER PRIMARY KEY AUTOINCREMENT,
                $COLUMN_TEXT_MM TEXT,
                $COLUMN_SEGMENTED_MM TEXT,
                $COLUMN_IMPORTANCE REAL,
                $COLUMN_RECALL_COUNT INTEGER DEFAULT 0,
                $COLUMN_CREATION_TIME INTEGER,
                $COLUMN_LAST_ACCESSED INTEGER,
                $COLUMN_STATE INTEGER
            )
        """.trimIndent()
        db?.execSQL(createMemoriesTable)
        createPerceptionTable(db)
        createShmSessionsTable(db)
    }

    private fun createPerceptionTable(db: SQLiteDatabase?) {
        val createPerceptionHistoryTable = """
            CREATE TABLE IF NOT EXISTS perception_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp INTEGER,
                state_type TEXT,
                state_value TEXT
            )
        """.trimIndent()
        db?.execSQL(createPerceptionHistoryTable)
    }

    private fun createShmSessionsTable(db: SQLiteDatabase?) {
        val createShmTable = """
            CREATE TABLE IF NOT EXISTS $TABLE_SHM_SESSIONS (
                $COLUMN_SESSION_ID TEXT PRIMARY KEY,
                $COLUMN_SESSION_TIMESTAMP INTEGER,
                $COLUMN_SESSION_JSON TEXT
            )
        """.trimIndent()
        db?.execSQL(createShmTable)
    }

    override fun onUpgrade(db: SQLiteDatabase?, oldVersion: Int, newVersion: Int) {
        if (db == null) return
        createPerceptionTable(db)
        createShmSessionsTable(db)
        if (oldVersion < 2) {
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_SEGMENTED_MM, "TEXT")
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_IMPORTANCE, "REAL DEFAULT 1.0")
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_RECALL_COUNT, "INTEGER DEFAULT 0")
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_CREATION_TIME, "INTEGER")
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_LAST_ACCESSED, "INTEGER")
            addColumnIfMissing(db, TABLE_MEMORIES, COLUMN_STATE, "INTEGER DEFAULT 0")
        }
    }

    private fun addColumnIfMissing(db: SQLiteDatabase, table: String, column: String, definition: String) {
        db.rawQuery("PRAGMA table_info($table)", null).use { cursor ->
            while (cursor.moveToNext()) {
                if (cursor.getString(1) == column) return
            }
        }
        db.execSQL("ALTER TABLE $table ADD COLUMN $column $definition")
    }

    fun storeMemory(mm: String, segmented: String = "", importance: Float = 1.0f) {
        val db = writableDatabase
        val values = ContentValues().apply {
            put(COLUMN_TEXT_MM, mm)
            put(COLUMN_SEGMENTED_MM, segmented)
            put(COLUMN_IMPORTANCE, importance)
            put(COLUMN_CREATION_TIME, System.currentTimeMillis() / 1000)
            put(COLUMN_LAST_ACCESSED, System.currentTimeMillis() / 1000)
            put(COLUMN_STATE, 0) // ACTIVE
        }
        db.insert(TABLE_MEMORIES, null, values)
    }

    // Phase 11.2: SHM Session Storage Layer
    fun storeShmSession(sessionId: String, timestamp: Long, jsonStr: String) {
        val db = writableDatabase
        val values = ContentValues().apply {
            put(COLUMN_SESSION_ID, sessionId)
            put(COLUMN_SESSION_TIMESTAMP, timestamp)
            put(COLUMN_SESSION_JSON, jsonStr)
        }
        db.insertWithOnConflict(TABLE_SHM_SESSIONS, null, values, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun getShmSessionJson(sessionId: String): String? {
        val db = readableDatabase
        db.query(TABLE_SHM_SESSIONS, arrayOf(COLUMN_SESSION_JSON), "$COLUMN_SESSION_ID = ?", arrayOf(sessionId), null, null, null).use { cursor ->
            if (cursor.moveToFirst()) {
                return cursor.getString(0)
            }
        }
        return null
    }

    fun getAllShmSessionsJson(): List<String> {
        val list = mutableListOf<String>()
        val db = readableDatabase
        db.query(TABLE_SHM_SESSIONS, arrayOf(COLUMN_SESSION_JSON), null, null, null, null, "$COLUMN_SESSION_TIMESTAMP DESC").use { cursor ->
            while (cursor.moveToNext()) {
                list.add(cursor.getString(0))
            }
        }
        return list
    }
}

