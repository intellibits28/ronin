package com.ronin.kernel

import org.junit.Assert.*
import org.junit.Test
import java.io.File

class DocumentIntelligenceTest {

    @Test
    fun testReadTextDocumentSuccess() {
        val testFile = File.createTempFile("sample_notes", ".md")
        testFile.deleteOnExit()
        testFile.writeText("# Meeting Notes\n\n- Discussed Ronin Kernel\n- Accelerometer analysis\n- On-device inference")

        val result = DocumentIntelligence.readDocument(testFile)
        assertTrue(result.success)
        assertEquals(testFile.name, result.fileName)
        assertTrue(result.content.contains("Accelerometer analysis"))
        assertEquals(5, result.lineCount)
        assertFalse(result.isTruncated)
    }

    @Test
    fun testSearchInDocument() {
        val testFile = File.createTempFile("server", ".log")
        testFile.deleteOnExit()
        testFile.writeText("""
            2026-09-25 10:00:01 INFO System started
            2026-09-25 10:00:05 ERROR Connection timeout on port 8080
            2026-09-25 10:00:10 INFO Retrying connection
            2026-09-25 10:00:15 ERROR Database deadlock detected
            2026-09-25 10:00:20 INFO System healthy
        """.trimIndent())

        val searchRes = DocumentIntelligence.searchInDocument(testFile, "ERROR")
        assertNull(searchRes.error)
        assertEquals(2, searchRes.totalMatches)
        assertEquals(2, searchRes.matches.size)
        assertEquals(2, searchRes.matches[0].lineNumber)
        assertTrue(searchRes.matches[0].lineContent.contains("port 8080"))
        assertEquals(4, searchRes.matches[1].lineNumber)
        assertTrue(searchRes.matches[1].lineContent.contains("deadlock"))
    }

    @Test
    fun testChunkText() {
        val longText = (1..100).joinToString("\n") { "Line $it: The quick brown fox jumps over the lazy dog." }
        val chunks = DocumentIntelligence.chunkText(longText, chunkSize = 500, overlap = 50)
        assertTrue(chunks.size > 1)
        for (chunk in chunks) {
            assertTrue(chunk.isNotBlank())
        }
    }

    @Test
    fun testBuildSummaryPromptMyanmar() {
        val prompt = DocumentIntelligence.buildSummaryPrompt("report.txt", "Sample text here", langMyanmar = true)
        assertTrue(prompt.contains("မြန်မာဘာသာဖြင့်"))
        assertTrue(prompt.contains("အကျဉ်းချုပ်"))
        assertTrue(prompt.contains("Sample text here"))
    }
}
