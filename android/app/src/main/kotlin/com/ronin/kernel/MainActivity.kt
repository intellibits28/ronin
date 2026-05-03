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
import com.google.android.gms.tasks.Tasks
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
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.AlertDialog
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Surface
import androidx.compose.material.Text
import androidx.compose.material.TopAppBar
import androidx.compose.material.IconButton
import androidx.compose.material.Icon
import androidx.compose.material.Scaffold
import androidx.compose.material.rememberScaffoldState
import androidx.compose.material.LinearProgressIndicator
import androidx.compose.material.TextField
import androidx.compose.material.TextFieldDefaults
import androidx.compose.material.Button
import androidx.compose.material.TextButton
import androidx.compose.material.Switch
import androidx.compose.material.RadioButton
import androidx.compose.material.DropdownMenu
import androidx.compose.material.DropdownMenuItem
import androidx.compose.material.OutlinedButton
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.json.JSONArray
import org.json.JSONObject

data class CloudProvider(
    val name: String,
    val endpoint: String,
    val modelId: String,
    val authType: String
)

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

    // Phase 4.4: Dynamic Configuration
    var showSettings by mutableStateOf(false)
    var offlineMode by mutableStateOf(false)
    var isKernelHydrated by mutableStateOf(false)
    var localModelPath by mutableStateOf("/storage/emulated/0/Ronin/models/gemma_4.litertlm")
    var primaryCloudProvider by mutableStateOf("Gemini")
    val cloudProviders = mutableStateListOf<CloudProvider>()
    val discoveredModels = mutableStateListOf<String>()
}

class MainActivity : ComponentActivity() {
    private lateinit var nativeEngine: NativeEngine
    
    // Phase 5.10: Full Integrity Registry
    private val MODEL_REGISTRY = mapOf(
        "gemma-4-E2B-it.litertlm" to "ab7838cdfc8f77e54d8ca45eadceb20452d9f01e4bfade03e5dce27911b27e42",
        "gemma-2b-it-cpu-int4.bin" to "176452e0eef32e7cd477e5609160278f3f5cbfeeb46d2cb2d37bd631af1b0bea",
        "gemma-2b-it-gpu-int4.bin" to "ef44d548e44a2a6f313c3f3e94a48e1de786871ad95f4cd81bfb35372032cdbd"
    )

    private val EXPECTED_ROUTER_HASH = "5725965a8ff8646946425ce07fd8fa5473818c4399ef589c1e937226589819ce"

    private lateinit var fusedLocationClient: FusedLocationProviderClient
    private lateinit var sharedPreferences: android.content.SharedPreferences
    private var lastPermissionState = false

    companion object {
    }

