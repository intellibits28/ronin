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
    const val MAX_SUMMARY_CHARS = 4500     // Default upper bound for Latin/English text
    const val MYANMAR_MAX_SUMMARY_CHARS = 1800 // Safe upper bound for Myanmar text (~1,800 tokens in Gemma)
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
        val needsOcrFallback: Boolean = false,
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
            val hasReadableText = extractedText.count { !it.isWhitespace() } >= 20
            val finalContent = if (hasReadableText) {
                extractedText
            } else {
                "[PDF Document: ${file.name}]\nTotal Pages: $pageCount\nNotice: This PDF contains scanned images or custom-encoded streams. Direct text extraction yielded no selectable text."
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
                pdfPageCount = pageCount,
                needsOcrFallback = !hasReadableText
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error extracting PDF document: ${e.message}", e)
            ReadResult(
                success = false,
                filePath = file.absolutePath,
                fileName = file.name,
                isPdf = true,
                pdfPageCount = pageCount,
                needsOcrFallback = true,
                error = "Failed to parse PDF: ${e.message}"
            )
        }
    }

    /**
     * Pure-Kotlin PDF text & Unicode CMap extractor.
     * Pass 1: Collects all decompressed streams and parses `ToUnicode` CMap tables (`beginbfchar` / `beginbfrange`)
     *         so CID/Type0 Myanmar & multilingual fonts are mapped back to Unicode code points.
     * Pass 2: Extracts both literal `(...)` strings and hex `<...>` strings from `Tj` and `TJ` operators.
     */
    internal fun extractTextFromPdfBytes(file: File, maxBytes: Int): String {
        val bytes = file.readBytes()
        val decompressedStreams = mutableListOf<ByteArray>()
        val cmapTable = HashMap<Int, String>()
        var offset = 0

        while (offset < bytes.size) {
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
                val asciiPreview = String(decompressed, StandardCharsets.ISO_8859_1)
                if (asciiPreview.contains("beginbfchar") || asciiPreview.contains("beginbfrange")) {
                    parseToUnicodeCmap(asciiPreview, cmapTable)
                } else {
                    decompressedStreams.add(decompressed)
                }
            }
            offset = streamEnd + 9
        }

        val textBuilder = StringBuilder()
        for (stream in decompressedStreams) {
            if (textBuilder.length >= maxBytes) break
            val streamText = extractTextFromPdfStream(stream, cmapTable)
            if (streamText.isNotBlank()) {
                if (textBuilder.isNotEmpty()) textBuilder.append("\n")
                textBuilder.append(streamText)
            }
        }

        return textBuilder.toString().trim()
    }

    private fun parseToUnicodeCmap(cmapText: String, cmapTable: MutableMap<Int, String>) {
        // 1. Parse beginbfchar ... endbfchar: <srcHex> <dstUtf16Hex>
        val bfCharBlockRegex = Regex("""beginbfchar(.*?)endbfchar""", RegexOption.DOT_MATCHES_ALL)
        val pairRegex = Regex("""<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>""")
        for (block in bfCharBlockRegex.findAll(cmapText)) {
            for (pair in pairRegex.findAll(block.groupValues[1])) {
                val srcCode = pair.groupValues[1].toIntOrNull(16) ?: continue
                val targetStr = decodeUtf16BeHex(pair.groupValues[2])
                if (targetStr.isNotEmpty()) {
                    cmapTable[srcCode] = targetStr
                }
            }
        }

        // 2. Parse beginbfrange ... endbfrange: <startHex> <endHex> <dstStartHex>
        val bfRangeBlockRegex = Regex("""beginbfrange(.*?)endbfrange""", RegexOption.DOT_MATCHES_ALL)
        val rangeRegex = Regex("""<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>""")
        for (block in bfRangeBlockRegex.findAll(cmapText)) {
            for (rng in rangeRegex.findAll(block.groupValues[1])) {
                val startCode = rng.groupValues[1].toIntOrNull(16) ?: continue
                val endCode = rng.groupValues[2].toIntOrNull(16) ?: continue
                val dstBase = rng.groupValues[3].toIntOrNull(16) ?: continue
                if (endCode >= startCode && (endCode - startCode) <= 1024) {
                    for (code in startCode..endCode) {
                        val mappedCp = dstBase + (code - startCode)
                        if (mappedCp in 1..0x10FFFF) {
                            cmapTable[code] = String(Character.toChars(mappedCp))
                        }
                    }
                }
            }
        }
    }

    private fun decodeUtf16BeHex(hex: String): String {
        val clean = hex.trim()
        if (clean.isEmpty()) return ""
        val padded = if (clean.length % 2 != 0) "0$clean" else clean
        val bytes = ByteArray(padded.length / 2)
        for (i in bytes.indices) {
            bytes[i] = padded.substring(i * 2, i * 2 + 2).toIntOrNull(16)?.toByte() ?: 0
        }
        return if (bytes.size >= 2 && bytes.size % 2 == 0) {
            String(bytes, StandardCharsets.UTF_16BE)
        } else {
            String(bytes, StandardCharsets.ISO_8859_1)
        }
    }

    private fun decodePdfHexToken(hex: String, cmapTable: Map<Int, String>): String {
        val clean = hex.replace(Regex("\\s+"), "")
        if (clean.isEmpty()) return ""
        if (clean.startsWith("FEFF", ignoreCase = true) || clean.startsWith("FFFE", ignoreCase = true)) {
            return decodeUtf16BeHex(clean.substring(4))
        }
        if (cmapTable.isNotEmpty() && clean.length % 4 == 0) {
            val sb = StringBuilder()
            var mappedCount = 0
            for (i in 0 until clean.length step 4) {
                val cid = clean.substring(i, i + 4).toIntOrNull(16) ?: continue
                val mapped = cmapTable[cid]
                if (mapped != null) {
                    sb.append(mapped)
                    mappedCount++
                } else if (cid in 0x20..0x7E || cid in 0x1000..0x109F) {
                    sb.append(cid.toChar())
                }
            }
            if (mappedCount > 0 || sb.isNotEmpty()) return sb.toString()
        }
        if (cmapTable.isNotEmpty() && clean.length % 2 == 0) {
            val sb = StringBuilder()
            var mappedCount = 0
            for (i in 0 until clean.length step 2) {
                val cid = clean.substring(i, i + 2).toIntOrNull(16) ?: continue
                val mapped = cmapTable[cid]
                if (mapped != null) {
                    sb.append(mapped)
                    mappedCount++
                }
            }
            if (mappedCount > 0) return sb.toString()
        }
        // Fallback: check if 4-hex chunks look like UTF-16BE (e.g., Myanmar 1000..109F or ASCII 0020..007E)
        if (clean.length % 4 == 0) {
            val utf16Candidate = decodeUtf16BeHex(clean)
            if (utf16Candidate.any { it in '\u1000'..'\u109F' || it.isLetterOrDigit() }) {
                return utf16Candidate
            }
        }
        return ""
    }

    private fun tryDecompressFlate(data: ByteArray): ByteArray? {
        for (nowrap in booleanArrayOf(false, true)) {
            try {
                val inflater = Inflater(nowrap)
                val bis = ByteArrayInputStream(data)
                val iis = InflaterInputStream(bis, inflater)
                val bos = ByteArrayOutputStream()
                val buf = ByteArray(2048)
                var n: Int
                while (iis.read(buf).also { n = it } != -1) {
                    bos.write(buf, 0, n)
                }
                val res = bos.toByteArray()
                if (res.isNotEmpty()) return res
            } catch (_: Exception) {}
        }
        return null
    }

    private fun extractTextFromPdfStream(streamBytes: ByteArray, cmapTable: Map<Int, String> = emptyMap()): String {
        val streamStr = String(streamBytes, StandardCharsets.ISO_8859_1)
        val out = StringBuilder()

        // Match (...) Tj, <...> Tj, or [...] TJ in document order
        val operatorRegex = Regex("""(?:\(((?:\\.|[^\\()])*)\)|<([0-9A-Fa-f\s]+)>)\s*(?:Tj|'|")|\[(.*?)\]\s*TJ""", RegexOption.DOT_MATCHES_ALL)
        val subTokenRegex = Regex("""\(((?:\\.|[^\\()])*)\)|<([0-9A-Fa-f\s]+)>""")

        for (match in operatorRegex.findAll(streamStr)) {
            val literalGroup = match.groups[1]?.value
            val hexGroup = match.groups[2]?.value
            val arrayGroup = match.groups[3]?.value

            when {
                literalGroup != null -> {
                    val cleaned = unescapePdfString(literalGroup, cmapTable)
                    if (cleaned.isNotBlank()) out.append(cleaned).append(" ")
                }
                hexGroup != null -> {
                    val decoded = decodePdfHexToken(hexGroup, cmapTable)
                    if (decoded.isNotBlank()) out.append(decoded).append(" ")
                }
                arrayGroup != null -> {
                    val lineSb = StringBuilder()
                    for (sub in subTokenRegex.findAll(arrayGroup)) {
                        val subLit = sub.groups[1]?.value
                        val subHex = sub.groups[2]?.value
                        if (subLit != null) {
                            lineSb.append(unescapePdfString(subLit, cmapTable))
                        } else if (subHex != null) {
                            lineSb.append(decodePdfHexToken(subHex, cmapTable))
                        }
                    }
                    if (lineSb.isNotBlank()) out.append(lineSb).append(" ")
                }
            }
        }

        return out.toString().replace(Regex("[ \\t]+"), " ").trim()
    }

    private fun unescapePdfString(str: String, cmapTable: Map<Int, String> = emptyMap()): String {
        val unescaped = str
            .replace("\\(", "(")
            .replace("\\)", ")")
            .replace("\\\\", "\\")
            .replace("\\r", "\r")
            .replace("\\n", "\n")
            .replace("\\t", "\t")
        if (unescaped.length >= 2 && unescaped[0] == '\u00FE' && unescaped[1] == '\u00FF') {
            val rawBytes = unescaped.substring(2).toByteArray(StandardCharsets.ISO_8859_1)
            return String(rawBytes, StandardCharsets.UTF_16BE)
        }
        if (cmapTable.isNotEmpty()) {
            val sb = StringBuilder()
            var mappedAny = false
            for (ch in unescaped) {
                val mapped = cmapTable[ch.code]
                if (mapped != null) {
                    sb.append(mapped)
                    mappedAny = true
                } else {
                    sb.append(ch)
                }
            }
            if (mappedAny) return sb.toString()
        }
        return unescaped
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
     * Checks if a text snippet contains Myanmar Unicode characters.
     */
    fun isMyanmarScript(text: String): Boolean {
        var myanmarChars = 0
        val sampleLen = text.length.coerceAtMost(1000)
        for (i in 0 until sampleLen) {
            val ch = text[i]
            if (ch in '\u1000'..'\u109F' || ch in '\uAA60'..'\uAA7F' || ch in '\uA9E0'..'\uA9FF') {
                myanmarChars++
                if (myanmarChars >= 3) return true
            }
        }
        return false
    }

    /**
     * Determines the maximum safe character budget for on-device Gemma 4 / LiteRT-LM.
     * In SentencePiece tokenization, Myanmar Unicode characters average ~0.9 to 1.2 tokens per character.
     * To strictly stay below LiteRT's 4,000-token context limit and leave ~1,500 tokens for output generation,
     * Myanmar documents must be budgeted at ~1,800 characters (~1,800 tokens).
     * English/Latin documents average ~4 characters per token, allowing ~4,500 characters.
     */
    fun calculateSafeSummaryCharLimit(content: String): Int {
        return if (isMyanmarScript(content)) MYANMAR_MAX_SUMMARY_CHARS else MAX_SUMMARY_CHARS
    }

    /**
     * Builds an effective summarization prompt for Gemma 4 / Ronin LLM.
     */
    fun buildSummaryPrompt(fileName: String, content: String, langMyanmar: Boolean = true): String {
        val safeLimit = calculateSafeSummaryCharLimit(content)
        val safeContent = if (content.length > safeLimit) {
            selectHighCentralityChunks(content, targetChars = safeLimit)
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

    /**
     * Phase 2.3: RAG Centrality-Based Chunk Selector for Long Documents.
     * Instead of blindly truncating or blowing up LiteRT token contexts, embeds all chunks into 768D space,
     * computes document centroid vector, and selects the top most representative chunks
     * preserving chronological order across the beginning, middle, and conclusion.
     */
    fun computeFileSha256(file: File): String {
        return try {
            val md = java.security.MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buf = ByteArray(64 * 1024)
                var read: Int
                while (input.read(buf).also { read = it } != -1) {
                    md.update(buf, 0, read)
                }
            }
            md.digest().joinToString("") { "%02x".format(it) }
        } catch (_: Exception) {
            "${file.length()}_${file.lastModified()}"
        }
    }

    /**
     * Phase 2.2: Semantic Document Summarization.
     * When neural embeddings are available, extracts high-centrality chunks.
     * When neural embeddings are absent, extracts structured lead/conclusion chunks without feature-hash pollution.
     */
    fun selectHighCentralityChunks(
        fullText: String,
        targetChars: Int = calculateSafeSummaryCharLimit(fullText),
        customEmbeddings: List<FloatArray>? = null
    ): String {
        if (fullText.length <= targetChars) return fullText
        val isMyanmar = isMyanmarScript(fullText)
        val chunkSize = if (isMyanmar) 600 else 1400
        val overlap = if (isMyanmar) 60 else 120
        val chunks = chunkText(fullText, chunkSize = chunkSize, overlap = overlap)
        if (chunks.size <= 1) return fullText.take(targetChars)

        val hasNeural = customEmbeddings != null && customEmbeddings.size == chunks.size
        val scoredIndices = if (hasNeural) {
            val dim = InferenceService.EMBEDDING_DIM
            val embeddings = customEmbeddings!!
            val centroid = FloatArray(dim)
            for (vec in embeddings) {
                for (d in 0 until dim.coerceAtMost(vec.size)) {
                    centroid[d] += vec[d]
                }
            }
            var normSq = 0.0
            for (v in centroid) normSq += v.toDouble() * v.toDouble()
            if (normSq > 1e-12) {
                val inv = (1.0 / kotlin.math.sqrt(normSq)).toFloat()
                for (i in centroid.indices) centroid[i] *= inv
            }

            chunks.indices.map { idx ->
                val vec = embeddings[idx]
                var dot = 0.0f
                for (d in 0 until dim.coerceAtMost(vec.size)) {
                    dot += vec[d] * centroid[d]
                }
                val bonus = when (idx) {
                    0 -> 0.15f
                    chunks.lastIndex -> 0.10f
                    else -> 0.0f
                }
                idx to (dot + bonus)
            }.sortedByDescending { it.second }
        } else {
            // Clean structural Lead/Conclusion heuristic when neural embedding model is absent
            chunks.indices.map { idx ->
                val score = when (idx) {
                    0 -> 1.0f              // Executive summary / introduction chunk
                    chunks.lastIndex -> 0.85f // Conclusion / recommendations chunk
                    1 -> 0.6f
                    else -> 0.3f
                }
                idx to score
            }.sortedByDescending { it.second }
        }

        val selectedIndices = mutableListOf<Int>()
        var accumulatedChars = 0
        for ((idx, _) in scoredIndices) {
            val len = chunks[idx].length
            if (accumulatedChars + len <= targetChars) {
                selectedIndices.add(idx)
                accumulatedChars += len + 32
            } else if (selectedIndices.isEmpty()) {
                selectedIndices.add(idx)
                accumulatedChars += len
                break
            }
        }

        if (selectedIndices.isEmpty()) return fullText.take(targetChars)

        selectedIndices.sort()
        val combined = selectedIndices.joinToString("\n\n---\n\n") { idx ->
            "[Section ${idx + 1}/${chunks.size}]\n${chunks[idx]}"
        }
        return if (combined.length > targetChars) combined.take(targetChars) else combined
    }

    /**
     * Phase 2.3: Hybrid Document Search (Exact Line Match + FTS5 BM25 + Scoped Neural Vector RRF).
     * Routes cleanly to SQLite FTS5 BM25 + MyanmarSegmenter lexical search whenever the embedding model is absent.
     */
    suspend fun hybridSearchInDocument(
        file: File,
        query: String,
        nativeEngine: NativeEngine? = null,
        maxResults: Int = 6
    ): SearchResult {
        val readResult = readDocument(file)
        if (!readResult.success) {
            return SearchResult(file.absolutePath, query, 0, emptyList(), readResult.error)
        }

        // 1. Run fast lexical line search
        val lexicalResult = searchInDocument(file, query, maxResults = maxResults)
        val combinedMatches = mutableListOf<SearchMatch>()
        combinedMatches.addAll(lexicalResult.matches)

        val chunks = chunkText(readResult.content, chunkSize = 1200, overlap = 100)
        if (chunks.isNotEmpty()) {
            val embModelPath = nativeEngine?.resolveEmbeddingModelPath() ?: ""
            val hasNeuralModel = embModelPath.isNotBlank() && File(embModelPath).exists()
            val modelId = nativeEngine?.configuredEmbeddingTier?.modelId ?: "embeddinggemma-2-270m"

            if (hasNeuralModel && nativeEngine != null && nativeEngine.isNativeLibraryLoaded()) {
                val docHash = computeFileSha256(file)
                val alreadyIndexed = nativeEngine.isDocumentIndexedSafe(file.absolutePath, docHash, modelId)

                // Phase 1 Active Inference: Dual-Gate BM25 Entropy Smart RAG Wakeup
                // If the document is already indexed in FTS5 and lexical search has high peak relevance
                // and low normalized Shannon entropy (H_hat < 0.35), select LEXICAL_ONLY and skip waking up EmbeddingGemma 2!
                val wakeupEval = if (alreadyIndexed) {
                    nativeEngine.evaluateRagWakeupSafe(query, file.absolutePath, topK = 5)
                } else null
                val shouldWakeupEmbedding = wakeupEval?.optBoolean("should_wakeup_embedding", true) ?: true

                if (!shouldWakeupEmbedding && wakeupEval != null) {
                    val normH = wakeupEval.optDouble("normalized_entropy", 0.0)
                    val topMatchesArr = wakeupEval.optJSONArray("top_matches")
                    if (topMatchesArr != null) {
                        for (i in 0 until topMatchesArr.length()) {
                            val j = topMatchesArr.optJSONObject(i) ?: continue
                            val cIdx = j.optInt("chunk_index", 0)
                            val cText = j.optString("chunk_text", "").trim()
                            val score = j.optDouble("rrf_score", 0.0)
                            if (cText.isNotEmpty() && combinedMatches.none { it.snippet.contains(cText.take(60)) || cText.contains(it.lineContent) }) {
                                combinedMatches.add(
                                    SearchMatch(
                                        lineNumber = cIdx + 1,
                                        lineContent = "[LEXICAL_ONLY Chunk #${cIdx + 1} • H=${"%.2f".format(normH)} • RRF ${"%.3f".format(score)}]",
                                        snippet = cText.take(360)
                                    )
                                )
                            }
                        }
                    }
                } else {
                    // Atomic re-indexing check: only generate and index embeddings if document hash changed
                    if (!alreadyIndexed) {
                        val chunkVectors = nativeEngine.generateEmbeddingsSubBatched(chunks, embModelPath)
                        if (chunkVectors.size == chunks.size) {
                            val chunksWithIndex = chunks.mapIndexed { idx, txt -> idx to txt }
                            nativeEngine.storeDocumentChunksAtomicSafe(file.absolutePath, docHash, modelId, chunksWithIndex, chunkVectors)
                        }
                    }

                    // Query vector extraction (INSPECT_DOC policy)
                    val queryVecList = nativeEngine.generateEmbeddingsSubBatched(listOf(query), embModelPath)
                    val queryVec = if (queryVecList.isNotEmpty()) queryVecList[0] else FloatArray(0)

                    val hybridJsonList = nativeEngine.hybridSearchWithModelSafe(query, queryVec, file.absolutePath, topK = 4, modelId = modelId)
                    for (j in hybridJsonList) {
                        val cIdx = j.optInt("chunk_index", 0)
                        val cText = j.optString("chunk_text", "").trim()
                        val score = j.optDouble("rrf_score", 0.0)
                        if (cText.isNotEmpty() && combinedMatches.none { it.snippet.contains(cText.take(60)) || cText.contains(it.lineContent) }) {
                            combinedMatches.add(
                                SearchMatch(
                                    lineNumber = cIdx + 1,
                                    lineContent = "[RAG Chunk #${cIdx + 1} • RRF ${"%.3f".format(score)}]",
                                    snippet = cText.take(360)
                                )
                            )
                        }
                    }
                }
            } else if (nativeEngine != null && nativeEngine.isNativeLibraryLoaded()) {
                // Clean Fallback: Route to SQLite FTS5 BM25 + MyanmarSegmenter with empty query vector (zero hash pollution)
                val ftsJsonList = nativeEngine.hybridSearchSafe(query, FloatArray(0), file.absolutePath, topK = 4)
                for (j in ftsJsonList) {
                    val cIdx = j.optInt("chunk_index", 0)
                    val cText = j.optString("chunk_text", "").trim()
                    val score = j.optDouble("rrf_score", 0.0)
                    if (cText.isNotEmpty() && combinedMatches.none { it.snippet.contains(cText.take(60)) || cText.contains(it.lineContent) }) {
                        combinedMatches.add(
                            SearchMatch(
                                lineNumber = cIdx + 1,
                                lineContent = "[FTS5 BM25 Chunk #${cIdx + 1} • RRF ${"%.3f".format(score)}]",
                                snippet = cText.take(360)
                            )
                        )
                    }
                }
            }
        }

        val finalMatches = combinedMatches.take(maxResults)
        return SearchResult(
            filePath = file.absolutePath,
            query = query,
            totalMatches = finalMatches.size,
            matches = finalMatches
        )
    }
}
