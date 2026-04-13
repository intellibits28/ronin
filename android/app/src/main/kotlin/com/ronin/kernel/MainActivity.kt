package com.ronin.kernel

import android.os.Bundle
import android.os.Environment
import android.os.Build
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private val nativeEngine = NativeEngine()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        nativeEngine.initializeKernel(filesDir.absolutePath)
        
        checkAndRequestStoragePermission()

        setContent {
            RoninChatUI(nativeEngine)
        }
    }

    private fun checkAndRequestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                val uri = Uri.fromParts("package", packageName, null)
                intent.data = uri
                try {
                    startActivity(intent)
                } catch (e: Exception) {
                    // Fallback for older devices or if the intent fails
                    val genericIntent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(genericIntent)
                }
            }
        }
    }
}

@Composable
fun RoninChatUI(engine: NativeEngine) {
    var inputText by remember { mutableStateOf("") }
    val messages = remember { mutableStateListOf<String>() }
    val reasoningLogs = remember { mutableStateListOf<String>() }
    
    // Kernel State Metrics
    var lmkPressure by remember { mutableStateOf(0) }
    var stability by remember { mutableStateOf(1.0f) } // 0.0 to 1.0
    var l1Count by remember { mutableStateOf(12) } // Pinned
    var l2Count by remember { mutableStateOf(45) } // Compressed
    var l3Count by remember { mutableStateOf(128) } // Deep-store
    
    val scope = rememberCoroutineScope()

    // Kernel Polling Loop
    LaunchedEffect(Unit) {
        while (true) {
            lmkPressure = engine.getLMKPressure()
            stability = (100 - lmkPressure) / 100.0f
            // Randomly simulate cache movement for the demo
            if (lmkPressure > 70) {
                reasoningLogs.add(0, "KV-cache compressed 40% due to LMK pressure.")
                l2Count += 5
                l1Count -= 2
            }
            delay(3000)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { 
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Ronin Kernel v2.5-LIVE-SEARCH")
                        Spacer(Modifier.width(8.dp))
                        StabilityHeartbeat(lmkPressure)
                    }
                },
                actions = {
                    StabilityMeter(stability)
                },
                backgroundColor = Color(0xFF121212),
                contentColor = Color.White,
                elevation = 8.dp
            )
        },
        backgroundColor = Color(0xFF1A1A1A)
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
        ) {
            // 1. Context Timeline (L1, L2, L3 Visualization)
            ContextTimeline(l1Count, l2Count, l3Count)

            // 2. Chat History
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.Bottom
            ) {
                items(messages) { msg ->
                    ChatBubble(msg)
                }
            }

            // 3. Reasoning Console (Expandable)
            ReasoningConsole(reasoningLogs)

            // 4. Input Area
            ChatInput(
                value = inputText,
                onValueChange = { inputText = it },
                onSend = {
                    if (inputText.isNotBlank()) {
                        messages.add("User: $inputText")
                        val currentInput = inputText
                        inputText = ""
                        scope.launch {
                            reasoningLogs.add(0, "Kernel Decision: Reasoning v2.5 live bypass activated.")
                            val kernelOutput = engine.processInputAsync(currentInput)
                            messages.add("Ronin: $kernelOutput")
                        }
                    }
                }
            )
        }
    }
}

@Composable
fun StabilityHeartbeat(pressure: Int) {
    val color = when {
        pressure < 30 -> Color.Green
        pressure < 70 -> Color.Yellow
        else -> Color.Red
    }
    Icon(
        imageVector = Icons.Default.Favorite,
        contentDescription = "Heartbeat",
        tint = color,
        modifier = Modifier.size(18.dp)
    )
}

@Composable
fun ContextTimeline(l1: Int, l2: Int, l3: Int) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF252525))
            .padding(8.dp),
        horizontalArrangement = Arrangement.SpaceEvenly
    ) {
        TimelineZone("L1 (Active)", l1, Color.Cyan)
        TimelineZone("L2 (Compressed)", l2, Color.Yellow)
        TimelineZone("L3 (Deep)", l3, Color.Magenta)
    }
}

@Composable
fun TimelineZone(label: String, count: Int, color: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, style = MaterialTheme.typography.caption, color = Color.Gray)
        Text("$count items", color = color, fontSize = 12.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.Bold)
    }
}

@Composable
fun ReasoningConsole(logs: List<String>) {
    var expanded by remember { mutableStateOf(false) }
    
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(8.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF222222))
            .animateContentSize()
    ) {
        Row(
            modifier = Modifier
                .clickable { expanded = !expanded }
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (expanded) Icons.Default.KeyboardArrowDown else Icons.Default.KeyboardArrowUp,
                contentDescription = "Expand",
                tint = Color.Gray
            )
            Spacer(Modifier.width(8.dp))
            Text("Reasoning Console", color = Color.Gray, fontSize = 12.sp)
        }

        if (expanded) {
            Box(modifier = Modifier.height(120.dp).padding(horizontal = 12.dp, vertical = 4.dp)) {
                LazyColumn {
                    items(logs) { log ->
                        Text(
                            text = "> $log",
                            color = Color(0xFF00FF00),
                            fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(vertical = 2.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun ChatBubble(message: String) {
    val isUser = message.startsWith("User:")
    val text = message.substringAfter(": ")
    
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        contentAlignment = if (isUser) Alignment.CenterEnd else Alignment.CenterStart
    ) {
        Text(
            text = text,
            modifier = Modifier
                .clip(RoundedCornerShape(12.dp))
                .background(if (isUser) Color(0xFF3D5AFE) else Color(0xFF333333))
                .padding(12.dp),
            color = Color.White,
            fontSize = 14.sp
        )
    }
}

@Composable
fun ChatInput(value: String, onValueChange: (String) -> Unit, onSend: () -> Unit) {
    Surface(elevation = 8.dp, color = Color(0xFF121212)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextField(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier.weight(1f),
                colors = TextFieldDefaults.textFieldColors(
                    backgroundColor = Color(0xFF252525),
                    textColor = Color.White,
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent
                ),
                shape = RoundedCornerShape(24.dp),
                placeholder = { Text("Ask Ronin...", color = Color.Gray) }
            )
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = onSend,
                shape = RoundedCornerShape(24.dp),
                colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF3D5AFE))
            ) {
                Text("Send", color = Color.White)
            }
        }
    }
}

@Composable
fun StabilityMeter(stability: Float) {
    val color = when {
        stability > 0.7f -> Color.Green
        stability > 0.4f -> Color.Yellow
        else -> Color.Red
    }

    Column(
        horizontalAlignment = Alignment.End,
        modifier = Modifier.padding(end = 16.dp)
    ) {
        Text("Stability", style = MaterialTheme.typography.caption, color = Color.White)
        LinearProgressIndicator(
            progress = stability,
            color = color,
            backgroundColor = Color.DarkGray,
            modifier = Modifier.width(100.dp)
        )
    }
}
