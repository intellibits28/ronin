package com.ronin.kernel

import java.io.File
import java.io.InputStream
import java.security.MessageDigest

/**
 * Tier 4: Storage Hygiene & Duplicate File Sweeper.
 * Identifies duplicate files (via size matching + SHA-256 hashing) and stale/large files.
 */
object StorageHygiene {

    data class DuplicateGroup(
        val sha256: String,
        val fileSizeBytes: Long,
        val files: List<File>
    ) {
        val wastedBytes: Long get() = if (files.size > 1) fileSizeBytes * (files.size - 1) else 0L
    }

    data class HygieneReport(
        val totalFilesScanned: Int,
        val totalBytesScanned: Long,
        val duplicateGroups: List<DuplicateGroup>,
        val totalWastedBytes: Long,
        val largeFiles: List<File>
    )

    fun computeSha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(16384)
        file.inputStream().use { input ->
            var bytesRead: Int
            while (input.read(buffer).also { bytesRead = it } != -1) {
                digest.update(buffer, 0, bytesRead)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    /**
     * Scans specified directory list for duplicate files and files exceeding large threshold.
     */
    fun scanDirectories(
        directories: List<File>,
        largeFileThresholdBytes: Long = 50L * 1024L * 1024L // 50 MB
    ): HygieneReport {
        val allFiles = mutableListOf<File>()

        for (dir in directories) {
            if (dir.exists() && dir.isDirectory) {
                dir.walkTopDown()
                    .maxDepth(4)
                    .filter { it.isFile && !it.name.startsWith(".") && it.length() > 0 }
                    .forEach { allFiles.add(it) }
            }
        }

        var totalBytes = 0L
        val largeFiles = mutableListOf<File>()
        for (f in allFiles) {
            val len = f.length()
            totalBytes += len
            if (len >= largeFileThresholdBytes) {
                largeFiles.add(f)
            }
        }

        // Fast Step 1: Group by file size
        val sizeBuckets = allFiles.groupBy { it.length() }.filter { it.value.size > 1 }

        // Step 2: Compute SHA-256 for size collisions only
        val duplicateGroups = mutableListOf<DuplicateGroup>()
        for ((size, files) in sizeBuckets) {
            val hashGroups = files.groupBy { computeSha256(it) }
            for ((hash, matchingFiles) in hashGroups) {
                if (matchingFiles.size > 1) {
                    duplicateGroups.add(
                        DuplicateGroup(
                            sha256 = hash,
                            fileSizeBytes = size,
                            files = matchingFiles
                        )
                    )
                }
            }
        }

        val totalWasted = duplicateGroups.sumOf { it.wastedBytes }

        return HygieneReport(
            totalFilesScanned = allFiles.size,
            totalBytesScanned = totalBytes,
            duplicateGroups = duplicateGroups,
            totalWastedBytes = totalWasted,
            largeFiles = largeFiles
        )
    }

    fun formatBytes(bytes: Long): String {
        return when {
            bytes >= 1024 * 1024 * 1024 -> "%.2f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
            bytes >= 1024 * 1024 -> "%.1f MB".format(bytes / (1024.0 * 1024.0))
            bytes >= 1024 -> "%.1f KB".format(bytes / 1024.0)
            else -> "$bytes B"
        }
    }
}
