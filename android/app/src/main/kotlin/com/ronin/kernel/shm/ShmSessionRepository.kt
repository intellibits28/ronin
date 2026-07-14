package com.ronin.kernel.shm

import com.ronin.kernel.DatabaseHelper

/**
 * Requirement 3: Storage Architecture
 * Dedicated SHM session storage layer wrapping DatabaseHelper SQLite table.
 * Ensures sessions are retrievable anytime without data loss when UI closes.
 */
class ShmSessionRepository(private val dbHelper: DatabaseHelper) {

    @Synchronized
    fun saveSession(session: ShmSession) {
        try {
            dbHelper.storeShmSession(session.sessionId, session.timestamp, session.toJson().toString())
        } catch (e: Exception) {
            throw ShmError.StorageError("Failed to save SHM session to SQLite repository: ${e.message}", e)
        }
    }

    @Synchronized
    fun getSession(sessionId: String): ShmSession? {
        return try {
            val jsonStr = dbHelper.getShmSessionJson(sessionId) ?: return null
            ShmSession.fromJson(jsonStr)
        } catch (e: Exception) {
            throw ShmError.StorageError("Failed to retrieve SHM session '$sessionId': ${e.message}", e)
        }
    }

    @Synchronized
    fun getAllSessions(): List<ShmSession> {
        return try {
            dbHelper.getAllShmSessionsJson().mapNotNull { jsonStr ->
                try {
                    ShmSession.fromJson(jsonStr)
                } catch (_: Exception) {
                    null
                }
            }
        } catch (e: Exception) {
            throw ShmError.StorageError("Failed to list SHM sessions from repository: ${e.message}", e)
        }
    }

    @Synchronized
    fun deleteSession(sessionId: String): Boolean {
        return try {
            val db = dbHelper.writableDatabase
            val rows = db.delete(DatabaseHelper.TABLE_SHM_SESSIONS, "${DatabaseHelper.COLUMN_SESSION_ID} = ?", arrayOf(sessionId))
            rows > 0
        } catch (e: Exception) {
            throw ShmError.StorageError("Failed to delete SHM session '$sessionId': ${e.message}", e)
        }
    }
}
