package com.ronin.kernel

import android.content.Context
import android.graphics.Bitmap
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import android.util.Log
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.nio.charset.StandardCharsets
import java.util.zip.Inflater
import java.util.zip.InflaterInputStream

/**
 * Phase 2: Document Intelligence Engine for Ronin Kernel.
 * Provides in-depth document reading, semantic chunking, keyword search,
 * and summarization prompt synthesis for Gemma 4 on-device inference.
 */
object DocumentIntelligence {
    private const val TAG = "Ronin_DocIntelligence"

    const val MAX_READ_BYTES = 512 * 1024 // 512 KB safe ceiling for on-device RAM
    const val MAX_SUMMARY_CHARS = 8000     // Fits comfortably in Gemma 4's 4k token window
    const val DEFAULT_CHUNK_SIZE = 2500
    const val DEFAULT_OVERLAP = 200

    data class ReadResult(
        val success: Boolean,
        val filePath: String,
        val fileName: String,
        val content: String = "",
        val lineCount: Int = 0,
        val charCount: Int = 0,
        val isTruncated: Boolean = false,
        val isPdf: Boolean = false,
        val pdfPageCount: Int = 0,
        val error: String? = null
    )

    data class SearchMatch(
        val lineNumber: Int,
        val lineContent: String,
        val snippet: String
    )

    data class SearchResult(
        val filePath: String,
        val query: String,
        val totalMatches: Int,
        val matches: List<SearchMatch>,
        val error: String? = null
    )

