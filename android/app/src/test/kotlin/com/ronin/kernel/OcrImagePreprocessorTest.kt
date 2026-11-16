package com.ronin.kernel

import org.junit.Assert.*
import org.junit.Test

class OcrImagePreprocessorTest {

    @Test
    fun testDarkModeScreenshotPixelsDetectedAndInvertedToBlackOnWhite() {
        val width = 40
        val height = 40
        val darkBg = 0xFF18191A.toInt() // Facebook dark mode background (luma ~24)
        val lightText = 0xFFE4E6EB.toInt() // Light UI text (luma ~229)

        val pixels = IntArray(width * height) { darkBg }
        // Paint a horizontal "text" band in center
        for (y in 16..20) {
            for (x in 8..31) {
                pixels[y * width + x] = lightText
            }
        }

        val res = OcrImagePreprocessor.preprocessPixels(pixels, width, height)
        assertTrue("Dark-mode background should trigger inversion", res.inverted)

        // Background pixel (corner) should now be crisp bright white (~255)
        val outBgLuma = OcrImagePreprocessor.computeLuminance(res.pixels[0])
        assertTrue("Inverted background luma should be >= 235 (was $outBgLuma)", outBgLuma >= 235)

        // Foreground text pixel should now be crisp dark black (~0)
        val outTextLuma = OcrImagePreprocessor.computeLuminance(res.pixels[18 * width + 15])
        assertTrue("Inverted text luma should be <= 25 (was $outTextLuma)", outTextLuma <= 25)
    }

    @Test
    fun testLightDocumentPixelsNotInverted() {
        val width = 40
        val height = 40
        val whiteBg = 0xFFFFFFFF.toInt()
        val darkInk = 0xFF151515.toInt()

        val pixels = IntArray(width * height) { whiteBg }
        for (y in 12..16) {
            for (x in 6..30) {
                pixels[y * width + x] = darkInk
            }
        }

        val res = OcrImagePreprocessor.preprocessPixels(pixels, width, height)
        assertFalse("Light document should NOT be inverted", res.inverted)

        val outBgLuma = OcrImagePreprocessor.computeLuminance(res.pixels[0])
        val outTextLuma = OcrImagePreprocessor.computeLuminance(res.pixels[14 * width + 12])
        assertTrue("Background should remain bright (was $outBgLuma)", outBgLuma >= 240)
        assertTrue("Text ink should remain dark (was $outTextLuma)", outTextLuma <= 20)
    }

    @Test
    fun testLowContrastGrayTextStretchedToSharpBlackAndWhite() {
        val width = 40
        val height = 40
        val lightGrayBg = 0xFFD2D2D2.toInt() // luma 210
        val faintGrayText = 0xFF8C8C8C.toInt() // luma 140

        val pixels = IntArray(width * height) { lightGrayBg }
        for (y in 14..22) {
            for (x in 8..32) {
                pixels[y * width + x] = faintGrayText
            }
        }

        val res = OcrImagePreprocessor.preprocessPixels(pixels, width, height)
        assertFalse(res.inverted)

        val outBgLuma = OcrImagePreprocessor.computeLuminance(res.pixels[0])
        val outTextLuma = OcrImagePreprocessor.computeLuminance(res.pixels[18 * width + 16])
        assertTrue("Background should stretch near 255 (was $outBgLuma)", outBgLuma >= 250)
        assertTrue("Faint text should stretch near 0 (was $outTextLuma)", outTextLuma <= 10)
    }

    @Test
    fun testMergeUniqueOcrLinesPreservesHeaderAndAddsMissingNumbersWithoutDuplicates() {
        val pass1Auto = "09 799 123 456"
        val pass2Sparse = "Ko Kyaw Zin\n09 799 123 456\n09 420 987 654\n."

        val merged = OcrEngine.mergeUniqueOcrLines(pass1Auto, pass2Sparse)
        val lines = merged.lines()
        assertEquals(3, lines.size)
        assertEquals("Ko Kyaw Zin", lines[0])
        assertEquals("09 799 123 456", lines[1])
        assertEquals("09 420 987 654", lines[2])
    }
}
