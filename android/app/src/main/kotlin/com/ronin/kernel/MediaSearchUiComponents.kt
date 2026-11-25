package com.ronin.kernel

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.net.Uri
import android.widget.MediaController
import android.widget.Toast
import android.widget.VideoView
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
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
import androidx.compose.ui.window.DialogProperties
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

private suspend fun loadMediaThumbnailBitmap(
    context: Context,
    mediaPath: String,
    mediaUri: String,
    modality: String,
    startMs: Long = 0L,
    targetPx: Int = 320,
    assetType: String = ""
): Bitmap? = withContext(Dispatchers.IO) {
    val file = File(mediaPath)
    val useFile = file.exists() && file.canRead()
    val uri = if (!useFile && mediaUri.isNotBlank()) Uri.parse(mediaUri) else null
    if (!useFile && uri == null) return@withContext null

    val isVideoAsset = modality.startsWith("VIDEO", ignoreCase = true) || assetType.equals("video", ignoreCase = true)
    val isImageAsset = modality.equals("image", ignoreCase = true) && !isVideoAsset

    return@withContext try {
        if (isVideoAsset) {
            val retriever = MediaMetadataRetriever()
            try {
                if (useFile) retriever.setDataSource(file.absolutePath)
                else retriever.setDataSource(context, uri!!)
                val raw = retriever.getFrameAtTime(startMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
                    ?: retriever.getFrameAtTime(0L)
                if (raw != null && max(raw.width, raw.height) > targetPx) {
                    val scale = targetPx.toFloat() / max(raw.width, raw.height).toFloat()
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
        } else if (isImageAsset) {
            MediaSearchEngine.decodeOrientedSampledBitmap(mediaPath, mediaUri, context, maxSidePx = targetPx)
        } else {
            null
        }
    } catch (_: Throwable) {
        null
    }
}

private suspend fun loadThumbnailBitmap(
    context: Context,
    item: MediaSearchResultItem,
    targetPx: Int = 320
): Bitmap? = loadMediaThumbnailBitmap(
    context = context,
    mediaPath = item.mediaPath,
    mediaUri = item.mediaUri,
    modality = item.modality,
    startMs = item.startMs,
    targetPx = targetPx,
    assetType = item.assetType
)

@Composable
fun PhotoPreviewDialog(
    item: MediaSearchResultItem,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    var previewBmp by remember(item.mediaPath, item.mediaUri) { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(item.mediaPath, item.mediaUri) {
        previewBmp = loadThumbnailBitmap(context, item, targetPx = 960)
    }

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
                            "PHOTO MATCH • ${"%.2f".format(item.score)} (${(item.score.coerceIn(0f, 1f) * 100f).roundToInt()}%)",
                            style = RoninThemeTokens.WhisperLabelStyle.copy(color = RoninThemeTokens.PrimaryEmerald)
                        )
                        Text(
                            item.displayName,
                            color = RoninThemeTokens.TextPrimary,
                            fontSize = 13.sp,
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

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 180.dp, max = 380.dp)
                        .clip(RoninThemeTokens.RadiusMd)
                        .background(Color.Black),
                    contentAlignment = Alignment.Center
                ) {
                    if (previewBmp != null) {
                        Image(
                            bitmap = previewBmp!!.asImageBitmap(),
                            contentDescription = item.displayName,
                            contentScale = ContentScale.Fit,
                            modifier = Modifier.fillMaxWidth().heightIn(min = 180.dp, max = 380.dp)
                        )
                    } else {
                        CircularProgressIndicator(
                            modifier = Modifier.size(24.dp),
                            strokeWidth = 2.dp,
                            color = RoninThemeTokens.PrimaryEmerald
                        )
                    }
                }

                Spacer(Modifier.height(6.dp))
                Text(
                    item.mediaPath,
                    color = RoninThemeTokens.TextMuted,
                    fontSize = 10.sp,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )

                Spacer(Modifier.height(10.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = { openMediaItemExternally(context, item) },
                        modifier = Modifier.weight(1f),
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        colors = ButtonDefaults.outlinedButtonColors(
                            backgroundColor = RoninThemeTokens.SurfaceLight,
                            contentColor = Color.White
                        )
                    ) {
                        Icon(Icons.Default.OpenInNew, null, tint = Color.White, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Open", color = Color.White, fontSize = 11.sp)
                    }
                    OutlinedButton(
                        onClick = { shareMediaItem(context, item) },
                        modifier = Modifier.weight(1f),
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        colors = ButtonDefaults.outlinedButtonColors(
                            backgroundColor = RoninThemeTokens.SurfaceLight,
                            contentColor = Color.White
                        )
                    ) {
                        Icon(Icons.Default.Share, null, tint = Color.White, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Share", color = Color.White, fontSize = 11.sp)
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
fun MediaSearchResultListCard(
    items: List<MediaSearchResultItem>,
    onPlayMoment: (MediaSearchResultItem, Long) -> Unit
) {
    val context = LocalContext.current
    val photoItems = remember(items.toList()) {
        items.filter { it.modality.equals("image", ignoreCase = true) && !it.assetType.equals("video", ignoreCase = true) }
    }
    val momentItems = remember(items.toList()) {
        items.filter { !(it.modality.equals("image", ignoreCase = true) && !it.assetType.equals("video", ignoreCase = true)) }
    }
    var previewPhoto by remember { mutableStateOf<MediaSearchResultItem?>(null) }
    var showAllPhotos by remember(items.size) { mutableStateOf(false) }

    Column(
        modifier = Modifier.fillMaxWidth().padding(top = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        if (photoItems.isNotEmpty()) {
            val visiblePhotos = if (showAllPhotos || photoItems.size <= 12) photoItems else photoItems.take(12)
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                visiblePhotos.chunked(3).forEach { rowItems ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        rowItems.forEach { item ->
                            var gridBmp by remember(item.mediaPath, item.mediaUri) { mutableStateOf<Bitmap?>(null) }
                            LaunchedEffect(item.mediaPath, item.mediaUri) {
                                gridBmp = loadThumbnailBitmap(context, item, targetPx = 320)
                            }
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .aspectRatio(1f)
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(Color(0xFF0E111A))
                                    .border(1.dp, RoninThemeTokens.Border, RoundedCornerShape(8.dp))
                                    .clickable { previewPhoto = item },
                                contentAlignment = Alignment.Center
                            ) {
                                if (gridBmp != null) {
                                    Image(
                                        bitmap = gridBmp!!.asImageBitmap(),
                                        contentDescription = item.displayName,
                                        contentScale = ContentScale.Crop,
                                        modifier = Modifier.fillMaxSize()
                                    )
                                } else {
                                    Icon(
                                        imageVector = Icons.Default.Image,
                                        contentDescription = item.displayName,
                                        tint = RoninThemeTokens.PrimaryEmerald,
                                        modifier = Modifier.size(26.dp)
                                    )
                                }
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(26.dp)
                                        .align(Alignment.BottomCenter)
                                        .background(
                                            Brush.verticalGradient(
                                                colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.72f))
                                            )
                                        )
                                )
                                Surface(
                                    color = Color.Black.copy(alpha = 0.68f),
                                    shape = RoundedCornerShape(4.dp),
                                    modifier = Modifier.align(Alignment.BottomStart).padding(4.dp)
                                ) {
                                    Text(
                                        "%.2f".format(item.score),
                                        color = RoninThemeTokens.PrimaryEmerald,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.padding(horizontal = 5.dp, vertical = 1.dp)
                                    )
                                }
                            }
                        }
                        repeat(3 - rowItems.size) {
                            Spacer(Modifier.weight(1f))
                        }
                    }
                }
            }
            if (photoItems.size > 12) {
                Surface(
                    color = Color(0xFF25283D),
                    shape = RoundedCornerShape(8.dp),
                    border = BorderStroke(1.dp, RoninThemeTokens.Border),
                    modifier = Modifier.fillMaxWidth().clickable { showAllPhotos = !showAllPhotos }
                ) {
                    Text(
                        text = if (showAllPhotos) "Show fewer photos" else "Show all ${photoItems.size} photos (+${photoItems.size - 12} more)",
                        color = Color(0xFF80DEEA),
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier.padding(vertical = 8.dp).wrapContentWidth(Alignment.CenterHorizontally)
                    )
                }
            }
        }

        momentItems.forEach { item ->
            val isVideo = item.assetType.equals("video", ignoreCase = true) || item.modality.startsWith("VIDEO", ignoreCase = true)
            val isImageMod = item.modality.equals("image", ignoreCase = true) || item.modality.equals("VIDEO_FRAME", ignoreCase = true)
            val isAudio = !isVideo && item.modality.equals("audio", ignoreCase = true)

            var thumbBmp by remember(item.mediaPath, item.startMs) { mutableStateOf<Bitmap?>(null) }
            LaunchedEffect(item.mediaPath, item.mediaUri, item.startMs) {
                if (isImageMod || isVideo) {
                    thumbBmp = loadThumbnailBitmap(context, item, targetPx = 280)
                }
            }

            val modBadgeText = when {
                isVideo && isImageMod -> "VIDEO FRAME"
                isVideo -> "VIDEO AUDIO"
                isImageMod -> "PHOTO"
                isAudio -> "AUDIO"
                else -> item.modality.uppercase()
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
    if (previewPhoto != null) {
        PhotoPreviewDialog(
            item = previewPhoto!!,
            onDismiss = { previewPhoto = null }
        )
    }
}

@Composable
fun MediaMomentPlayerDialog(
    request: MediaMomentPlayerRequest,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val item = request.item
    val isVideo = item.assetType.equals("video", ignoreCase = true) || item.modality.startsWith("VIDEO", ignoreCase = true)
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
                        colors = ButtonDefaults.outlinedButtonColors(
                            backgroundColor = RoninThemeTokens.SurfaceLight,
                            contentColor = Color.White
                        )
                    ) {
                        Icon(Icons.Default.OpenInNew, null, tint = Color.White, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Open Externally", color = Color.White, fontSize = 11.sp)
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

    val targetPhotoCount = if (chatViewModel.excludeScreenshotsOnIndex) {
        chatViewModel.deviceNonScreenshotPhotoCount
    } else {
        chatViewModel.devicePhotoCount
    }
    val photoButtonLabel = if (targetPhotoCount > 0) "Index All $targetPhotoCount Photos" else "Index Photos"
    val audioButtonLabel = if (chatViewModel.deviceAudioCount > 0) "Index Audio (${chatViewModel.deviceAudioCount})" else "Index Audio"
    val videoButtonLabel = if (chatViewModel.deviceVideoCount > 0) "Index Videos (${chatViewModel.deviceVideoCount})" else "Index Videos"

    SettingsCardSection(title = "4.6 Multimodal Media Search (EmbeddingGemma 2)") {
        Text(
            "Runtime: LiteRT-LM 0.19.1 • 768D • Fast 70-Token Index",
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
        if (chatViewModel.deviceMediaCountsText.isNotBlank()) {
            Spacer(Modifier.height(2.dp))
            Text(
                chatViewModel.deviceMediaCountsText,
                color = RoninThemeTokens.TextMuted,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(Modifier.height(6.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "Skip Screenshots on auto-index",
                color = RoninThemeTokens.TextSecondary,
                fontSize = 11.sp
            )
            Switch(
                checked = chatViewModel.excludeScreenshotsOnIndex,
                onCheckedChange = { activity?.saveExcludeScreenshotsOnIndex(it) },
                colors = SwitchDefaults.colors(
                    checkedThumbColor = RoninThemeTokens.PrimaryEmerald,
                    checkedTrackColor = RoninThemeTokens.PrimaryEmeraldDim
                )
            )
        }

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
                colors = ButtonDefaults.outlinedButtonColors(
                    backgroundColor = RoninThemeTokens.ErrorCrimson.copy(alpha = 0.12f),
                    contentColor = RoninThemeTokens.ErrorCrimson
                )
            ) {
                Text("Cancel Media Indexing", color = RoninThemeTokens.ErrorCrimson, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        } else {
            Spacer(Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { activity?.launchManualMediaIndexPicker() },
                    modifier = Modifier.weight(1f).heightIn(min = 42.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = RoninThemeTokens.PrimaryEmeraldDim),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.AddPhotoAlternate, null, tint = RoninThemeTokens.PrimaryEmerald, modifier = Modifier.size(15.dp))
                    Spacer(Modifier.width(4.dp))
                    Text("Add Photos / Videos", color = RoninThemeTokens.PrimaryEmerald, fontSize = 10.sp, fontWeight = FontWeight.Bold)
                }
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.PHOTOS) },
                    modifier = Modifier.weight(1f).heightIn(min = 42.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text(photoButtonLabel, color = Color.White, fontSize = 10.sp)
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.AUDIO) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text(audioButtonLabel, color = Color.White, fontSize = 10.sp)
                }
                Button(
                    onClick = { activity?.startMediaIndexing(MediaIndexScope.VIDEO) },
                    modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text(videoButtonLabel, color = Color.White, fontSize = 10.sp)
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = { activity?.startMediaIndexing(MediaIndexScope.ALL) },
                modifier = Modifier.fillMaxWidth().heightIn(min = 40.dp),
                colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                shape = RoundedCornerShape(8.dp)
            ) {
                Icon(Icons.Default.AutoAwesome, null, tint = Color(0xFF00E5FF), modifier = Modifier.size(15.dp))
                Spacer(Modifier.width(6.dp))
                Text("Index All Media (Photos, Audio & Videos)", color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            }
            Spacer(Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(
                    onClick = { chatViewModel.showIndexedMediaManager = true },
                    modifier = Modifier.weight(1.3f).heightIn(min = 38.dp),
                    border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.5f)),
                    colors = ButtonDefaults.outlinedButtonColors(
                        backgroundColor = Color(0xFF64B5F6).copy(alpha = 0.12f),
                        contentColor = Color(0xFF64B5F6)
                    )
                ) {
                    Icon(Icons.Default.PhotoLibrary, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(15.dp))
                    Spacer(Modifier.width(5.dp))
                    Text("Manage (${chatViewModel.totalIndexedMediaCount})", color = Color(0xFF64B5F6), fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
                OutlinedButton(
                    onClick = { activity?.clearMediaSearchIndex() },
                    modifier = Modifier.weight(1f).heightIn(min = 38.dp),
                    border = BorderStroke(1.dp, RoninThemeTokens.Border),
                    colors = ButtonDefaults.outlinedButtonColors(
                        backgroundColor = RoninThemeTokens.SurfaceLight,
                        contentColor = RoninThemeTokens.TextSecondary
                    )
                ) {
                    Icon(Icons.Default.DeleteSweep, null, tint = RoninThemeTokens.TextSecondary, modifier = Modifier.size(15.dp))
                    Spacer(Modifier.width(4.dp))
                    Text("Clear All", color = RoninThemeTokens.TextSecondary, fontSize = 11.sp)
                }
            }
        }
    }
}

@Composable
fun IndexedMediaManagerDialog(
    chatViewModel: ChatViewModel,
    nativeEngine: NativeEngine,
    activity: MainActivity?,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    var filter by remember { mutableStateOf(MediaModalityFilter.ALL) }
    var entries by remember { mutableStateOf<List<IndexedMediaEntry>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var showManageMenu by remember { mutableStateOf(false) }
    var showFilterMenu by remember { mutableStateOf(false) }
    var selectionMode by remember { mutableStateOf(false) }
    val selectedPaths = remember { mutableStateListOf<String>() }
    var previewPhotoEntry by remember { mutableStateOf<MediaSearchResultItem?>(null) }

    val targetPhotoCount = if (chatViewModel.excludeScreenshotsOnIndex) {
        chatViewModel.deviceNonScreenshotPhotoCount
    } else {
        chatViewModel.devicePhotoCount
    }

    LaunchedEffect(filter, chatViewModel.mediaIndexVersion) {
        isLoading = true
        val loaded = withContext(Dispatchers.IO) {
            MediaSearchEngine.listIndexedMedia(nativeEngine, filter = filter, limit = 500)
        }
        entries = loaded
        val loadedSet = loaded.map { it.mediaPath }.toSet()
        selectedPaths.removeAll { it !in loadedSet }
        isLoading = false
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            color = RoninThemeTokens.Surface,
            shape = RoninThemeTokens.RadiusLg,
            border = BorderStroke(1.dp, RoninThemeTokens.Border),
            modifier = Modifier.fillMaxWidth(0.95f).fillMaxHeight(0.86f)
        ) {
            Column(modifier = Modifier.fillMaxSize().padding(14.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.PhotoLibrary, null, tint = Color(0xFF00E5FF), modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Instant Media Search",
                            color = RoninThemeTokens.TextPrimary,
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                    IconButton(onClick = onDismiss, modifier = Modifier.size(30.dp)) {
                        Icon(Icons.Default.Close, "Close", tint = Color.Gray)
                    }
                }

                Spacer(Modifier.height(10.dp))

                // Sub-header matching Google AI Edge Gallery's Manage + Filter controls
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            Icons.Default.CheckCircleOutline,
                            null,
                            tint = RoninThemeTokens.PrimaryEmerald,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(Modifier.width(4.dp))
                        Text(
                            "${entries.size} items",
                            color = RoninThemeTokens.TextPrimary,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1
                        )
                    }

                    Box {
                        Surface(
                            color = Color(0xFF1F4E79),
                            shape = RoundedCornerShape(18.dp),
                            modifier = Modifier.clickable { showManageMenu = true }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.ArrowDropDown, null, tint = Color.White, modifier = Modifier.size(16.dp))
                                Text("Manage", color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                            }
                        }
                        DropdownMenu(
                            expanded = showManageMenu,
                            onDismissRequest = { showManageMenu = false },
                            modifier = Modifier.background(Color(0xFF232635))
                        ) {
                            DropdownMenuItem(onClick = {
                                showManageMenu = false
                                activity?.launchManualMediaIndexPicker()
                            }) {
                                Text("Add more photos / videos", color = Color.White, fontSize = 13.sp)
                            }
                            DropdownMenuItem(onClick = {
                                showManageMenu = false
                                onDismiss()
                                activity?.startMediaIndexing(MediaIndexScope.PHOTOS)
                            }) {
                                val lbl = if (targetPhotoCount > 0) "Index all $targetPhotoCount photos" else "Index all photos"
                                Text(lbl, color = Color.White, fontSize = 13.sp)
                            }
                            DropdownMenuItem(onClick = {
                                showManageMenu = false
                                selectionMode = !selectionMode
                                if (!selectionMode) selectedPaths.clear()
                            }) {
                                Text(
                                    if (selectionMode) "Exit selection mode" else "Select items to remove",
                                    color = Color.White,
                                    fontSize = 13.sp
                                )
                            }
                            DropdownMenuItem(onClick = {
                                showManageMenu = false
                                selectionMode = false
                                selectedPaths.clear()
                                activity?.clearMediaSearchIndex()
                            }) {
                                Text("Remove all items", color = RoninThemeTokens.ErrorCrimson, fontSize = 13.sp)
                            }
                        }
                    }

                    Box {
                        Surface(
                            color = Color(0xFF2B2E3B),
                            shape = RoundedCornerShape(18.dp),
                            border = BorderStroke(1.dp, RoninThemeTokens.Border),
                            modifier = Modifier.clickable { showFilterMenu = true }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(filter.label, color = Color.White, fontSize = 12.sp)
                                Icon(Icons.Default.ArrowDropDown, null, tint = Color.White, modifier = Modifier.size(16.dp))
                            }
                        }
                        DropdownMenu(
                            expanded = showFilterMenu,
                            onDismissRequest = { showFilterMenu = false },
                            modifier = Modifier.background(Color(0xFF232635))
                        ) {
                            listOf(
                                MediaModalityFilter.ALL,
                                MediaModalityFilter.IMAGE,
                                MediaModalityFilter.VIDEO,
                                MediaModalityFilter.AUDIO
                            ).forEach { opt ->
                                DropdownMenuItem(onClick = {
                                    filter = opt
                                    showFilterMenu = false
                                }) {
                                    Text(opt.label, color = Color.White, fontSize = 13.sp)
                                }
                            }
                        }
                    }
                }

                if (chatViewModel.isIndexingMedia) {
                    Spacer(Modifier.height(8.dp))
                    LinearProgressIndicator(
                        progress = chatViewModel.mediaIndexFraction.coerceIn(0f, 1f),
                        modifier = Modifier.fillMaxWidth(),
                        color = Color(0xFF00E5FF),
                        backgroundColor = Color(0xFF25283D)
                    )
                    Spacer(Modifier.height(3.dp))
                    Text(
                        chatViewModel.mediaIndexProgressText,
                        color = Color(0xFF80DEEA),
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }

                Spacer(Modifier.height(10.dp))

                Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
                    when {
                        isLoading -> {
                            CircularProgressIndicator(
                                color = RoninThemeTokens.PrimaryEmerald,
                                modifier = Modifier.align(Alignment.Center)
                            )
                        }
                        entries.isEmpty() -> {
                            Column(
                                modifier = Modifier.align(Alignment.Center).padding(20.dp),
                                horizontalAlignment = Alignment.CenterHorizontally
                            ) {
                                Icon(Icons.Default.PhotoLibrary, null, tint = Color.Gray, modifier = Modifier.size(40.dp))
                                Spacer(Modifier.height(8.dp))
                                Text(
                                    "No indexed items in ${filter.label}",
                                    color = RoninThemeTokens.TextSecondary,
                                    fontSize = 13.sp,
                                    fontWeight = FontWeight.SemiBold
                                )
                                Spacer(Modifier.height(10.dp))
                                Button(
                                    onClick = { activity?.launchManualMediaIndexPicker() },
                                    colors = ButtonDefaults.buttonColors(backgroundColor = RoninThemeTokens.PrimaryEmeraldDim)
                                ) {
                                    Text("Add Photos / Videos", color = RoninThemeTokens.PrimaryEmerald, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                                }
                            }
                        }
                        else -> {
                            LazyVerticalGrid(
                                columns = GridCells.Fixed(3),
                                horizontalArrangement = Arrangement.spacedBy(4.dp),
                                verticalArrangement = Arrangement.spacedBy(4.dp),
                                modifier = Modifier.fillMaxSize()
                            ) {
                                items(entries, key = { it.mediaPath }) { entry ->
                                    val isSelected = entry.mediaPath in selectedPaths
                                    val isVideo = entry.modality.equals("video", ignoreCase = true)
                                    val isAudio = entry.modality.equals("audio", ignoreCase = true)
                                    val isImage = entry.modality.equals("image", ignoreCase = true)
                                    var tileBmp by remember(entry.mediaPath, entry.mediaUri) { mutableStateOf<Bitmap?>(null) }
                                    LaunchedEffect(entry.mediaPath, entry.mediaUri, entry.modality) {
                                        if (!isAudio) {
                                            tileBmp = loadMediaThumbnailBitmap(
                                                context = context,
                                                mediaPath = entry.mediaPath,
                                                mediaUri = entry.mediaUri,
                                                modality = if (isVideo) "image" else "image",
                                                startMs = 0L,
                                                targetPx = 280,
                                                assetType = if (isVideo) "video" else "image"
                                            )
                                        }
                                    }

                                    Box(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .aspectRatio(1f)
                                            .clip(RoundedCornerShape(6.dp))
                                            .background(Color(0xFF0E111A))
                                            .border(
                                                width = if (isSelected) 2.5.dp else 1.dp,
                                                color = if (isSelected) RoninThemeTokens.PrimaryEmerald else RoninThemeTokens.Border,
                                                shape = RoundedCornerShape(6.dp)
                                            )
                                            .clickable {
                                                if (selectionMode) {
                                                    if (isSelected) selectedPaths.remove(entry.mediaPath)
                                                    else selectedPaths.add(entry.mediaPath)
                                                } else if (isImage) {
                                                    previewPhotoEntry = MediaSearchResultItem(
                                                        mediaPath = entry.mediaPath,
                                                        mediaUri = entry.mediaUri,
                                                        modality = "image",
                                                        displayName = entry.displayName,
                                                        startMs = 0L,
                                                        endMs = 0L,
                                                        durationMs = 0L,
                                                        score = 1.0f,
                                                        modelId = entry.modelId,
                                                        assetType = "image"
                                                    )
                                                } else {
                                                    val playItem = MediaSearchResultItem(
                                                        mediaPath = entry.mediaPath,
                                                        mediaUri = entry.mediaUri,
                                                        modality = if (isVideo) "image" else "audio",
                                                        displayName = entry.displayName,
                                                        startMs = 0L,
                                                        endMs = entry.durationMs,
                                                        durationMs = entry.durationMs,
                                                        score = 1.0f,
                                                        modelId = entry.modelId,
                                                        assetType = if (isVideo) "video" else "audio"
                                                    )
                                                    chatViewModel.activeMediaPlayerRequest = MediaMomentPlayerRequest(playItem, 0L)
                                                }
                                            },
                                        contentAlignment = Alignment.Center
                                    ) {
                                        if (tileBmp != null) {
                                            Image(
                                                bitmap = tileBmp!!.asImageBitmap(),
                                                contentDescription = entry.displayName,
                                                contentScale = ContentScale.Crop,
                                                modifier = Modifier.fillMaxSize()
                                            )
                                        } else {
                                            Column(
                                                horizontalAlignment = Alignment.CenterHorizontally,
                                                modifier = Modifier.padding(6.dp)
                                            ) {
                                                Icon(
                                                    imageVector = when {
                                                        isVideo -> Icons.Default.Movie
                                                        isAudio -> Icons.Default.GraphicEq
                                                        else -> Icons.Default.Image
                                                    },
                                                    contentDescription = entry.displayName,
                                                    tint = if (isAudio) Color(0xFFCE93D8) else if (isVideo) Color(0xFF64B5F6) else RoninThemeTokens.PrimaryEmerald,
                                                    modifier = Modifier.size(26.dp)
                                                )
                                                if (isAudio) {
                                                    Spacer(Modifier.height(4.dp))
                                                    Text(
                                                        entry.displayName,
                                                        color = Color.White,
                                                        fontSize = 9.sp,
                                                        maxLines = 2,
                                                        overflow = TextOverflow.Ellipsis
                                                    )
                                                }
                                            }
                                        }

                                        Box(
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .height(24.dp)
                                                .align(Alignment.BottomCenter)
                                                .background(
                                                    Brush.verticalGradient(
                                                        colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.78f))
                                                    )
                                                )
                                        )

                                        Row(
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .align(Alignment.BottomCenter)
                                                .padding(horizontal = 5.dp, vertical = 3.dp),
                                            horizontalArrangement = Arrangement.SpaceBetween,
                                            verticalAlignment = Alignment.CenterVertically
                                        ) {
                                            Text(
                                                "Analyzed",
                                                color = Color.White,
                                                fontSize = 10.sp,
                                                fontWeight = FontWeight.Medium
                                            )
                                            if (entry.durationMs > 0L) {
                                                Text(
                                                    "▶ ${MediaSearchEngine.formatTimestampMs(entry.durationMs)}",
                                                    color = Color.White,
                                                    fontSize = 9.sp,
                                                    fontFamily = FontFamily.Monospace
                                                )
                                            }
                                        }

                                        if (selectionMode) {
                                            Box(
                                                modifier = Modifier
                                                    .align(Alignment.TopEnd)
                                                    .padding(4.dp)
                                                    .size(20.dp)
                                                    .clip(CircleShape)
                                                    .background(
                                                        if (isSelected) RoninThemeTokens.PrimaryEmerald
                                                        else Color.Black.copy(alpha = 0.55f)
                                                    )
                                                    .border(1.dp, Color.White, CircleShape),
                                                contentAlignment = Alignment.Center
                                            ) {
                                                if (isSelected) {
                                                    Icon(Icons.Default.Check, null, tint = Color.Black, modifier = Modifier.size(13.dp))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (selectionMode) {
                    Spacer(Modifier.height(10.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = {
                                val toRemove = selectedPaths.toList()
                                activity?.removeIndexedMediaPaths(toRemove) {
                                    selectionMode = false
                                    selectedPaths.clear()
                                }
                            },
                            enabled = selectedPaths.isNotEmpty(),
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(backgroundColor = RoninThemeTokens.ErrorCrimson)
                        ) {
                            Text("Remove Selected (${selectedPaths.size})", color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        }
                        OutlinedButton(
                            onClick = {
                                selectionMode = false
                                selectedPaths.clear()
                            },
                            border = BorderStroke(1.dp, RoninThemeTokens.Border),
                            colors = ButtonDefaults.outlinedButtonColors(
                                backgroundColor = RoninThemeTokens.SurfaceLight,
                                contentColor = Color.White
                            )
                        ) {
                            Text("Cancel", color = Color.White, fontSize = 12.sp)
                        }
                    }
                }
            }
        }
    }

    if (previewPhotoEntry != null) {
        PhotoPreviewDialog(
            item = previewPhotoEntry!!,
            onDismiss = { previewPhotoEntry = null }
        )
    }
}