    /**
     * Resolves a file query to an actual File.
     * Accepts either an absolute path or a query string to look up via searchFiles.
     */
    fun resolveFile(query: String, searchFallback: ((String) -> Array<String>)? = null): File? {
        val trimmed = query.trim().trim('"', '\'')
        if (trimmed.isEmpty()) return null

        val directFile = File(trimmed)
        if (directFile.exists() && directFile.isFile) {
            return directFile
        }

        // Try lookup via search fallback if available
        if (searchFallback != null) {
            try {
                val results = searchFallback(trimmed)
                for (path in results) {
                    val candidate = File(path)
                    if (candidate.exists() && candidate.isFile) {
                        return candidate
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Search fallback failed for '$trimmed': ${e.message}")
            }
        }
        return null
    }

    /**
     * Reads text content from supported file formats (.txt, .md, .csv, .json, .pdf, code files).
     */
    fun readDocument(file: File, maxBytes: Int = MAX_READ_BYTES): ReadResult {
        if (!file.exists()) {
            return ReadResult(success = false, filePath = file.absolutePath, fileName = file.name, error = "File not found: ${file.path}")
        }
        if (!file.canRead()) {
            return ReadResult(success = false, filePath = file.absolutePath, fileName = file.name, error = "Permission denied: ${file.path}")
        }

        val lowerName = file.name.lowercase()
        return if (lowerName.endsWith(".pdf")) {
            readPdfDocument(file, maxBytes)
        } else {
            readTextDocument(file, maxBytes)
        }
    }

    /**
     * Reads plain text, markdown, csv, json, and source code files.
     */
    fun readTextDocument(file: File, maxBytes: Int = MAX_READ_BYTES): ReadResult {
        return try {
            val length = file.length()
            val isTruncated = length > maxBytes
            val bytesToRead = if (isTruncated) maxBytes else length.toInt()

            val bytes = ByteArray(bytesToRead)
            FileInputStream(file).use { fis ->
                var totalRead = 0
                while (totalRead < bytesToRead) {
                    val read = fis.read(bytes, totalRead, bytesToRead - totalRead)
                    if (read == -1) break
                    totalRead += read
                }
            }

            // UTF-8 decoding with fallback
            val text = String(bytes, 0, bytes.size, StandardCharsets.UTF_8)
            val lines = text.lines()

            ReadResult(
                success = true,
                filePath = file.absolutePath,
                fileName = file.name,
                content = text,
                lineCount = lines.size,
                charCount = text.length,
                isTruncated = isTruncated
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error reading text document: ${e.message}", e)
            ReadResult(
                success = false,
                filePath = file.absolutePath,
                fileName = file.name,
                error = e.message ?: "Unknown read error"
            )
        }
    }

    /**
     * Reads a PDF document: extracts page count and text streams.
     */
    fun readPdfDocument(file: File, maxBytes: Int = MAX_READ_BYTES): ReadResult {
        var pageCount = 0
        try {
            // Attempt Android PdfRenderer for page count
            val pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
            if (pfd != null) {
                PdfRenderer(pfd).use { renderer ->
                    pageCount = renderer.pageCount
                }
            }
        } catch (t: Throwable) {
            Log.d(TAG, "PdfRenderer unavailable (running in host/JVM or unsupported device): ${t.message}")
        }

        return try {
            val extractedText = extractTextFromPdfBytes(file, maxBytes)
            val isTruncated = extractedText.length >= maxBytes
            val finalContent = if (extractedText.isNotBlank()) {
                extractedText
            } else {
                "[PDF Document: ${file.name}]\nTotal Pages: $pageCount\nNotice: This PDF contains scanned images or encrypted streams. Direct text extraction yielded no selectable text."
            }

            ReadResult(
                success = true,
                filePath = file.absolutePath,
                fileName = file.name,
                content = finalContent,
                lineCount = finalContent.lines().size,
                charCount = finalContent.length,
                isTruncated = isTruncated,
                isPdf = true,
                pdfPageCount = pageCount
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error extracting PDF document: ${e.message}", e)
            ReadResult(
                success = false,
                filePath = file.absolutePath,
                fileName = file.name,
                isPdf = true,
                pdfPageCount = pageCount,
                error = "Failed to parse PDF: ${e.message}"
            )
        }
    }

    /**
     * Lightweight pure-Kotlin PDF stream text extractor.
     * Decompresses Flate streams and extracts strings from BT...ET operators.
     */
    private fun extractTextFromPdfBytes(file: File, maxBytes: Int): String {
        val bytes = file.readBytes()
        val textBuilder = StringBuilder()
        var offset = 0

        while (offset < bytes.size && textBuilder.length < maxBytes) {
            val streamStart = indexOf(bytes, "stream".toByteArray(StandardCharsets.US_ASCII), offset)
            if (streamStart == -1) break

            var dataStart = streamStart + 6
            if (dataStart < bytes.size && bytes[dataStart] == '\r'.code.toByte()) dataStart++
            if (dataStart < bytes.size && bytes[dataStart] == '\n'.code.toByte()) dataStart++

            val streamEnd = indexOf(bytes, "endstream".toByteArray(StandardCharsets.US_ASCII), dataStart)
            if (streamEnd == -1) break

            val streamLen = streamEnd - dataStart
            if (streamLen > 0) {
                val streamBytes = bytes.copyOfRange(dataStart, streamEnd)
                val decompressed = tryDecompressFlate(streamBytes) ?: streamBytes
                val streamText = extractTextFromPdfStream(decompressed)
                if (streamText.isNotBlank()) {
                    if (textBuilder.isNotEmpty()) textBuilder.append("\n")
                    textBuilder.append(streamText)
                }
            }
            offset = streamEnd + 9
        }

        return textBuilder.toString().trim()
    }

    private fun tryDecompressFlate(data: ByteArray): ByteArray? {
        return try {
            val inflater = Inflater(false)
            val bis = ByteArrayInputStream(data)
            val iis = InflaterInputStream(bis, inflater)
            val bos = ByteArrayOutputStream()
            val buf = ByteArray(1024)
            var n: Int
            while (iis.read(buf).also { n = it } != -1) {
                bos.write(buf, 0, n)
            }
            bos.toByteArray()
        } catch (_: Exception) {
            null
        }
    }

    private fun extractTextFromPdfStream(streamBytes: ByteArray): String {
        val streamStr = String(streamBytes, StandardCharsets.ISO_8859_1)
        val out = StringBuilder()
        
        // Find text enclosed in ( ... ) followed by Tj or TJ
        val tjRegex = Regex("""\((.*?)\)\s*(?:Tj|'|")""")
        val tjMatches = tjRegex.findAll(streamStr)
        for (match in tjMatches) {
            val raw = match.groupValues[1]
            val cleaned = unescapePdfString(raw)
            if (cleaned.isNotBlank()) {
                out.append(cleaned).append(" ")
            }
        }

        // Also handle array of strings: [ (Hello) -20 (World) ] TJ
        val arrayRegex = Regex("""\[(.*?)\]\s*TJ""")
        val arrayMatches = arrayRegex.findAll(streamStr)
        for (m in arrayMatches) {
            val inner = m.groupValues[1]
            val itemRegex = Regex("""\((.*?)\)""")
            for (sub in itemRegex.findAll(inner)) {
                val subClean = unescapePdfString(sub.groupValues[1])
                if (subClean.isNotBlank()) {
                    out.append(subClean).append(" ")
                }
            }
        }

        return out.toString().trim()
    }

    private fun unescapePdfString(str: String): String {
        return str
            .replace("\\(", "(")
            .replace("\\)", ")")
            .replace("\\\\", "\\")
            .replace("\\r", "\r")
            .replace("\\n", "\n")
            .replace("\\t", "\t")
    }

    private fun indexOf(source: ByteArray, target: ByteArray, fromIndex: Int = 0): Int {
        if (target.isEmpty()) return 0
        val max = source.size - target.size
        for (i in fromIndex..max) {
            var found = true
            for (j in target.indices) {
                if (source[i + j] != target[j]) {
                    found = false
                    break
                }
            }
            if (found) return i
        }
        return -1
    }

    /**
     * Performs keyword search inside a document, returning matched line numbers and surrounding snippets.
     */
    fun searchInDocument(file: File, query: String, maxResults: Int = 10): SearchResult {
        val readResult = readDocument(file)
        if (!readResult.success) {
            return SearchResult(file.absolutePath, query, 0, emptyList(), readResult.error)
        }

        val lines = readResult.content.lines()
        val matches = mutableListOf<SearchMatch>()
        val queryLower = query.lowercase().trim()

        for (i in lines.indices) {
            val line = lines[i]
            if (line.lowercase().contains(queryLower)) {
                val prev = if (i > 0) lines[i - 1].trim() else ""
                val next = if (i < lines.size - 1) lines[i + 1].trim() else ""
                val snippet = buildString {
                    if (prev.isNotEmpty()) append("  $prev\n")
                    append("▶ $line\n")
                    if (next.isNotEmpty()) append("  $next")
                }.trim()

                matches.add(SearchMatch(lineNumber = i + 1, lineContent = line.trim(), snippet = snippet))
                if (matches.size >= maxResults) break
            }
        }

        return SearchResult(
            filePath = file.absolutePath,
            query = query,
            totalMatches = matches.size,
            matches = matches
        )
    }

    /**
     * Splits text into manageable semantic chunks for inference or embedding.
     */
    fun chunkText(text: String, chunkSize: Int = DEFAULT_CHUNK_SIZE, overlap: Int = DEFAULT_OVERLAP): List<String> {
        if (text.length <= chunkSize) return listOf(text)

        val chunks = mutableListOf<String>()
        var start = 0

        while (start < text.length) {
            var end = (start + chunkSize).coerceAtMost(text.length)
            
            // Try to split on paragraph boundary or newline
            if (end < text.length) {
                val paragraphBreak = text.lastIndexOf("\n\n", end)
                if (paragraphBreak > start + (chunkSize / 2)) {
                    end = paragraphBreak + 2
                } else {
                    val lineBreak = text.lastIndexOf('\n', end)
                    if (lineBreak > start + (chunkSize / 2)) {
                        end = lineBreak + 1
                    }
                }
            }

            val chunk = text.substring(start, end).trim()
            if (chunk.isNotEmpty()) {
                chunks.add(chunk)
            }

            if (end >= text.length) break
            start = (end - overlap).coerceAtLeast(start + 1)
        }

        return chunks
    }

    /**
     * Builds an effective summarization prompt for Gemma 4 / Ronin LLM.
     */
    fun buildSummaryPrompt(fileName: String, content: String, langMyanmar: Boolean = true): String {
        // Cap content to MAX_SUMMARY_CHARS
        val safeContent = if (content.length > MAX_SUMMARY_CHARS) {
            content.take(MAX_SUMMARY_CHARS) + "\n\n[... Remaining content truncated for token limits ...]"
        } else {
            content
        }

        return if (langMyanmar) {
            """
            အောက်ပါ စာရွက်စာတမ်း ('$fileName') ၏ အကြောင်းအရာကို အခြေခံ၍ မြန်မာဘာသာဖြင့် တိကျရှင်းလင်းစွာ အကျဉ်းချုပ် (Executive Summary) ပြုလုပ်ပေးပါ။

            [အစီရင်ခံစာ ပုံစံ]:
            1. 📌 အကျဉ်းချုပ် (Core Summary)
            2. 🔍 အဓိက အချက်အလက်များ (Key Highlights & Findings)
            3. 💡 သုံးသပ်ချက် / အရေးကြီး မှတ်ချက် (Key Takeaways)

            [စာရွက်စာတမ်း အကြောင်းအရာ]:
            $safeContent
            """.trimIndent()
        } else {
            """
            Please provide a comprehensive and structured summary of the following document ('$fileName').

            [Format]:
            1. 📌 Executive Summary
            2. 🔍 Key Highlights & Findings
            3. 💡 Takeaways & Action Items

            [Document Content]:
            $safeContent
            """.trimIndent()
        }
    }
}
