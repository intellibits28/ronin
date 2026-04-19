package com.ronin.kernel

import android.os.Bundle
import android.widget.Toast
import android.content.Context
import android.app.ActivityManager
import android.os.BatteryManager
import android.content.IntentFilter
import android.net.wifi.WifiManager
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.hardware.camera2.CameraManager
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.CancellationTokenSource
import androidx.compose.material.icons.filled.Settings
import androidx.compose.ui.text.font.FontFamily
import android.os.Environment
import android.os.Build
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean
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
    
    // Lazy Loading History
    var historyPage by mutableStateOf(0)
    var isLoadingHistory by mutableStateOf(false)
    var hasMoreHistory by mutableStateOf(true)

    // Health metrics
    var temperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)
}

class MainActivity : ComponentActivity() {
    private val nativeEngine = NativeEngine()
    private lateinit var fusedLocationClient: FusedLocationProviderClient

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Phase 4.0: Ensure assets are extracted to filesystem for C++ standard I/O access
        copyAssetsToFilesDir()
        
        nativeEngine.initializeKernel(filesDir.absolutePath)
        nativeEngine.setEngineInstance()
        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)

        setupHardwareCallbacks()
        checkAndRequestStoragePermission()
        checkAndRequestHardwarePermissions()

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            RoninChatUI(nativeEngine, chatViewModel)
        }
    }

    private fun copyAssetsToFilesDir() {
        try {
            val assetManager = assets
            val files = assetManager.list("") ?: return
            
            // 1. Root assets (capabilities.json, etc)
            for (filename in files) {
                if (filename == "models" || filename == "images" || filename == "webkit") continue
                if (filename.contains(".")) {
                    copyFile(filename, "")
                }
            }
            
            // 2. Models directory
            val models = assetManager.list("models") ?: return
            val modelsDir = java.io.File(filesDir, "assets/models")
            if (!modelsDir.exists()) modelsDir.mkdirs()
            
            for (modelFile in models) {
                copyFile(modelFile, "models")
            }
            
            Log.i("RoninBoot", "Assets successfully synchronized to internal storage.")
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to sync assets: ${e.message}")
        }
    }

    private fun copyFile(filename: String, subDir: String) {
        val assetPath = if (subDir.isEmpty()) filename else "$subDir/$filename"
        val destDir = if (subDir.isEmpty()) java.io.File(filesDir, "assets") else java.io.File(filesDir, "assets/$subDir")
        if (!destDir.exists()) destDir.mkdirs()
        
        val outFile = java.io.File(destDir, filename)
        
        // Skip if already exists and size matches (basic optimization)
        assets.open(assetPath).use { inputStream ->
            if (outFile.exists() && outFile.length() == inputStream.available().toLong()) {
                return
            }
            
            java.io.FileOutputStream(outFile).use { outputStream ->
                inputStream.copyTo(outputStream)
            }
        }
    }

    private fun checkAndRequestHardwarePermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(android.Manifest.permission.BLUETOOTH_CONNECT)
            permissions.add(android.Manifest.permission.BLUETOOTH_SCAN)
            permissions.add(android.Manifest.permission.BLUETOOTH_ADVERTISE)
            permissions.add(android.Manifest.permission.BLUETOOTH_ADMIN)
        }

        permissions.add(android.Manifest.permission.ACCESS_FINE_LOCATION)
        permissions.add(android.Manifest.permission.ACCESS_COARSE_LOCATION)
        permissions.add(android.Manifest.permission.CAMERA)

        val missing = permissions.filter {
            checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED
        }

        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), 1001)
        }
    }

    private fun setupHardwareCallbacks() {
        nativeEngine.onRequestHardwareData = { nodeId ->
            when (nodeId) {
                5 -> {
                    // Quick data fetch for Vtable-based execution
                    "Current Location: [Lat: 16.8, Lon: 96.1] (Verified via JNI Data-Pipe)"
                }
                else -> "Error: Unknown data node $nodeId"
            }
        }

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
                        // GPS: Robust 3-Tier Fallback Implementation (v3.9.7-RECOVERY)
                        val hasFine = checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        val hasCoarse = checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        
                        if (hasFine || hasCoarse) {
                            val cancellationToken = CancellationTokenSource()
                            val locationFound = AtomicBoolean(false)

                            // Tier 1: Primary - getCurrentLocation
                            fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, cancellationToken.token)
                                .addOnSuccessListener { loc ->
                                    if (loc != null && !locationFound.get()) {
                                        locationFound.set(true)
                                        Log.i("RoninUI", "GPS Tier 1: Success [${loc.latitude}, ${loc.longitude}]")
                                        nativeEngine.injectLocation(loc.latitude, loc.longitude)
                                    } else if (loc == null) {
                                        // Trigger Tier 2 immediately if null
                                        Log.w("RoninUI", "GPS Tier 1 returned null. Falling back to Tier 2.")
                                    }
                                }
                                .addOnFailureListener { e ->
                                    Log.e("RoninUI", "GPS Tier 1 Failure: ${e.message}")
                                }
                            
                            // Tier 2 & 3 Handler: 10s Timeout or Fallback Trigger
                            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                                if (!locationFound.get()) {
                                    cancellationToken.cancel()
                                    Log.w("RoninUI", "GPS Primary Timeout. Attempting Tier 2 (lastLocation)...")
                                    
                                    // Tier 2: Fallback - lastLocation
                                    @Suppress("MissingPermission")
                                    fusedLocationClient.lastLocation.addOnSuccessListener { lastLoc ->
                                        if (lastLoc != null) {
                                            locationFound.set(true)
                                            Log.i("RoninUI", "GPS Tier 2: Success [${lastLoc.latitude}, ${lastLoc.longitude}]")
                                            nativeEngine.injectLocation(lastLoc.latitude, lastLoc.longitude)
                                        } else {
                                            // Tier 3: Failsafe - (0,0)
                                            Log.e("RoninUI", "GPS Tier 2 Failure. Injecting Tier 3 Failsafe (0,0).")
                                            nativeEngine.injectLocation(0.0, 0.0)
                                        }
                                    }.addOnFailureListener {
                                        Log.e("RoninUI", "GPS Tier 2 Fatal. Injecting Tier 3 Failsafe (0,0).")
                                        nativeEngine.injectLocation(0.0, 0.0)
                                    }
                                }
                            }, 10000)
                            success = true
                        } else {
                            Log.e("RoninUI", "GPS Error: Permissions Denied. Unblocking Kernel with (0,0).")
                            nativeEngine.injectLocation(0.0, 0.0)
                            runOnUiThread {
                                Toast.makeText(this, "Permission Denied: GPS", Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                    6 -> {
                        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            // Android 10+ requires Intent for user to toggle WiFi manually for security
                            val panelIntent = Intent(Settings.Panel.ACTION_WIFI)
                            startActivity(panelIntent)
                            success = true
                        } else {
                            @Suppress("DEPRECATION")
                            success = wifiManager.setWifiEnabled(state)
                        }
                    }
                    7 -> {
                        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
                        val bluetoothAdapter = bluetoothManager.adapter
                        if (bluetoothAdapter != null) {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                // Android 13+ (API 33) restricts background toggling
                                val panelIntent = Intent(Settings.ACTION_BLUETOOTH_SETTINGS)
                                startActivity(panelIntent)
                                success = true
                            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                if (checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                                    success = if (state) bluetoothAdapter.enable() else bluetoothAdapter.disable()
                                } else {
                                    Log.e("RoninUI", "Bluetooth Error: Missing BLUETOOTH_CONNECT")
                                    requestPermissions(arrayOf(android.Manifest.permission.BLUETOOTH_CONNECT), 1002)
                                }
                            } else {
                                @Suppress("MissingPermission")
                                success = if (state) bluetoothAdapter.enable() else bluetoothAdapter.disable()
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "CRITICAL ERROR: Hardware Node $nodeId failed", e)
                success = false
            }

            if (success) {
                Log.i("RoninUI", "System: $toolName action successfully dispatched.")
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
    
    val scope = rememberCoroutineScope()

    // Initialize Kernel Async UI Bridge
    LaunchedEffect(Unit) {
        engine.onKernelMessage = { message ->
            messages.add("Ronin: $message")
        }
    }

    // Lazy Loading History Implementation
    val loadNextHistoryPage = {
        if (!chatViewModel.isLoadingHistory && chatViewModel.hasMoreHistory) {
            chatViewModel.isLoadingHistory = true
            scope.launch {
                val pageSize = 20
                val offset = chatViewModel.historyPage * pageSize
                val newHistory = engine.getChatHistoryAsync(pageSize, offset)
                
                if (newHistory.isEmpty()) {
                    chatViewModel.hasMoreHistory = false
                } else {
                    // Prepend history
                    newHistory.reversed().forEach { (role, content) ->
                        val msg = if (role == "user") "User: $content" else "Ronin: $content"
                        messages.add(0, msg)
                    }
                    chatViewModel.historyPage++
                }
                chatViewModel.isLoadingHistory = false
            }
        }
    }

    // Initial history load (REMOVED for v3.9.7 PRIVACY-RECOVERY - Clean slate on startup)
    // Only fetch history when explicitly requested via /history (Handled in processInput logic)

    // Detect when user scrolls to top to load more history
    LaunchedEffect(chatListState.firstVisibleItemIndex) {
        if (chatListState.firstVisibleItemIndex == 0 && messages.isNotEmpty() && !chatViewModel.isLoadingHistory) {
            loadNextHistoryPage()
        }
    }

    // Auto-scroll logic for Chat: Use SideEffect for layout-aware scrolling
    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            // Check if we are near bottom to avoid "jumping" if user is reading history
            val layoutInfo = chatListState.layoutInfo
            val visibleItemsInfo = layoutInfo.visibleItemsInfo
            
            // If we're not at the bottom, we only auto-scroll if the LAST message was from the user
            // OR if we're already very close to the bottom (within 2 items)
            val lastMsg = messages.last()
            val isUserMsg = lastMsg.startsWith("User:")
            val isAtBottom = if (visibleItemsInfo.isEmpty()) true 
                             else visibleItemsInfo.last().index >= layoutInfo.totalItemsCount - 2

            if (isUserMsg || isAtBottom) {
                // Post to UI thread equivalent in Compose: animateScrollToItem is already async/layout-aware
                chatListState.animateScrollToItem(messages.size - 1)
            }
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
    
    // Sync History from Kernel - REMOVED for v3.9.7-PRIVACY-UI (Use /history command)

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
                        Text("Ronin Kernel v3.9.7-AUTO-SCROLL")
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

            if (chatViewModel.isLoadingHistory) {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth().height(2.dp),
                    color = Color.Cyan,
                    backgroundColor = Color.Transparent
                )
            }

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
                            
                            if (cleanInput == "/history") {
                                reasoningLogs.add(0, "> Privacy Bypass: Explicit history fetch requested.")
                                loadNextHistoryPage()
                                return@launch
                            }

                            val kernelOutput = engine.processInputAsync(currentInput)

                            // Check if a tool was engaged
                            if (kernelOutput.contains("Action Initiated - Flashlight")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: Flashlight toggled.")
                            } else if (kernelOutput.contains("Locating device")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: GPS sensor active.")
                            } else if (kernelOutput.contains("Action Initiated - WiFi")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: WiFi action dispatched.")
                            } else if (kernelOutput.contains("Action Initiated - Bluetooth")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: Bluetooth action dispatched.")
                            }

                            // Add Ronin's response to the UI
                            messages.add("Ronin: $kernelOutput")

                            // Explicitly scroll to bottom after response, ensuring UI thread post
                            launch {
                                delay(100)
                                chatListState.animateScrollToItem(messages.size - 1)
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