    private val modelPickerLauncher = registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let {
            importModelFromUri(it)
        }
    }

    private fun importModelFromUri(uri: Uri) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        
        lifecycleScope.launch {
            chatViewModel.reasoningLogs.add(0, "Importing Model: ${uri.lastPathSegment}")
            val success = withContext(Dispatchers.IO) {
                try {
                    val inputStream = contentResolver.openInputStream(uri)
                    val modelsDir = java.io.File(filesDir, "models")
                    if (!modelsDir.exists()) modelsDir.mkdirs()
                    
                    val fileName = uri.lastPathSegment?.substringAfterLast("/") ?: "imported_model.bin"
                    val targetFile = java.io.File(modelsDir, fileName)
                    
                    inputStream?.use { input ->
                        java.io.FileOutputStream(targetFile).use { output ->
                            input.copyTo(output, bufferSize = 1024 * 1024) // Phase 6.6: 1MB Buffer for high-speed import
                        }
                    }
                    true
                } catch (e: Exception) {
                    Log.e("RoninImport", "Model import failed: ${e.message}")
                    false
                }
            }
            
            if (success) {
                scanLocalModels()
                Toast.makeText(this@MainActivity, "Model Imported Successfully.", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this@MainActivity, "Model Import Failed.", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun scanLocalModels() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()
        
        val models = modelsDir.listFiles { file -> 
            file.extension == "bin" || file.extension == "litertlm" || file.extension == "onnx" 
        }?.map { it.absolutePath } ?: emptyList()
        
        chatViewModel.discoveredModels.clear()
        chatViewModel.discoveredModels.addAll(models)
        
        if (models.isEmpty()) {
            nativeEngine.pushKernelMessage("> System: No Reasoning Brain found in internal models directory.")
        } else {
            Log.i("RoninScan", "Discovered ${models.size} models in private storage.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        nativeEngine = NativeEngine(this)

        // Initialize EncryptedSharedPreferences (Phase 4.4)
        val masterKey = MasterKey.Builder(this)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        
        sharedPreferences = EncryptedSharedPreferences.create(
            this,
            "ronin_secure_prefs",
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )

        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)
        
        // Phase 4.8.1: Critical Initialization Order Fix
        copyAssetsToFilesDir(filesDir)

        // Phase 6.6: Unified Asynchronous Initialization
        lifecycleScope.launch(Dispatchers.Main) {
            // 1. Load native libraries off-thread
            NativeEngine.initializeAsync()
            
            // 2. Hydrate spine
            nativeEngine.initialize()
            registerComponentCallbacks(nativeEngine)
            
            // 3. Setup hardware bridge
            setupHardwareCallbacks()
            
            // 4. Persistence Sync & Native Config
            val lastProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini"
            nativeEngine.setPrimaryCloudProviderSafe(lastProvider)
            
            val offline = sharedPreferences.getBoolean("offline_mode", false)
            nativeEngine.setOfflineModeSafe(offline)
            
            // 5. Load Cloud Providers (touches JNI)
            loadCloudProvidersFromDisk()

            // 6. Ensure permissions are fresh for hydration
            lastPermissionState = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Environment.isExternalStorageManager()
            } else {
                checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED
            }

            if (lastPermissionState) {
                scanLocalModels()
                val savedModelPath = sharedPreferences.getString("local_model_path", "")
                if (!savedModelPath.isNullOrEmpty()) {
                    Log.i("RoninBoot", "Cold Start: Re-hydrating saved model $savedModelPath")
                    hydrateModel(savedModelPath)
                }
            }
        }

        // Phase 4.4.3: Initial Hydration of Provider Templates
        val configDir = java.io.File("/storage/emulated/0/Ronin/config")
        if (!configDir.exists()) configDir.mkdirs()
        val providersFile = java.io.File(configDir, "providers.json")
        if (!providersFile.exists()) {
            try {
                assets.open("providers.json").use { input ->
                    java.io.FileOutputStream(providersFile).use { output ->
                        input.copyTo(output)
                    }
                }
                Log.i("RoninBoot", "Initial provider templates hydrated.")
            } catch (e: Exception) {
                Log.e("RoninBoot", "Failed to hydrate providers: ${e.message}")
            }
        }

        // loadCloudProvidersFromDisk() removed (moved to async block)
        
        checkAndRequestStoragePermission()
        checkAndRequestHardwarePermissions()

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            // Sync active path and provider from Engine/Prefs before showing UI
            LaunchedEffect(Unit) {
                // Wait a bit for async init if needed, though UI handles nulls
                chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                chatViewModel.offlineMode = sharedPreferences.getBoolean("offline_mode", false)
                chatViewModel.primaryCloudProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini"
                chatViewModel.isKernelHydrated = nativeEngine.isLoaded()
            }
            RoninChatUI(
                engine = nativeEngine,
                chatViewModel = chatViewModel,
                modelPicker = modelPickerLauncher,
                onSaveOfflineMode = { saveOfflineMode(it) }
            )
        }
    }

    private fun copyAssetsToFilesDir(filesDir: java.io.File) {
        val assetsDir = java.io.File(filesDir, "assets")
        if (!assetsDir.exists()) assetsDir.mkdirs()
        
        // Copy capabilities.json
        try {
            val capFile = java.io.File(assetsDir, "capabilities.json")
            if (!capFile.exists()) {
                assets.open("capabilities.json").use { input ->
                    java.io.FileOutputStream(capFile).use { output ->
                        input.copyTo(output, bufferSize = 1024 * 1024)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to copy capabilities: ${e.message}")
        }

        // Copy models if they don't exist
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()
        
        try {
            val routerFile = java.io.File(modelsDir, "model.onnx")
            if (!routerFile.exists()) {
                assets.open("models/model.onnx").use { input ->
                    java.io.FileOutputStream(routerFile).use { output ->
                        input.copyTo(output, bufferSize = 1024 * 1024)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to copy router model: ${e.message}")
        }
    }

    private fun loadCloudProvidersFromDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = java.io.File("/storage/emulated/0/Ronin/config")
        val providersFile = java.io.File(configDir, "providers.json")
        
        if (providersFile.exists()) {
            try {
                val json = providersFile.readText()
                val array = JSONArray(json)
                chatViewModel.cloudProviders.clear()
                for (i in 0 until array.length()) {
                    val obj = array.getJSONObject(i)
                    chatViewModel.cloudProviders.add(CloudProvider(
                        obj.getString("name"),
                        obj.getString("endpoint"),
                        obj.getString("modelId"),
                        obj.getString("authType")
                    ))
                }
                nativeEngine.updateCloudProvidersSafe(json)
            } catch (e: Exception) {
                Log.e("RoninBoot", "Failed to parse providers: ${e.message}")
            }
        }
    }

    private fun setupHardwareCallbacks() {
        nativeEngine.getSecureApiKey = { provider ->
            // Phase 4.4: Secure Credential Sovereignty
            sharedPreferences.getString(provider, "")?.trim() ?: ""
        }

        nativeEngine.onRequestHardwareData = { nodeId ->
            when (nodeId) {
                5 -> {
                    try {
                        val hasFine = checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        val hasCoarse = checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                        if (hasFine || hasCoarse) {
                            val locationTask = fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, null)
                            val location = Tasks.await(locationTask)
                            if (location != null) "(${location.latitude}, ${location.longitude})" else "GPS_ERROR: Location Null"
                        } else "GPS_ERROR: Permission Denied"
                    } catch (e: Exception) {
                        Log.e("RoninUI", "Hardware Data Bridge Failed: ${e.message}")
                        "GPS_ERROR: ${e.message}"
                    }
                }
                else -> "Error: Unknown data node $nodeId"
            }
        }

        nativeEngine.executeHardwareAction = { nodeId, state ->
            var toolName = ""
            when (nodeId) {
                1 -> toolName = "Reasoning Spine (Power Profile)"
                4 -> toolName = "Flashlight"
                5 -> toolName = "GPS"
                6 -> toolName = "WiFi"
                7 -> toolName = "Bluetooth"
            }
            if (nodeId == 1) {
                 runOnUiThread { 
                     val message = if (state) "Kernel: NPU Power Level RESTORED." else "Kernel: NPU Throttling ACTIVE (Thermal Protection)."
                     Toast.makeText(this, message, Toast.LENGTH_SHORT).show() 
                 }
                 true
            } else {
                runOnUiThread { Toast.makeText(this, "Kernel: Initiating $toolName toggle...", Toast.LENGTH_SHORT).show() }
                var success = false
                try {
                    when (nodeId) {
                        4 -> {
                            val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
                            val cameraId = cameraManager.cameraIdList[0]
                            cameraManager.setTorchMode(cameraId, state)
                            success = true
                        }
                        6 -> {
                            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                            @Suppress("DEPRECATION")
                            wifiManager.isWifiEnabled = state
                            success = true
                        }
                        7 -> {
                            val bluetoothAdapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
                            if (state) bluetoothAdapter?.enable() else bluetoothAdapter?.disable()
                            success = true
                        }
                    }
                } catch (e: Exception) {
                    Log.e("RoninUI", "Hardware execution failed: ${e.message}")
                }
                success
            }
        }

        nativeEngine.onSystemTiersUpdate = { temp, used, total ->
            val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
            chatViewModel.temperature = temp
            chatViewModel.ramUsedGB = used
            chatViewModel.ramTotalGB = total
        }

        nativeEngine.onKernelMessage = { msg ->
            val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
            chatViewModel.reasoningLogs.add(0, msg)
        }
    }

    private fun hydrateModel(path: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val file = java.io.File(path)
        if (!file.exists()) {
            Toast.makeText(this, "Model file not found at: $path", Toast.LENGTH_LONG).show()
            return
        }

        // Phase 5.10: Full Background Integrity Verification
        lifecycleScope.launch(Dispatchers.Default) {
            val filename = file.name
            val currentFingerprint = calculateFingerprint(file)

            // 1. Strict Registry Guard
            val expectedHash = MODEL_REGISTRY[filename] ?: if (filename == "model.onnx") EXPECTED_ROUTER_HASH else ""

            if (expectedHash.isNotEmpty() && currentFingerprint != expectedHash) {
                withContext(Dispatchers.Main) {
                    Log.e("RoninIntegrity", "FATAL: Integrity Breach detected for $filename. Expected: $expectedHash, Got: $currentFingerprint")
                    Toast.makeText(this@MainActivity, "CRITICAL: Integrity Breach - model hydration aborted.", Toast.LENGTH_LONG).show()
                    chatViewModel.isKernelHydrated = false
                }
                return@launch
            }

            // 2. Anti-Swap Check for unregistered models
            val savedFingerprint = sharedPreferences.getString("fingerprint_$path", "")
            if (expectedHash.isEmpty() && !savedFingerprint.isNullOrEmpty() && savedFingerprint != currentFingerprint) {
                withContext(Dispatchers.Main) {
                     val builder = android.app.AlertDialog.Builder(this@MainActivity)
                     builder.setTitle("Anti-Swap Protection")
                     builder.setMessage("Warning: Unregistered model data changed for '$filename'. Re-verify model?")
                     builder.setPositiveButton("Load Anyway") { _: android.content.DialogInterface, _: Int -> 
                         performHydration(path, currentFingerprint) 
                     }
                     builder.setNegativeButton("Cancel", null)
                     builder.show()
                }
            } else {
                performHydration(path, currentFingerprint)
            }
        }
    }

    private fun performHydration(path: String, fingerprint: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            chatViewModel.reasoningLogs.add(0, "Hydration Triggered: ${path.substringAfterLast("/")}")

            val jniSuccess = withContext(Dispatchers.IO) {
                nativeEngine.loadModel(path)
            }

            if (jniSuccess) {
                chatViewModel.isKernelHydrated = true
                sharedPreferences.edit().putString("local_model_path", path).apply()
                sharedPreferences.edit().putString("fingerprint_$path", fingerprint).apply()
                chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                Toast.makeText(this@MainActivity, "Kernel Hydrated Successfully.", Toast.LENGTH_SHORT).show()
            } else {
                chatViewModel.isKernelHydrated = false
                Toast.makeText(this@MainActivity, "CRITICAL: Kernel Hydration Failed.", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun calculateFingerprint(file: java.io.File): String {
        return try {
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buffer = ByteArray(1024 * 1024) // Phase 6.6: 1MB Buffer for high-speed hashing
                var bytesRead = input.read(buffer)
                while (bytesRead != -1) {
                    digest.update(buffer, 0, bytesRead)
                    bytesRead = input.read(buffer)
                }
            }
            digest.digest().joinToString("") { "%02x".format(it) }
        } catch (e: Exception) { "" }
    }

    private fun checkAndRequestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    intent.addCategory("android.intent.category.DEFAULT")
                    intent.data = Uri.parse(String.format("package:%s", packageName))
                    startActivity(intent)
                } catch (e: Exception) {
                    val intent = Intent()
                    intent.action = Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION
                    startActivity(intent)
                }
            }
        } else {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(android.Manifest.permission.READ_EXTERNAL_STORAGE), 1001)
            }
        }
    }

    private fun checkAndRequestHardwarePermissions() {
        val permissions = mutableListOf<String>()
        if (checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            permissions.add(android.Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (checkSelfPermission(android.Manifest.permission.CAMERA) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            permissions.add(android.Manifest.permission.CAMERA)
        }
        if (permissions.isNotEmpty()) {
            requestPermissions(permissions.toTypedArray(), 1002)
        }
    }

    private fun saveOfflineMode(offline: Boolean) {
        sharedPreferences.edit().putBoolean("offline_mode", offline).apply()
        nativeEngine.setOfflineModeSafe(offline)
    }

    override fun onResume() {
        super.onResume()
        val currentPermission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED
        }

        if (currentPermission && !lastPermissionState) {
            Log.i("RoninLifecycle", "Permission granted while resumed. Refreshing registry.")
            refreshRegistry()
        }
        lastPermissionState = currentPermission
    }

    private fun refreshRegistry() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            // All Files Access Guard: wait 500ms for OS filesystem sync
            delay(500)
            scanLocalModels()
            
            val savedModelPath = sharedPreferences.getString("local_model_path", "")
            if (!savedModelPath.isNullOrEmpty()) {
                hydrateModel(savedModelPath)
            } else if (chatViewModel.discoveredModels.isNotEmpty()) {
                // Phase 4.8.5: Auto-select first available model on first run
                // Phase 4.9.0: Now using Internal Storage
                val autoPath = chatViewModel.discoveredModels[0]
                Log.i("RoninBoot", "First Run (Internal): Auto-selecting model $autoPath")
                hydrateModel(autoPath)
            }
        }
    }
}

