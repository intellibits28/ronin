package com.ronin.kernel

import android.os.Bundle
import android.widget.Toast
import android.content.Context
import android.app.ActivityManager
import android.os.BatteryManager
import android.content.IntentFilter
import android.os.Build
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Send
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewmodel.compose.viewModel
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.tasks.CancellationTokenSource
import java.util.concurrent.atomic.AtomicBoolean
import java.io.File
import java.io.FileOutputStream

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<String>()
    var lmkPressure by mutableStateOf(0)
    var stability by mutableStateOf(1.0f)
    var temperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)
    var isLoadingHistory by mutableStateOf(false)
    var hasMoreHistory by mutableStateOf(true)
    var historyPage by mutableStateOf(0)
}

class MainActivity : ComponentActivity() {

    private val nativeEngine = NativeEngine()
    private lateinit var fusedLocationClient: FusedLocationProviderClient

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Phase 4.0: Use External Files Dir for LMK Survival debugging on unrooted devices
        val baseDir = getExternalFilesDir(null) ?: filesDir
        
        // Ensure assets are extracted to filesystem for C++ standard I/O access
        copyAssetsToFilesDir(baseDir)
        
        nativeEngine.initializeKernel(baseDir.absolutePath)
        nativeEngine.setCameraManager(this)
        nativeEngine.setEngineInstance()
        nativeEngine.hydrate()
        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)

        // Rule 4 Compliance: Register for push-based OS memory callbacks
        registerComponentCallbacks(nativeEngine)

        setupHardwareCallbacks()
        checkAndRequestStoragePermission()
        checkAndRequestHardwarePermissions()

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            RoninChatUI(nativeEngine, chatViewModel)
        }
    }

    private fun copyAssetsToFilesDir(targetDir: File) {
        try {
            val assetManager = assets
            val files = assetManager.list("") ?: return
            
            for (filename in files) {
                if (filename == "models" || filename == "images" || filename == "webkit") continue
                if (filename.contains(".")) {
                    copyFile(filename, "", targetDir)
                }
            }
            
            val models = assetManager.list("models") ?: return
            val modelsDir = File(targetDir, "assets/models")
            if (!modelsDir.exists()) modelsDir.mkdirs()
            
            for (modelFile in models) {
                copyFile(modelFile, "models", targetDir)
            }
            
            Log.i("RoninBoot", "Assets successfully synchronized to: ${targetDir.absolutePath}")
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to sync assets: ${e.message}")
        }
    }

    private fun copyFile(filename: String, subDir: String, targetDir: File) {
        val assetPath = if (subDir.isEmpty()) filename else "$subDir/$filename"
        val destDir = if (subDir.isEmpty()) File(targetDir, "assets") else File(targetDir, "assets/$subDir")
        if (!destDir.exists()) destDir.mkdirs()
        
        val outFile = File(destDir, filename)
        assets.open(assetPath).use { inputStream ->
            if (outFile.exists() && outFile.length() == inputStream.available().toLong()) {
                return
            }
            FileOutputStream(outFile).use { outputStream ->
                inputStream.copyTo(outputStream)
            }
        }
    }

    private fun checkAndRequestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(android.Manifest.permission.WRITE_EXTERNAL_STORAGE, android.Manifest.permission.READ_EXTERNAL_STORAGE), 1000)
            }
        }
    }

    private fun checkAndRequestHardwarePermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(android.Manifest.permission.BLUETOOTH_CONNECT)
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
        nativeEngine.getSecureApiKey = { provider ->
            when (provider) {
                "Gemini" -> "AIzaSy_MOCK_GEMINI_KEY_UNROOTED"
                "OpenRouter" -> "sk-or-v1-MOCK_KEY"
                else -> ""
            }
        }

        nativeEngine.onRequestHardwareData = { nodeId ->
            when (nodeId) {
                5 -> {
                    try {
                        val hasFine = checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        val hasCoarse = checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        
                        if (hasFine || hasCoarse) {
                            val locationTask = fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, null)
                            val location = com.google.android.gms.tasks.Tasks.await(locationTask)
                            if (location != null) {
                                "(${location.latitude}, ${location.longitude})"
                            } else {
                                "GPS_ERROR: Location Null"
                            }
                        } else {
                            "GPS_ERROR: Permission Denied"
                        }
                    } catch (e: Exception) {
                        Log.e("RoninUI", "Hardware Data Bridge Failed: ${e.message}")
                        "GPS_ERROR: ${e.message}"
                    }
                }
                else -> "Error: Unknown data node $nodeId"
            }
        }

        nativeEngine.executeHardwareAction = { nodeId, state ->
            var success = false
            try {
                when (nodeId) {
                    4 -> success = true // Managed in NativeEngine
                    5 -> {
                        val hasFine = checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        if (hasFine) {
                            val cancellationToken = CancellationTokenSource()
                            val locationFound = AtomicBoolean(false)
                            fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, cancellationToken.token)
                                .addOnSuccessListener { loc ->
                                    if (loc != null && !locationFound.get()) {
                                        locationFound.set(true)
                                        nativeEngine.injectLocation(loc.latitude, loc.longitude)
                                    }
                                }
                            success = true
                        }
                    }
                    6 -> success = true
                    7 -> success = true
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "Action failed", e)
            }
            success
        }
    }
}

