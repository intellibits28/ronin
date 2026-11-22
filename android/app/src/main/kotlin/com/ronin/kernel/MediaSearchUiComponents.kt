package com.ronin.kernel

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.net.Uri
import android.provider.MediaStore
import android.widget.MediaController
import android.widget.Toast
import android.widget.VideoView
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.max
import kotlin.math.roundToInt

data class MediaMomentPlayerRequest(
    val item: MediaSearchResultItem,
    val startMs: Long
)

fun openMediaItemExternally(context: Context, item: MediaSearchResultItem) {
    val file = File(item.mediaPath)
    if (file.exists() && file.canRead()) {
        FileSearchNodeHooks.performFileAction(context, file.absolutePath, "view")
        return
    }
    if (item.mediaUri.isNotBlank()) {
        try {
            val uri = Uri.parse(item.mediaUri)
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, FileSearchNodeHooks.getMimeType(item.displayName))
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            val chooser = Intent.createChooser(intent, "Open with...").apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(chooser)
        } catch (e: Exception) {
            Toast.makeText(context, "Cannot open media: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    } else {
        Toast.makeText(context, "File not accessible: ${item.displayName}", Toast.LENGTH_SHORT).show()
    }
}

fun shareMediaItem(context: Context, item: MediaSearchResultItem) {
    val file = File(item.mediaPath)
    if (file.exists() && file.canRead()) {
        FileSearchNodeHooks.shareFile(context, file.absolutePath)
        return
    }
    if (item.mediaUri.isNotBlank()) {
        try {
            val uri = Uri.parse(item.mediaUri)
            val intent = Intent(Intent.ACTION_SEND).apply {
                type = FileSearchNodeHooks.getMimeType(item.displayName)
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, item.displayName)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            val chooser = Intent.createChooser(intent, "Share media via...").apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(chooser)
        } catch (e: Exception) {
            Toast.makeText(context, "Cannot share media: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
}

private suspend fun loadThumbnailBitmap(
    context: Context,
    item: MediaSearchResultItem
): Bitmap? = withContext(Dispatchers.IO) {
    val file = File(item.mediaPath)
    val useFile = file.exists() && file.canRead()
    val uri = if (!useFile && item.mediaUri.isNotBlank()) Uri.parse(item.mediaUri) else null
    if (!useFile && uri == null) return@withContext null

    return@withContext try {
        if (item.modality.startsWith("VIDEO")) {
            val retriever = MediaMetadataRetriever()
            try {
                if (useFile) retriever.setDataSource(file.absolutePath)
                else retriever.setDataSource(context, uri!!)
                val raw = retriever.getFrameAtTime(item.startMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
                    ?: retriever.getFrameAtTime(0L)
                if (raw != null && max(raw.width, raw.height) > 280) {
                    val scale = 280f / max(raw.width, raw.height).toFloat()
                    val scaled = Bitmap.createScaledBitmap(
                        raw,
                        max(1, (raw.width * scale).roundToInt()),
                        max(1, (raw.height * scale).roundToInt()),
                        true
                    )
                    if (scaled !== raw) raw.recycle()
                    scaled
                } else raw
            } finally {
                try { retriever.release() } catch (_: Throwable) {}
            }
        } else if (item.modality == "IMAGE") {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            if (useFile) BitmapFactory.decodeFile(file.absolutePath, bounds)
            else context.contentResolver.openInputStream(uri!!)?.use { BitmapFactory.decodeStream(it, null, bounds) }
            var sample = 1
            val longest = max(bounds.outWidth, bounds.outHeight)
            while (longest / sample > 280) sample *= 2
            val opts = BitmapFactory.Options().apply { inSampleSize = sample }
            if (useFile) BitmapFactory.decodeFile(file.absolutePath, opts)
            else context.contentResolver.openInputStream(uri!!)?.use { BitmapFactory.decodeStream(it, null, opts) }
        } else {
            null
        }
    } catch (_: Throwable) {
        null
    }
}

@Composable
fun MediaSearchResultListCard(
    items: List<MediaSearchResultItem>,
    onPlayMoment: (MediaSearchResultItem, Long) -> Unit
) {
    val context = LocalContext.current
    Column(
        modifier = Modifier.fillMaxWidth().padding(top = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items.forEach { item ->
            var thumbBmp by remember(item.mediaPath, item.startMs) { mutableStateOf<Bitmap?>(null) }
            LaunchedEffect(item.mediaPath, item.mediaUri, item.startMs) {
                if (item.modality == "IMAGE" || item.modality.startsWith("VIDEO")) {
                    thumbBmp = loadThumbnailBitmap(context, item)
                }
            }

            val isVideo = item.modality.startsWith("VIDEO")
            val isAudio = item.modality == "AUDIO"
            val modBadgeText = when (item.modality) {
                "IMAGE" -> "PHOTO"
                "AUDIO" -> "AUDIO"
                "VIDEO_FRAME" -> "VIDEO FRAME"
                "VIDEO_AUDIO" -> "VIDEO AUDIO"
                else -> item.modality
            }
            val badgeColor = when {
                isVideo -> Color(0xFF64B5F6)
                isAudio -> Color(0xFFCE93D8)
                else -> RoninThemeTokens.PrimaryEmerald
            }
            val matchPct = (item.score.coerceIn(0f, 1f) * 100f).roundToInt()

            Surface(
                color = RoninThemeTokens.SurfaceLight,
                shape = RoninThemeTokens.RadiusMd,
                border = BorderStroke(1.dp, RoninThemeTokens.Border),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(modifier = Modifier.padding(10.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            modifier = Modifier
                                .size(64.dp)
                                .clip(RoninThemeTokens.RadiusSm)
                                .background(Color(0xFF0E111A))
                                .clickable {
                                    if (isVideo || isAudio) onPlayMoment(item, item.startMs)
                                    else openMediaItemExternally(context, item)
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            if (thumbBmp != null) {
                                Image(
                                    bitmap = thumbBmp!!.asImageBitmap(),
                                    contentDescription = item.displayName,
                                    contentScale = ContentScale.Crop,
                                    modifier = Modifier.fillMaxSize()
                                )
                            } else {
                                Icon(
                                    imageVector = when {
                                        isVideo -> Icons.Default.Movie
                                        isAudio -> Icons.Default.GraphicEq
                                        else -> Icons.Default.Image
                                    },
                                    contentDescription = modBadgeText,
                                    tint = badgeColor,
                                    modifier = Modifier.size(28.dp)
                                )
                            }
                            if (isVideo) {
                                Surface(
                                    color = Color.Black.copy(alpha = 0.72f),
                                    shape = RoundedCornerShape(3.dp),
                                    modifier = Modifier.align(Alignment.BottomEnd).padding(2.dp)
                                ) {
                                    Text(
                                        MediaSearchEngine.formatTimestampMs(item.startMs),
                                        color = Color.White,
                                        fontSize = 8.sp,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.padding(horizontal = 3.dp, vertical = 1.dp)
                                    )
                                }
                            }
                        }

                        Spacer(Modifier.width(10.dp))

                        Column(modifier = Modifier.weight(1f)) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Surface(
                                    color = badgeColor.copy(alpha = 0.16f),
                                    shape = RoundedCornerShape(4.dp)
                                ) {
                                    Text(
                                        modBadgeText,
                                        color = badgeColor,
                                        fontSize = 9.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.padding(horizontal = 5.dp, vertical = 2.dp)
                                    )
                                }
                                Text(
                                    "$matchPct% match",
                                    color = RoninThemeTokens.PrimaryEmerald,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                            Spacer(Modifier.height(3.dp))
                            Text(
                                item.displayName,
                                color = RoninThemeTokens.TextPrimary,
                                fontSize = 13.sp,
                                fontWeight = FontWeight.SemiBold,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            if (isVideo || isAudio) {
                                Text(
                                    "Matched moment: ${MediaSearchEngine.formatTimestampMs(item.startMs)} – ${MediaSearchEngine.formatTimestampMs(item.endMs)}",
                                    color = Color(0xFF80DEEA),
                                    fontSize = 10.sp,
                                    fontFamily = FontFamily.Monospace
                                )
                            } else {
                                Text(
                                    item.mediaPath,
                                    color = RoninThemeTokens.TextMuted,
                                    fontSize = 10.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    }

                    if (item.extraMomentsMs.isNotEmpty() && (isVideo || isAudio)) {
                        Spacer(Modifier.height(6.dp))
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Also at:", color = RoninThemeTokens.TextMuted, fontSize = 10.sp)
                            item.extraMomentsMs.forEach { ts ->
                                Surface(
                                    color = Color(0xFF25283D),
                                    shape = RoundedCornerShape(4.dp),
                                    border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.35f)),
                                    modifier = Modifier.clickable { onPlayMoment(item, ts) }
                                ) {
                                    Text(
                                        "▶ ${MediaSearchEngine.formatTimestampMs(ts)}",
                                        color = Color(0xFF64B5F6),
                                        fontSize = 10.sp,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
                                    )
                                }
                            }
                        }
                    }

                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        if (isVideo || isAudio) {
                            Surface(
                                color = RoninThemeTokens.PrimaryEmeraldDim,
                                shape = RoninThemeTokens.RadiusSm,
                                border = BorderStroke(1.dp, RoninThemeTokens.PrimaryEmerald.copy(alpha = 0.45f)),
                                modifier = Modifier.clickable { onPlayMoment(item, item.startMs) }
                            ) {
                                Row(
                                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(Icons.Default.PlayArrow, "Play", tint = RoninThemeTokens.PrimaryEmerald, modifier = Modifier.size(14.dp))
                                    Spacer(Modifier.width(4.dp))
                                    Text(
                                        "Play at ${MediaSearchEngine.formatTimestampMs(item.startMs)}",
                                        color = RoninThemeTokens.PrimaryEmerald,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace
                                    )
                                }
                            }
                        }
                        Surface(
                            color = Color(0xFF25283D),
                            shape = RoninThemeTokens.RadiusSm,
                            border = BorderStroke(1.dp, RoninThemeTokens.Border),
                            modifier = Modifier.clickable { openMediaItemExternally(context, item) }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.OpenInNew, "Open", tint = Color.White, modifier = Modifier.size(13.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("Open", color = Color.White, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                            }
                        }
                        Surface(
                            color = Color(0xFF25283D),
                            shape = RoninThemeTokens.RadiusSm,
                            border = BorderStroke(1.dp, RoninThemeTokens.Border),
                            modifier = Modifier.clickable { shareMediaItem(context, item) }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.Share, "Share", tint = Color.White, modifier = Modifier.size(13.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("Share", color = Color.White, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun MediaMomentPlayerDialog(
    request: MediaMomentPlayerRequest,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val item = request.item
    val isVideo = item.modality.startsWith("VIDEO")
    var targetStartMs by remember(request) { mutableStateOf(request.startMs) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            color = RoninThemeTokens.Surface,
            shape = RoninThemeTokens.RadiusLg,
            border = BorderStroke(1.dp, RoninThemeTokens.Border),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(14.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            if (isVideo) "VIDEO MOMENT PLAYER" else "AUDIO MOMENT PLAYER",
                            style = RoninThemeTokens.WhisperLabelStyle.copy(color = Color(0xFF64B5F6))
                        )
                        Text(
                            item.displayName,
                            color = RoninThemeTokens.TextPrimary,
                            fontSize = 14.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    IconButton(onClick = onDismiss, modifier = Modifier.size(28.dp)) {
                        Icon(Icons.Default.Close, "Close", tint = Color.Gray)
                    }
                }

                Spacer(Modifier.height(10.dp))

                if (isVideo) {
                    var videoViewRef by remember { mutableStateOf<VideoView?>(null) }
                    AndroidView(
                        factory = { ctx ->
                            VideoView(ctx).apply {
                                val controller = MediaController(ctx)
                                controller.setAnchorView(this)
                                setMediaController(controller)
                                val file = File(item.mediaPath)
                                if (file.exists() && file.canRead()) {
                                    setVideoPath(file.absolutePath)
                                } else if (item.mediaUri.isNotBlank()) {
                                    setVideoURI(Uri.parse(item.mediaUri))
                                }
                                setOnPreparedListener { mp ->
                                    if (targetStartMs > 0L) mp.seekTo(targetStartMs.toInt())
                                    start()
                                }
                                setOnErrorListener { _, _, _ ->
                                    Toast.makeText(ctx, "Playback error; try Open Externally", Toast.LENGTH_SHORT).show()
                                    false
                                }
                                videoViewRef = this
                            }
                        },
                        update = { vv ->
                            videoViewRef = vv
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(220.dp)
                            .clip(RoninThemeTokens.RadiusMd)
                            .background(Color.Black)
                    )
                    DisposableEffect(Unit) {
                        onDispose {
                            try { videoViewRef?.stopPlayback() } catch (_: Throwable) {}
                        }
                    }

                    if (item.extraMomentsMs.isNotEmpty() || item.startMs > 0L) {
                        Spacer(Modifier.height(8.dp))
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            val jumpPoints = (listOf(item.startMs) + item.extraMomentsMs).distinct()
                            jumpPoints.forEach { ts ->
                                Surface(
                                    color = Color(0xFF25283D),
                                    shape = RoundedCornerShape(4.dp),
                                    border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.45f)),
                                    modifier = Modifier.clickable {
                                        targetStartMs = ts
                                        try {
                                            videoViewRef?.seekTo(ts.toInt())
                                            videoViewRef?.start()
                                        } catch (_: Throwable) {}
                                    }
                                ) {
                                    Text(
                                        "▶ ${MediaSearchEngine.formatTimestampMs(ts)}",
                                        color = Color(0xFF64B5F6),
                                        fontSize = 11.sp,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                    )
                                }
                            }
                        }
                    }
                } else {
                    var isPlaying by remember { mutableStateOf(false) }
                    var isPrepared by remember { mutableStateOf(false) }
                    var positionMs by remember { mutableStateOf(targetStartMs) }
                    var durationMs by remember { mutableStateOf(max(item.durationMs, targetStartMs + 20_000L)) }
                    var playerError by remember { mutableStateOf<String?>(null) }

                    val mediaPlayer = remember(item.mediaPath, item.mediaUri) { MediaPlayer() }
                    DisposableEffect(item.mediaPath, item.mediaUri) {
                        try {
                            val file = File(item.mediaPath)
                            if (file.exists() && file.canRead()) {
                                mediaPlayer.setDataSource(file.absolutePath)
                            } else if (item.mediaUri.isNotBlank()) {
                                mediaPlayer.setDataSource(context, Uri.parse(item.mediaUri))
                            }
                            mediaPlayer.setOnPreparedListener { mp ->
                                durationMs = max(1L, mp.duration.toLong())
                                if (targetStartMs > 0L) mp.seekTo(targetStartMs.toInt())
                                mp.start()
                                isPrepared = true
                                isPlaying = true
                            }
                            mediaPlayer.setOnCompletionListener {
                                isPlaying = false
                            }
                            mediaPlayer.setOnErrorListener { _, what, extra ->
                                playerError = "Audio playback failed ($what/$extra)"
                                isPlaying = false
                                true
                            }
                            mediaPlayer.prepareAsync()
                        } catch (e: Exception) {
                            playerError = e.message ?: "Unable to initialize audio player"
                        }
                        onDispose {
                            try {
                                if (mediaPlayer.isPlaying) mediaPlayer.stop()
                                mediaPlayer.release()
                            } catch (_: Throwable) {}
                        }
                    }

                    LaunchedEffect(isPrepared, isPlaying) {
                        while (isPrepared && isPlaying) {
                            try {
                                positionMs = mediaPlayer.currentPosition.toLong()
                            } catch (_: Throwable) {}
                            delay(250)
                        }
                    }

                    Surface(
                        color = RoninThemeTokens.SurfaceLight,
                        shape = RoninThemeTokens.RadiusMd,
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            if (playerError != null) {
                                Text("⚠️ $playerError", color = RoninThemeTokens.ErrorCrimson, fontSize = 11.sp)
                            } else {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    IconButton(
                                        onClick = {
                                            if (!isPrepared) return@IconButton
                                            try {
                                                if (mediaPlayer.isPlaying) {
                                                    mediaPlayer.pause()
                                                    isPlaying = false
                                                } else {
                                                    mediaPlayer.start()
                                                    isPlaying = true
                                                }
                                            } catch (_: Throwable) {}
                                        }
                                    ) {
                                        Icon(
                                            if (isPlaying) Icons.Default.Pause else Icons.Default.PlayArrow,
                                            contentDescription = "Play/Pause",
                                            tint = RoninThemeTokens.PrimaryEmerald,
                                            modifier = Modifier.size(32.dp)
                                        )
                                    }
                                    Column(modifier = Modifier.weight(1f)) {
                                        Slider(
                                            value = positionMs.toFloat().coerceIn(0f, max(1f, durationMs.toFloat())),
                                            onValueChange = { newPos ->
                                                positionMs = newPos.toLong()
                                                if (isPrepared) {
                                                    try { mediaPlayer.seekTo(positionMs.toInt()) } catch (_: Throwable) {}
                                                }
                                            },
                                            valueRange = 0f..max(1f, durationMs.toFloat()),
                                            colors = SliderDefaults.colors(
                                                thumbColor = RoninThemeTokens.PrimaryEmerald,
                                                activeTrackColor = RoninThemeTokens.PrimaryEmerald
                                            )
                                        )
                                        Row(
                                            modifier = Modifier.fillMaxWidth(),
                                            horizontalArrangement = Arrangement.SpaceBetween
                                        ) {
                                            Text(
                                                MediaSearchEngine.formatTimestampMs(positionMs),
                                                color = Color(0xFF80DEEA),
                                                fontSize = 10.sp,
                                                fontFamily = FontFamily.Monospace
                                            )
                                            Text(
                                                MediaSearchEngine.formatTimestampMs(durationMs),
                                                color = Color.Gray,
                                                fontSize = 10.sp,
                                                fontFamily = FontFamily.Monospace
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Spacer(Modifier.height(10.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    OutlinedButton(
                        onClick = { openMediaItemExternally(context, item) },
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White)
                    ) {
                        Icon(Icons.Default.OpenInNew, null, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Open Externally", fontSize = 11.sp)
                    }
                    Button(
                        onClick = onDismiss,
                        colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D))
                    ) {
                        Text("Close", color = Color.White, fontSize = 11.sp)
                    }
                }
            }
        }
    }
}

@Composable
fun MediaIndexerSettingsSection(
    chatViewModel: ChatViewModel,
    activity: MainActivity?
) {
    LaunchedEffect(Unit) {
        activity?.refreshMediaIndexInfo()
    }

    SettingsCardSection(title = "4.6 Multimodal Media Search (EmbeddingGemma 2)") {
        Text(
            "Runtime: LiteRT-LM 0.19.1 • 768D Unified Vector Space",
            color = Color(0xFF00E5FF),
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Spacer(Modifier.height(4.dp))
        Text(
            chatViewModel.embeddingModelCapabilitiesText,
            color = RoninThemeTokens.TextSecondary,
            fontSize = 11.sp
        )
        Spacer(Modifier.height(4.dp))
        Text(
            chatViewModel.mediaIndexStatsText,
            color = RoninThemeTokens.PrimaryEmerald,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )

        if (chatViewModel.isIndexingMedia) {
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(
                progress = chatViewModel.mediaIndexFraction.coerceIn(0f, 1f),
                modifier = Modifier.fillMaxWidth(),
                color = Color(0xFF00E5FF),
                backgroundColor = Color(0xFF25283D)
            )
            Spacer(Modifier.height(4.dp))
            Text(
                chatViewModel.mediaIndexProgressText,
                color = Color(0xFF80DEEA),
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
            Spacer(Modifier.height(6.dp))
            OutlinedButton(
                onClick = { activity?.cancelMediaIndexing() },
                modifier = Modifier.fillMaxWidth(),
                border = BorderStroke(1.dp, RoninThemeTokens.ErrorCrimson.copy(alpha = 0.6f)),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = RoninThemeTokens.ErrorCrimson)
            ) {
                Text("Cancel Media Indexing", fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        } else {
            Spacer(Modifier.height(10.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.PHOTOS) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Index Photos", color = Color.White, fontSize = 11.sp)
                }
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.AUDIO) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Index Audio", color = Color.White, fontSize = 11.sp)
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.VIDEO) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Index Videos", color = Color.White, fontSize = 11.sp)
                }
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.ALL) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = RoninThemeTokens.PrimaryEmeraldDim),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Index All", color = RoninThemeTokens.PrimaryEmerald, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = { activity?.clearMediaSearchIndex() },
                modifier = Modifier.fillMaxWidth().heightIn(min = 38.dp),
                border = BorderStroke(1.dp, RoninThemeTokens.Border),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = RoninThemeTokens.TextSecondary)
            ) {
                Icon(Icons.Default.DeleteSweep, null, modifier = Modifier.size(15.dp))
                Spacer(Modifier.width(6.dp))
                Text("Clear Media Index", fontSize = 11.sp)
            }
        }
    }
}