@Composable
fun RoninChatUI(
    engine: NativeEngine,
    chatViewModel: ChatViewModel,
    modelPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>,
    onSaveOfflineMode: (Boolean) -> Unit
) {
    val context = LocalContext.current
    val reasoningLogs = chatViewModel.reasoningLogs
    val reasoningListState = rememberLazyListState()
    val scrollState = rememberLazyListState()
    var currentInput by remember { mutableStateOf("") }
    val scaffoldState = rememberScaffoldState()
    val scope = rememberCoroutineScope()

    LaunchedEffect(reasoningLogs.size) { 
        if (reasoningLogs.isNotEmpty()) {
            reasoningListState.animateScrollToItem(0) 
        }
    }

    LaunchedEffect(Unit) {
        val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memInfo = ActivityManager.MemoryInfo()
        
        // Phase 6.6: Off-thread Health Monitor
        withContext(Dispatchers.IO) {
            while (true) {
                activityManager.getMemoryInfo(memInfo)
                val totalRAM = memInfo.totalMem / (1024f * 1024f * 1024f)
                val availableRAM = memInfo.availMem / (1024f * 1024f * 1024f)
                val usedRAM = totalRAM - availableRAM
                
                // Get temperature without registering receiver on Main thread
                val batteryStatus: Intent? = IntentFilter(Intent.ACTION_BATTERY_CHANGED).let { filter ->
                    context.registerReceiver(null, filter)
                }
                val temp = batteryStatus?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.div(10f) ?: 0f
                
                withContext(Dispatchers.Main) {
                    chatViewModel.temperature = temp
                    chatViewModel.ramUsedGB = usedRAM
                    chatViewModel.ramTotalGB = totalRAM
                    chatViewModel.lmkPressure = engine.getLMKPressureSafe()
                    chatViewModel.stability = (100 - chatViewModel.lmkPressure) / 100.0f
                }
                
                engine.updateSystemHealthSafe(temp, usedRAM, totalRAM)
                
                // Delay increased to 5 seconds to reduce JNI/Context overhead
                delay(5000)
            }
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        topBar = {
            TopAppBar(
                title = { Text("Ronin Kernel", fontWeight = FontWeight.Bold) },
                actions = {
                    IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) {
                        Icon(if (chatViewModel.showSysInfo) Icons.Default.Info else Icons.Default.BarChart, contentDescription = "System Info")
                    }
                    IconButton(onClick = { chatViewModel.showSettings = true }) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                },
                backgroundColor = MaterialTheme.colors.surface,
                contentColor = MaterialTheme.colors.onSurface,
                elevation = 0.dp
            )
        }
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize().background(Color(0xFF0F111A))) {
            
            // Health Stats Bar (Phase 4.4)
            AnimatedVisibility(visible = chatViewModel.showSysInfo) {
                SystemInfoPanel(chatViewModel)
            }

            // Reasoning Logs Spine (L2 Console)
            Box(modifier = Modifier.weight(0.3f).fillMaxWidth().background(Color.Black.copy(alpha = 0.3f)).padding(8.dp)) {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Psychology, "Reasoning", tint = Color(0xFF64B5F6), modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(8.dp))
                        Text("Reasoning Console", fontSize = 12.sp, color = Color(0xFF64B5F6), fontWeight = FontWeight.Bold)
                    }
                    Divider(color = Color(0xFF64B5F6).copy(alpha = 0.2f), modifier = Modifier.padding(vertical = 4.dp))
                    LazyColumn(state = reasoningListState, modifier = Modifier.fillMaxSize()) {
                        items(reasoningLogs) { log ->
                            Text(text = log, color = if (log.startsWith("Error")) Color.Red else Color(0xFFAAAAAA), fontSize = 11.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(vertical = 1.dp))
                        }
                    }
                }
            }

            // Main Chat (L1 Console)
            Box(modifier = Modifier.weight(0.7f).fillMaxWidth()) {
                LazyColumn(state = scrollState, modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) {
                    items(chatViewModel.messages.reversed()) { msg ->
                        ChatBubble(msg)
                    }
                }
            }

            // Input Section (Tier 0 Interface)
            Surface(elevation = 8.dp, color = Color(0xFF1A1C2C)) {
                Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                    TextField(
                        value = currentInput,
                        onValueChange = { currentInput = it },
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)),
                        placeholder = { Text("Send instruction to Kernel...") },
                        colors = TextFieldDefaults.textFieldColors(
                            backgroundColor = Color(0xFF25283D),
                            focusedIndicatorColor = Color.Transparent,
                            unfocusedIndicatorColor = Color.Transparent,
                            textColor = Color.White
                        ),
                        trailingIcon = {
                             IconButton(onClick = {
                                if (currentInput.isNotBlank()) {
                                    val input = currentInput
                                    chatViewModel.messages.add("User: $input")
                                    currentInput = ""
                                    scope.launch {
                                        val response = withContext(Dispatchers.Default) {
                                            engine.processInput(input)
                                        }
                                        chatViewModel.messages.add("Ronin: $response")
                                        scrollState.animateScrollToItem(0)
                                    }
                                }
                             }) {
                                 Icon(Icons.Default.Send, "Send", tint = Color(0xFF64B5F6))
                             }
                        }
                    )
                }
            }
        }
    }

    if (chatViewModel.showSettings) {
        SettingsDialog(chatViewModel, modelPicker, onSaveOfflineMode)
    }
}

