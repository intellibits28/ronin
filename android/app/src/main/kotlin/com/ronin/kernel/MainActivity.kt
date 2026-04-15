package com.ronin.kernel

import android.os.Bundle
import android.widget.Toast
import android.content.Context
import android.app.ActivityManager
import android.os.BatteryManager
import android.content.IntentFilter
import android.net.wifi.WifiManager
import android.bluetooth.BluetoothAdapter
import android.location.LocationManager
import android.hardware.camera2.CameraManager
import androidx.compose.material.icons.filled.Settings
import androidx.compose.ui.text.font.FontFamily
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
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.runtime.snapshots.SnapshotStateList

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<String>()
    val reasoningLogs = mutableStateListOf<String>()
    var showSysInfo by mutableStateOf(false)
    var lmkPressure by mutableStateOf(0)
    var stability by mutableStateOf(1.0f)
    var l1Count by mutableStateOf(0)
    var l2Count by mutableStateOf(0)
    var l3Count by mutableStateOf(0)
    
    // Health metrics
    var temperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)
}

class MainActivity : ComponentActivity() {
    private val nativeEngine = NativeEngine()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        nativeEngine.initializeKernel(filesDir.absolutePath)
        nativeEngine.setEngineInstance()

        setupHardwareCallbacks()
        checkAndRequestStoragePermission()
        checkAndRequestHardwarePermissions()

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            RoninChatUI(nativeEngine, chatViewModel)
        }
    }

    private fun checkAndRequestHardwarePermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(android.Manifest.permission.BLUETOOTH_CONNECT)
            permissions.add(android.Manifest.permission.BLUETOOTH_SCAN)
        }

        permissions.add(android.Manifest.permission.ACCESS_FINE_LOCATION)
        permissions.add(android.Manifest.permission.CAMERA)

        val missing = permissions.filter {
            checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED
        }

        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), 1001)
        }
    }

    private fun setupHardwareCallbacks() {
        nativeEngine.executeHardwareAction = { nodeId, state ->
            var toolName = ""
            when (nodeId) {
                4 -> toolName = "Flashlight"
                5 -> toolName = "GPS"
                6 -> toolName = "WiFi"
                7 -> toolName = "Bluetooth"
            }

            // Show immediate feedback to user
            runOnUiThread {
                Toast.makeText(this, "Kernel: Initiating $toolName toggle...", Toast.LENGTH_SHORT).show()
            }

            var success = false
            try {
                when (nodeId) {
                    4 -> {
                        val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
                        val cameraId = cameraManager.cameraIdList[0]
                        cameraManager.setTorchMode(cameraId, state) 
                        success = true
                    }
                    5 -> {
                        val locationManager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
                        success = locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    }
                    6 -> {
                        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                        @Suppress("DEPRECATION")
                        success = wifiManager.setWifiEnabled(state)
                    }
                    7 -> {
                        val bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
                        if (bluetoothAdapter != null) {
                            @Suppress("MissingPermission")
                            success = if (bluetoothAdapter.isEnabled == state) true 
                                      else if (state) bluetoothAdapter.enable() 
                                      else bluetoothAdapter.disable()
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "CRITICAL ERROR: Hardware Node $nodeId failed", e)
                success = false
            }

            if (success) {
                Log.i("RoninUI", "System: $toolName set to ${if (state) "ON" else "OFF"}")
            }
            success
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
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel = viewModel()) {
    var inputText by remember { mutableStateOf("") }
    val messages = chatViewModel.messages
    val reasoningLogs = chatViewModel.reasoningLogs
    val context = LocalContext.current
    val scaffoldState = rememberScaffoldState()
    
    // Scroll States for Auto-scroll
    val chatListState = rememberLazyListState()
    val reasoningListState = rememberLazyListState()
    
    // Auto-scroll logic for Chat
    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            chatListState.animateScrollToItem(messages.size - 1)
        }
    }

    // Auto-scroll logic for Reasoning Console (Newest thoughts at index 0)
    LaunchedEffect(reasoningLogs.size) {
        if (reasoningLogs.isNotEmpty()) {
            reasoningListState.animateScrollToItem(0)
        }
    }

    // Kernel State Metrics (Synchronized from ViewModel)
    val lmkPressure = chatViewModel.lmkPressure
    val stability = chatViewModel.stability
    val l1Count = chatViewModel.l1Count
    val l2Count = chatViewModel.l2Count
    val l3Count = chatViewModel.l3Count
    
    val scope = rememberCoroutineScope()

    // Sync History from Kernel (Source of Truth)
    LaunchedEffect(Unit) {
        if (messages.isEmpty()) {
            val history = engine.getChatHistoryAsync()
            history.forEach { (role, content) ->
                if (role == "user") messages.add("User: $content")
                else messages.add("Ronin: $content")
            }
        }
    }

    // Kernel Polling Loop (System Monitoring v3.9)
    LaunchedEffect(Unit) {
        val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memInfo = ActivityManager.MemoryInfo()
        
        while (true) {
            // Fetch RAM
            activityManager.getMemoryInfo(memInfo)
            val totalRAM = memInfo.totalMem / (1024f * 1024f * 1024f)
            val availableRAM = memInfo.availMem / (1024f * 1024f * 1024f)
            val usedRAM = totalRAM - availableRAM
            
            // Fetch Temp
            val intent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val temp = intent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.div(10f) ?: 0f
            
            chatViewModel.temperature = temp
            chatViewModel.ramUsedGB = usedRAM
            chatViewModel.ramTotalGB = totalRAM
            
            // Push to C++ Kernel
            val highPressure = engine.updateSystemHealth(temp, usedRAM, totalRAM)
            if (highPressure) {
                reasoningLogs.add(0, "> High Memory Pressure: Pruning KV Cache.")
            }

            chatViewModel.lmkPressure = engine.getLMKPressure()
            chatViewModel.stability = (100 - chatViewModel.lmkPressure) / 100.0f
            
            delay(2500)
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        topBar = {
            TopAppBar(
                title = { 
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Ronin Kernel v3.9.4-STABLE")
                        Spacer(Modifier.width(8.dp))
                        StabilityHeartbeat(lmkPressure)
                    }
                },
                actions = {
                    IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) {
                        Icon(Icons.Default.Settings, contentDescription = "Toggle Info", tint = if (chatViewModel.showSysInfo) Color.Cyan else Color.Gray)
                    }
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
                state = chatListState,
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
            ReasoningConsole(reasoningLogs, reasoningListState)

            // 4. Input Area
            ChatInput(
                value = inputText,
                onValueChange = { inputText = it },
                onSend = {
                    if (inputText.isNotBlank()) {
                        messages.add("User: $inputText")
                        val currentInput = inputText
                        inputText = ""
                        
                        // Reset context counters for stable release v3.9
                        chatViewModel.l1Count = 0
                        chatViewModel.l2Count = 0
                        chatViewModel.l3Count = 0
                        
                        scope.launch {
                            val cleanInput = currentInput.trim().lowercase()
                            val greetings = listOf("hi", "hello", "hey", "mingalaba")
                            
                            if (greetings.any { cleanInput == it }) {
                                reasoningLogs.add(0, "> Dispatching to Chat: Greeting detected.")
                            } else {
                                val isSearch = currentInput.contains("search", ignoreCase = true) || 
                                               currentInput.contains("find", ignoreCase = true) ||
                                               currentInput.contains("locate", ignoreCase = true)
                                               
                                if (isSearch) {
                                    reasoningLogs.add(0, "Kernel Decision: Reasoning v3.9.1-STABLE bypass activated.")
                                } else {
                                    reasoningLogs.add(0, "Thompson Sampling: Selected 'Reasoning_Engine' for input.")
                                }
                            }
                            
                            val kernelOutput = engine.processInputAsync(currentInput)
                            
                            // Check if a tool was engaged
                            if (kernelOutput.contains("Switching on Flashlight")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: Flashlight engaged.")
                            } else if (kernelOutput.contains("Locating device")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: GPS engaged.")
                            } else if (kernelOutput.contains("System: WiFi")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: WiFi engaged.")
                            } else if (kernelOutput.contains("System: Bluetooth")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: Bluetooth engaged.")
                            }
                            
                            // Re-sync from Kernel history to ensure perfect consistency
                            val history = engine.getChatHistoryAsync()
                            messages.clear()
                            history.forEach { (role, content) ->
                                if (role == "user") messages.add("User: $content")
                                else messages.add("Ronin: $content")
                            }
                        }
                    }
                }
            )
            
            if (chatViewModel.showSysInfo) {
                SystemHealthOverlay(chatViewModel.temperature, chatViewModel.ramUsedGB, chatViewModel.ramTotalGB)
            }
        }
    }
}

@Composable
fun SystemHealthOverlay(temp: Float, used: Float, total: Float) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = Color(0xFF121212),
        elevation = 4.dp
    ) {
        Row(
            modifier = Modifier.padding(8.dp).fillMaxWidth(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "${"%.1f".format(temp)}°C | RAM: ${"%.2f".format(used)}/${"%.2f".format(total)} GB",
                color = Color.Green,
                fontSize = 12.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
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
fun ReasoningConsole(logs: List<String>, scrollState: LazyListState) {
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
                LazyColumn(state = scrollState) {
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