@Composable
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel) {
    val messages = chatViewModel.messages
    val reasoningLogs = remember { mutableStateListOf<String>() }
    var currentInput by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val chatListState = rememberLazyListState()
    val scaffoldState = rememberScaffoldState()

    LaunchedEffect(Unit) {
        engine.onKernelMessage = { message ->
            reasoningLogs.add(0, message)
            if (message.startsWith("System Update:") || message.startsWith("Current Location:")) {
                if (!messages.contains("Ronin: $message")) {
                    messages.add("Ronin: $message")
                }
            }
        }

        engine.onSystemTiersUpdate = { temp, used, total ->
            chatViewModel.temperature = temp
            chatViewModel.ramUsedGB = used
            chatViewModel.ramTotalGB = total
            val pressure = engine.getLMKPressure()
            chatViewModel.lmkPressure = pressure
            chatViewModel.stability = (100 - pressure) / 100.0f
        }
    }

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
                    newHistory.reversed().forEach { (role, content) ->
                        val msg = if (role == "user") "User: $content" else "Ronin: $content"
                        if (!messages.contains(msg)) {
                            messages.add(0, msg)
                        }
                    }
                    chatViewModel.historyPage++
                }
                chatViewModel.isLoadingHistory = false
            }
        }
    }

    LaunchedEffect(Unit) {
        if (messages.isEmpty()) {
            loadNextHistoryPage()
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        topBar = {
            TopAppBar(title = { Text("Ronin Kernel v4.3") }, backgroundColor = Color(0xFF121212), contentColor = Color.White)
        }
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding).background(Color.Black)) {
            SystemHealthOverlay(chatViewModel.temperature, chatViewModel.ramUsedGB, chatViewModel.ramTotalGB)
            StabilityIndicator(chatViewModel.stability)
            
            Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
                LazyColumn(state = chatListState, modifier = Modifier.fillMaxSize().padding(8.dp), reverseLayout = false) {
                    items(messages) { msg ->
                        Text(text = msg, color = if (msg.startsWith("User:")) Color.Cyan else Color.White, modifier = Modifier.padding(vertical = 4.dp))
                    }
                }
            }

            ReasoningConsole(reasoningLogs)

            Row(modifier = Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
                TextField(
                    value = currentInput,
                    onValueChange = { currentInput = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Command Ronin...") },
                    colors = TextFieldDefaults.textFieldColors(backgroundColor = Color.DarkGray, textColor = Color.White)
                )
                IconButton(onClick = {
                    if (currentInput.isNotBlank()) {
                        messages.add("User: $currentInput")
                        val inputCopy = currentInput
                        currentInput = ""
                        scope.launch {
                            val kernelRawOutput = engine.processInputAsync(inputCopy)
                            val kernelOutput = try {
                                if (kernelRawOutput.startsWith("{")) {
                                    val resultStart = kernelRawOutput.indexOf("\"result\": \"") + 11
                                    val resultEnd = kernelRawOutput.lastIndexOf("\"")
                                    kernelRawOutput.substring(resultStart, resultEnd)
                                } else kernelRawOutput
                            } catch (e: Exception) { kernelRawOutput }

                            if (kernelOutput.contains("Action Initiated - Flashlight")) {
                                scaffoldState.snackbarHostState.showSnackbar("System: Flashlight toggled.")
                            }
                            if (!kernelOutput.startsWith("{")) {
                                messages.add("Ronin: $kernelOutput")
                            }
                        }
                    }
                }) {
                    Icon(Icons.Default.Send, contentColor = Color.Cyan, contentDescription = "Send")
                }
            }
        }
    }
}

@Composable
fun SystemHealthOverlay(temp: Float, used: Float, total: Float) {
    Row(modifier = Modifier.fillMaxWidth().padding(8.dp).background(Color(0xFF1A1A1A)), horizontalArrangement = SpaceEvenly) {
        Text("Temp: ${"%.1f".format(temp)}°C", color = if (temp > 40) Color.Red else Color.Green, fontSize = 12.sp)
        Text("RAM: ${"%.2f".format(used)}/${"%.2f".format(total)} GB", color = Color.White, fontSize = 12.sp)
    }
}

@Composable
fun StabilityIndicator(stability: Float) {
    val color = when {
        stability > 0.8f -> Color.Green
        stability > 0.4f -> Color.Yellow
        else -> Color.Red
    }
    Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Stability ", color = Color.White, fontSize = 12.sp)
        LinearProgressIndicator(progress = stability, color = color, backgroundColor = Color.DarkGray, modifier = Modifier.weight(1f))
    }
}

@Composable
fun ReasoningConsole(logs: List<String>) {
    Column(modifier = Modifier.height(120.dp).fillMaxWidth().background(Color(0xFF0A0A0A)).padding(4.dp)) {
        Text("Reasoning Console", color = Color.Gray, fontSize = 10.sp, fontWeight = FontWeight.Bold)
        Divider(color = Color.DarkGray)
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(logs) { log ->
                Text(text = log, color = Color(0xFF00FF00), fontSize = 10.sp, modifier = Modifier.padding(vertical = 1.dp))
            }
        }
    }
}

private fun SpaceEvenly(arrangement: Arrangement.Horizontal) = Arrangement.SpaceEvenly