@Composable
fun SystemInfoPanel(chatViewModel: ChatViewModel) {
    Surface(color = Color(0xFF161922), modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) {
                InfoItem("Thermal", "${chatViewModel.temperature}°C", if (chatViewModel.temperature > 40) Color.Red else Color.Green)
                InfoItem("RAM Used", "${"%.2f".format(chatViewModel.ramUsedGB)} GB", Color.White)
                InfoItem("Stability", "${(chatViewModel.stability * 100).toInt()}%", Color.Cyan)
            }
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(
                progress = chatViewModel.stability,
                modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                color = if (chatViewModel.stability < 0.5f) Color.Red else Color.Cyan,
                backgroundColor = Color.White.copy(alpha = 0.1f)
            )
        }
    }
}

@Composable
fun InfoItem(label: String, value: String, color: Color) {
    Column {
        Text(label, size = 10.sp, color = Color.Gray)
        Text(value, size = 14.sp, color = color, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun Text(text: String, size: androidx.compose.ui.unit.TextUnit, color: Color, fontWeight: FontWeight = FontWeight.Normal) {
    androidx.compose.material.Text(text = text, fontSize = size, color = color, fontWeight = fontWeight)
}

@Composable
fun ChatBubble(message: String) {
    val isUser = message.startsWith("User:")
    val content = if (isUser) message.removePrefix("User: ") else message.removePrefix("Ronin: ")
    
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        Surface(
            color = if (isUser) Color(0xFF2D3142) else Color(0xFF64B5F6).copy(alpha = 0.1f),
            shape = RoundedCornerShape(
                topStart = 16.dp, topEnd = 16.dp, 
                bottomStart = if (isUser) 16.dp else 0.dp, 
                bottomEnd = if (isUser) 0.dp else 16.dp
            ),
            border = if (!isUser) BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.3f)) else null
        ) {
            Text(
                content,
                modifier = Modifier.padding(12.dp),
                color = Color.White,
                fontSize = 14.sp
            )
        }
    }
}

@Composable
fun SettingsDialog(
    chatViewModel: ChatViewModel,
    modelPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>,
    onSaveOfflineMode: (Boolean) -> Unit
) {
    AlertDialog(
        onDismissRequest = { chatViewModel.showSettings = false },
        title = { Text("Ronin Kernel Configuration", fontWeight = FontWeight.Bold) },
        text = {
            Column {
                Divider()
                Spacer(Modifier.height(16.dp))
                
                Text("Reasoning Brain", fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text(chatViewModel.localModelPath.substringAfterLast("/"), fontSize = 12.sp, color = Color.Gray)
                
                Spacer(Modifier.height(8.dp))
                OutlinedButton(
                    onClick = { modelPicker.launch(arrayOf("*/*")) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.CloudDownload, "Import")
                    Spacer(Modifier.width(8.dp))
                    Text("Import .litertlm / .bin")
                }
                
                Spacer(Modifier.height(16.dp))
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                    Text("Offline-Only Mode", modifier = Modifier.weight(1f))
                    Switch(checked = chatViewModel.offlineMode, onCheckedChange = { 
                        chatViewModel.offlineMode = it 
                        onSaveOfflineMode(it)
                    })
                }
                Text("Disables Cloud Fallback for maximum data sovereignty.", fontSize = 10.sp, color = Color.Gray)

                Spacer(Modifier.height(16.dp))
                Text("Cloud Provider", fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                var expanded by remember { mutableStateOf(false) }
                Box {
                    OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
                        Text(chatViewModel.primaryCloudProvider)
                        Icon(Icons.Default.ArrowDropDown, "Expand")
                    }
                    DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                        chatViewModel.cloudProviders.forEach { provider ->
                            DropdownMenuItem(onClick = {
                                chatViewModel.primaryCloudProvider = provider.name
                                expanded = false
                            }) {
                                Text(provider.name)
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { chatViewModel.showSettings = false }) {
                Text("CLOSE")
            }
        }
    )
}

@Composable
fun Divider(color: Color = Color.Gray.copy(alpha = 0.2f), modifier: Modifier = Modifier) {
    androidx.compose.material.Divider(color = color, modifier = modifier)
}
