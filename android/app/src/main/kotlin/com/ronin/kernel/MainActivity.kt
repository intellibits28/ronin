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
            chatViewModel.reasoningLogs.add(0, "Importing External Model...")
            
            val success = withContext(Dispatchers.IO) {
                try {
                    val contentResolver = applicationContext.contentResolver
                    val fileName = getFileName(uri) ?: "imported_model.litertlm"
                    val modelsDir = java.io.File(filesDir, "models")
                    if (!modelsDir.exists()) modelsDir.mkdirs()
                    
                    val destFile = java.io.File(modelsDir, fileName)
                    
                    contentResolver.openInputStream(uri)?.use { input ->
                        java.io.FileOutputStream(destFile).use { output ->
                            input.copyTo(output)
                        }
                    }
                    Log.i("RoninBridge", "External model imported to: ${destFile.absolutePath}")
                    destFile.absolutePath
                } catch (e: Exception) {
                    Log.e("RoninBridge", "Import failed: ${e.message}")
                    null
                }
            }

            if (success != null) {
                // Phase 4.9.1: Trigger hydration with the new Internal Path
                hydrateModel(success)
                Toast.makeText(this@MainActivity, "Model imported successfully.", Toast.LENGTH_SHORT).show()
                scanLocalModels() // Refresh UI list
            } else {
                Toast.makeText(this@MainActivity, "Failed to import model.", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun getFileName(uri: Uri): String? {
        var name: String? = null
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (cursor.moveToFirst()) {
                name = cursor.getString(nameIndex)
            }
        }
        return name
    }

    override fun onResume() {
        super.onResume()
        // Phase 4.6.6: OnResume Refresh Logic
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
            val md = java.security.MessageDigest.getInstance("SHA-256")
            java.io.FileInputStream(file).use { fis ->
                val buffer = ByteArray(1024 * 1024) // 1MB streaming chunk
                var read = fis.read(buffer)
                while (read != -1) {
                    md.update(buffer, 0, read)
                    read = fis.read(buffer)
                }
            }
            md.digest().joinToString("") { "%02x".format(it) }
        } catch (e: Exception) {
            "error_${e.message}"
        }
    }


    private fun scanLocalModels() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        // Phase 4.9.0: Path Modernization (Private Internal Storage)
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()
        
        val models = modelsDir.listFiles { _, name -> 
            name.endsWith(".bin") || name.endsWith(".litertlm") 
        } ?: emptyArray()

        chatViewModel.discoveredModels.clear()
        models.forEach { chatViewModel.discoveredModels.add(it.absolutePath) }

        if (models.isEmpty()) {
            nativeEngine.pushKernelMessage("> System: No Reasoning Brain found in internal models directory.")
        } else {
            Log.i("RoninScan", "Discovered ${models.size} models in private storage.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        nativeEngine = NativeEngine(this)

        // Phase 6.6: Unified Asynchronous Initialization
        lifecycleScope.launch(Dispatchers.Main) {
            // 1. Load native libraries off-thread
            NativeEngine.initializeAsync()
            
            // 2. Hydrate spine
            nativeEngine.initialize()
            registerComponentCallbacks(nativeEngine)
            
            // 3. Setup hardware bridge
            setupHardwareCallbacks()
            
            // 4. Persistence Sync
            val lastProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini"
            nativeEngine.setPrimaryCloudProvider(lastProvider)
            
            if (lastPermissionState) {
                scanLocalModels()
                val savedModelPath = sharedPreferences.getString("local_model_path", "")
                if (!savedModelPath.isNullOrEmpty()) {
                    hydrateModel(savedModelPath)
                }
            }
        }

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
        // Ensure assets (model.onnx) are physically copied to storage before JNI hydration
        copyAssetsToFilesDir(filesDir)

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

        loadCloudProvidersFromDisk()
        
        lastPermissionState = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED
        }

        checkAndRequestStoragePermission()
        checkAndRequestHardwarePermissions()

        val offline = sharedPreferences.getBoolean("offline_mode", false)
        nativeEngine.setOfflineMode(offline)
        val lastProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini"

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            // Sync active path and provider from Engine/Prefs before showing UI
            LaunchedEffect(Unit) {
                chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                chatViewModel.offlineMode = offline
                chatViewModel.primaryCloudProvider = lastProvider
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

    fun saveOfflineMode(offline: Boolean) {
        sharedPreferences.edit().putBoolean("offline_mode", offline).apply()
    }

    fun savePrimaryCloudProvider(name: String) {
        sharedPreferences.edit().putString("primary_cloud_provider", name).apply()
    }

    private fun getPathFromUri(uri: Uri): String? {
        return uri.path?.let { path ->
            if (path.contains("primary:")) {
                "/storage/emulated/0/${path.substringAfter("primary:")}"
            } else {
                path
            }
        }
    }

    private fun copyAssetsToFilesDir(targetDir: java.io.File) {
        try {
            val assetManager = assets
            
            // Phase 4.8.1: Hardened Asset Discovery
            val assetsList = assetManager.list("models") ?: emptyArray()
            val modelExists = assetsList.contains("model.onnx")
            Log.i("RoninBoot", "Asset Verification: assets/models/model.onnx found = $modelExists")

            val files = assetManager.list("") ?: return
            for (filename in files) {
                if (filename == "models" || filename == "images" || filename == "webkit") continue
                if (filename.contains(".")) {
                    copyFile(filename, "", targetDir)
                }
            }

            val models = assetManager.list("models") ?: return
            for (modelFile in models) {
                copyFile(modelFile, "models", targetDir)
            }

            // Phase 5.3: Explicit Provider Hydration
            val configDir = java.io.File(filesDir, "config")
            if (!configDir.exists()) configDir.mkdirs()
            val providersFile = java.io.File(configDir, "providers.json")
            
            if (!providersFile.exists()) {
                Log.i("RoninBoot", "Hydrating default providers.json...")
                try {
                    assets.open("providers.json").use { input ->
                        providersFile.outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }
                    Log.i("RoninBoot", "Default providers.json deployed.")
                } catch (e: Exception) {
                    Log.e("RoninBoot", "Asset providers.json not found in root.")
                }
            }
            Log.i("RoninBoot", "Assets successfully synchronized to: ${targetDir.absolutePath}")
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to sync assets: ${e.message}")
        }
    }

    private fun copyFile(filename: String, subDir: String, targetDir: java.io.File) {
        val assetPath = if (subDir.isEmpty()) filename else "$subDir/$filename"
        val destDir = if (subDir.isEmpty()) {
            java.io.File(targetDir, "assets")
        } else {
            java.io.File(targetDir, "assets/$subDir")
        }
        if (!destDir.exists()) destDir.mkdirs()
        
        val outFile = java.io.File(destDir, filename)
        
        try {
            assets.open(assetPath).use { inputStream ->
                val assetSize = inputStream.available().toLong()
                
                // Requirement 2: Overwrite logic (Skip if size matches)
                if (outFile.exists() && outFile.length() == assetSize) {
                    Log.i("RoninBoot", "Skipping $filename (Size matches: $assetSize bytes)")
                    return
                }

                // Requirement 3: Path Verification
                Log.i("RoninBoot", "Copying asset: $assetPath -> ${outFile.absolutePath}")
                
                java.io.FileOutputStream(outFile).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
                
                if (filename == "model.onnx") {
                    Log.i("RoninBoot", "CRITICAL ASSET PLACED: ${outFile.absolutePath} (Size: ${outFile.length()})")
                }
            }
        } catch (e: Exception) {
            Log.e("RoninBoot", "Failed to copy $filename: ${e.message}")
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
                        5 -> {
                            val hasFine = checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                            val hasCoarse = checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED
                            if (hasFine || hasCoarse) {
                                val cancellationToken = CancellationTokenSource()
                                val locationFound = AtomicBoolean(false)
                                fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, cancellationToken.token)
                                    .addOnSuccessListener { loc ->
                                        if (loc != null && !locationFound.get()) {
                                            locationFound.set(true)
                                            nativeEngine.injectLocation(loc.latitude, loc.longitude)
                                        }
                                    }
                                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                                    if (!locationFound.get()) {
                                        cancellationToken.cancel()
                                        @Suppress("MissingPermission")
                                        fusedLocationClient.lastLocation.addOnSuccessListener { lastLoc ->
                                            if (lastLoc != null) {
                                                locationFound.set(true)
                                                nativeEngine.injectLocation(lastLoc.latitude, lastLoc.longitude)
                                            } else {
                                                nativeEngine.injectLocation(0.0, 0.0)
                                            }
                                        }.addOnFailureListener { nativeEngine.injectLocation(0.0, 0.0) }
                                    }
                                }, 10000)
                                success = true
                            } else {
                                nativeEngine.injectLocation(0.0, 0.0)
                            }
                        }
                        6 -> {
                            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                                startActivity(Intent(Settings.Panel.ACTION_WIFI))
                                success = true
                            } else {
                                @Suppress("DEPRECATION")
                                success = wifiManager.setWifiEnabled(state)
                            }
                        }
                        7 -> {
                            val bluetoothAdapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
                            if (bluetoothAdapter != null) {
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                    startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
                                    success = true
                                } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                    if (checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                                        @Suppress("DEPRECATION")
                                        success = if (state) bluetoothAdapter.enable() else bluetoothAdapter.disable()
                                    }
                                } else {
                                    @Suppress("MissingPermission", "DEPRECATION")
                                    success = if (state) bluetoothAdapter.enable() else bluetoothAdapter.disable()
                                }
                            }
                        }
                    }
                } catch (e: Exception) {
                    Log.e("RoninUI", "Hardware Node $nodeId failed", e)
                }
                success
            }
        }
    }

    private fun loadCloudProvidersFromDisk() {
        val configDir = java.io.File(filesDir, "config")
        val file = java.io.File(configDir, "providers.json")
        if (file.exists()) {
            try {
                val json = file.readText()
                val jsonArray = JSONArray(json)
                val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
                chatViewModel.cloudProviders.clear()
                for (i in 0 until jsonArray.length()) {
                    val obj = jsonArray.getJSONObject(i)
                    chatViewModel.cloudProviders.add(CloudProvider(
                        obj.getString("name"),
                        obj.getString("endpoint"),
                        obj.getString("model_id"),
                        obj.optString("auth_type", "api_key")
                    ))
                }
                Log.i("RoninUI", "Loaded ${jsonArray.length()} cloud providers from internal storage.")
            } catch (e: Exception) {
                Log.e("RoninUI", "Failed to load providers: ${e.message}")
            }
        }
    }

    fun saveCloudProvider(provider: CloudProvider, apiKey: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        sharedPreferences.edit().putString(provider.name, apiKey).apply()

        val existingIndex = chatViewModel.cloudProviders.indexOfFirst { it.name == provider.name }
        if (existingIndex != -1) {
            chatViewModel.cloudProviders[existingIndex] = provider
        } else {
            chatViewModel.cloudProviders.add(provider)
        }

        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val configDir = java.io.File(filesDir, "config")
                if (!configDir.exists()) configDir.mkdirs()

                val providersFile = java.io.File(configDir, "providers.json")
                val jsonArray = JSONArray()
                chatViewModel.cloudProviders.forEach { p ->
                    val obj = JSONObject()
                    obj.put("name", p.name)
                    obj.put("endpoint", p.endpoint)
                    obj.put("model_id", p.modelId)
                    obj.put("auth_type", p.authType)
                    jsonArray.put(obj)
                }
                val providersJson = jsonArray.toString().replace("\\/", "/")
                providersFile.writeText(providersJson)

                nativeEngine.updateCloudProviders(providersJson)

                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Provider ${provider.name} saved securely.", Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "Failed to save cloud config: ${e.message}")
            }
        }
    }

    fun removeLocalModel(path: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val file = java.io.File(path)
        
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                if (file.exists() && file.delete()) {
                    // Purge Metadata
                    sharedPreferences.edit().remove("fingerprint_$path").apply()
                    
                    withContext(Dispatchers.Main) {
                        chatViewModel.discoveredModels.remove(path)
                        
                        // If the deleted model was the active one, unhydrate
                        if (chatViewModel.localModelPath == path) {
                            chatViewModel.isKernelHydrated = false
                            chatViewModel.localModelPath = ""
                            sharedPreferences.edit().remove("local_model_path").apply()
                        }
                        
                        Toast.makeText(this@MainActivity, "Model purged from storage.", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Failed to delete model file.", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "Error deleting model: ${e.message}")
            }
        }
    }

    fun removeCloudProvider(name: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        chatViewModel.cloudProviders.removeIf { it.name == name }

        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val configDir = java.io.File(filesDir, "config")
                val providersFile = java.io.File(configDir, "providers.json")
                val jsonArray = JSONArray()
                chatViewModel.cloudProviders.forEach { p ->
                    val obj = JSONObject()
                    obj.put("name", p.name)
                    obj.put("endpoint", p.endpoint)
                    obj.put("model_id", p.modelId)
                    obj.put("auth_type", p.authType)
                    jsonArray.put(obj)
                }
                providersFile.writeText(jsonArray.toString())
                
                // Phase 5.1.5: Purge Metadata and Secure Credentials
                val masterKey = MasterKey.Builder(this@MainActivity)
                    .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                    .build()
                val securePrefs = EncryptedSharedPreferences.create(
                    this@MainActivity,
                    "secure_creds",
                    masterKey,
                    EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                    EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
                )
                securePrefs.edit().remove("key_$name").apply()
                
                nativeEngine.updateCloudProviders(jsonArray.toString())
                
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Provider $name and credentials purged.", Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                Log.e("RoninUI", "Failed to purge provider: ${e.message}")
            }
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
                    startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) 
                }
            }
        }
    }
}

@Composable
fun RoninChatUI(
    engine: NativeEngine, 
    chatViewModel: ChatViewModel = viewModel(), 
    modelPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>,
    onSaveOfflineMode: (Boolean) -> Unit
) {
    var inputText by remember { mutableStateOf("") }
    val messages = chatViewModel.messages
    val reasoningLogs = chatViewModel.reasoningLogs
    val context = LocalContext.current
    val scaffoldState = rememberScaffoldState()
    val chatListState = rememberLazyListState()
    val reasoningListState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    var showAddProvider by remember { mutableStateOf(false) }
    var showCommands by remember { mutableStateOf(false) }
    val commands = listOf("/status", "/skills", "/model", "/reset")
    val filteredCommands = commands.filter { it.startsWith(inputText) && it != inputText }

    LaunchedEffect(inputText) {
        showCommands = inputText.startsWith("/") && filteredCommands.isNotEmpty()
    }

    if (chatViewModel.showSettings) {
        SettingsDialog(
            onDismiss = { chatViewModel.showSettings = false },
            onSelectModel = { 
                // Phase 4.9.1: Filter for octet-stream and specific extensions
                modelPicker.launch(arrayOf("application/octet-stream", "application/x-binary")) 
            },
            currentModelPath = chatViewModel.localModelPath,
            providers = chatViewModel.cloudProviders,
            discoveredModels = chatViewModel.discoveredModels,
            primaryProvider = chatViewModel.primaryCloudProvider,
            onPrimaryProviderChange = {
                chatViewModel.primaryCloudProvider = it
                engine.setPrimaryCloudProvider(it)
                (context as? MainActivity)?.savePrimaryCloudProvider(it)
            },
            onDeleteProvider = { (context as? MainActivity)?.removeCloudProvider(it) },
            onDeleteModel = { (context as? MainActivity)?.removeLocalModel(it) },
            onAddProvider = { showAddProvider = true },

            offlineMode = chatViewModel.offlineMode,
            onOfflineModeChange = { 
                chatViewModel.offlineMode = it
                engine.setOfflineMode(it)
                onSaveOfflineMode(it)
            }
        )
    }

    if (showAddProvider) {
        AddProviderDialog(
            engine = engine,
            onDismiss = { showAddProvider = false },
            onSave = { provider, key ->
                (context as? MainActivity)?.saveCloudProvider(provider, key)
            }
        )
    }

    LaunchedEffect(Unit) {
        engine.onKernelMessage = { message ->
            if (message.startsWith("[STREAM]")) {
                val token = message.substringAfter("[STREAM]")
                if (messages.isNotEmpty() && messages.last().startsWith("Ronin:")) {
                    val lastMsg = messages.last()
                    messages[messages.size - 1] = lastMsg + token
                } else {
                    messages.add("Ronin: $token")
                }
            } else if (message.startsWith("[THINKING]")) {
                // Phase 4.6.9 Preview: Thinking UI Readiness
                val thought = message.substringAfter("[THINKING]")
                reasoningLogs.add(0, "> THOUGHT: $thought")
            } else {
                reasoningLogs.add(0, message)
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
                val newHistory: List<Pair<String, String>> = engine.getChatHistoryAsync(pageSize, offset)
                if (newHistory.isEmpty()) {
                    chatViewModel.hasMoreHistory = false
                } else {
                    for (pair in newHistory.reversed()) {
                        val (role, content) = pair
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
    
    LaunchedEffect(chatListState.firstVisibleItemIndex) {
        if (chatListState.firstVisibleItemIndex == 0 && messages.isNotEmpty() && !chatViewModel.isLoadingHistory) {
            loadNextHistoryPage()
        }
    }

    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            val layoutInfo = chatListState.layoutInfo
            val visibleItemsInfo = layoutInfo.visibleItemsInfo
            val lastMsg = messages.last()
            val isUserMsg = lastMsg.startsWith("User:")
            val isAtBottom = if (visibleItemsInfo.isEmpty()) {
                true 
            } else {
                visibleItemsInfo.last().index >= layoutInfo.totalItemsCount - 2
            }
            if (isUserMsg || isAtBottom) {
                chatListState.animateScrollToItem(messages.size - 1)
            }
        }
    }

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
                    chatViewModel.lmkPressure = engine.getLMKPressure()
                    chatViewModel.stability = (100 - chatViewModel.lmkPressure) / 100.0f
                }
                
                engine.updateSystemHealth(temp, usedRAM, totalRAM)
                
                // Delay increased to 5 seconds to reduce JNI/Context overhead
                delay(5000)
            }
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        topBar = {
            TopAppBar(
                title = { 
                    Row(verticalAlignment = Alignment.CenterVertically) { 
                        Text("Ronin Kernel v4.4-DYNAMIC")
                        Spacer(Modifier.width(8.dp))
                        StabilityHeartbeat(chatViewModel.lmkPressure) 
                    } 
                },
                actions = {
                    IconButton(onClick = { chatViewModel.showSettings = true }) { 
                        Icon(Icons.Default.Settings, "Settings", tint = Color.White) 
                    }
                    IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) { 
                        Icon(Icons.Default.Info, "Info", tint = if (chatViewModel.showSysInfo) Color.Cyan else Color.Gray) 
                    }
                    StabilityMeter(chatViewModel.stability)
                },
                backgroundColor = Color(0xFF121212),
                contentColor = Color.White,
                elevation = 8.dp
            )
        },
        backgroundColor = Color(0xFF1A1A1A)
    ) { paddingValues ->
        Column(modifier = Modifier.fillMaxSize().padding(paddingValues)) {
            ContextTimeline(chatViewModel.l1Count, chatViewModel.l2Count, chatViewModel.l3Count)
            
            if (chatViewModel.isLoadingHistory) {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth().height(2.dp), 
                    color = Color.Cyan, 
                    backgroundColor = Color.Transparent
                )
            }
            
            LazyColumn(
                state = chatListState, 
                modifier = Modifier.weight(1f).fillMaxWidth().padding(horizontal = 16.dp), 
                verticalArrangement = Arrangement.Bottom
            ) {
                items(messages) { msg -> ChatBubble(msg) }
            }
            
            ReasoningConsole(reasoningLogs, reasoningListState)
            
            AnimatedVisibility(visible = showCommands) {
                Surface(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                    elevation = 8.dp,
                    shape = RoundedCornerShape(topStart = 12.dp, topEnd = 12.dp),
                    color = MaterialTheme.colors.surface
                ) {
                    Column {
                        filteredCommands.forEach { cmd ->
                            Text(
                                text = cmd,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { 
                                        inputText = cmd
                                        showCommands = false
                                    }
                                    .padding(12.dp),
                                color = MaterialTheme.colors.onSurface
                            )
                        }
                    }
                }
            }

            ChatInput(value = inputText, onValueChange = { inputText = it }, onSend = {
                if (inputText.isNotBlank()) {
                    val isCommand = inputText.trim().startsWith("/")
                    
                    // Phase 4.8.5: Smart Bypass
                    // Block only if local inference is required but not ready
                    if (!isCommand && chatViewModel.offlineMode && !chatViewModel.isKernelHydrated) {
                        Toast.makeText(context, "Local Inference Blocked: Model Not Hydrated.", Toast.LENGTH_SHORT).show()
                        return@ChatInput
                    }

                    messages.add("User: $inputText")
                    val currentInput = inputText
                    inputText = ""
                    chatViewModel.l1Count = 0
                    chatViewModel.l2Count = 0
                    chatViewModel.l3Count = 0
                    scope.launch {
                        if (currentInput.trim().lowercase() == "/history") { 
                            loadNextHistoryPage()
                            return@launch 
                        }
                        val kernelRawOutput = engine.processInputAsync(currentInput)
                        val kernelOutput = try { 
                            if (kernelRawOutput.startsWith("{")) { 
                                val start = kernelRawOutput.indexOf("\"result\": \"") + 11
                                val end = kernelRawOutput.lastIndexOf("\"")
                                if (start in 11 until end) {
                                    val result = kernelRawOutput.substring(start, end)
                                    if (result.startsWith("[DONE]") || result.startsWith("[STREAM_COMPLETE]")) {
                                        // Already handled by stream tokens
                                        ""
                                    } else {
                                        result
                                    }
                                } else {
                                    kernelRawOutput
                                }
                            } else {
                                if (kernelRawOutput.startsWith("[DONE]") || kernelRawOutput.startsWith("[STREAM_COMPLETE]")) "" else kernelRawOutput 
                            }
                        } catch (e: Exception) { 
                            kernelRawOutput 
                        }
                        if (kernelOutput.isNotEmpty()) {
                            messages.add("Ronin: $kernelOutput")
                        }
                        launch { 
                            delay(100)
                            chatListState.animateScrollToItem(messages.size - 1) 
                        }
                    }
                }
            })
            
            if (chatViewModel.showSysInfo) {
                SystemHealthOverlay(chatViewModel.temperature, chatViewModel.ramUsedGB, chatViewModel.ramTotalGB)
            }
        }
    }
}

@Composable
fun SystemHealthOverlay(temp: Float, used: Float, total: Float) {
    Surface(modifier = Modifier.fillMaxWidth(), color = Color(0xFF121212), elevation = 4.dp) {
        Row(modifier = Modifier.padding(8.dp).fillMaxWidth(), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
            Text(text = "${"%.1f".format(temp)}°C | RAM: ${"%.2f".format(used)}/${"%.2f".format(total)} GB", color = Color.Green, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        }
    }
}

@Composable
fun StabilityHeartbeat(pressure: Int) {
    val color = when { pressure < 30 -> Color.Green; pressure < 70 -> Color.Yellow; else -> Color.Red }
    Icon(Icons.Default.Favorite, "Heartbeat", tint = color, modifier = Modifier.size(18.dp))
}

@Composable
fun ContextTimeline(l1: Int, l2: Int, l3: Int) {
    Row(modifier = Modifier.fillMaxWidth().background(Color(0xFF252525)).padding(8.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
        TimelineZone("L1 (Active)", l1, Color.Cyan)
        TimelineZone("L2 (Compressed)", l2, Color.Yellow)
        TimelineZone("L3 (Deep)", l3, Color.Magenta)
    }
}

@Composable
fun TimelineZone(label: String, count: Int, color: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, style = MaterialTheme.typography.caption, color = Color.Gray)
        Text("$count items", color = color, fontSize = 12.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun ReasoningConsole(logs: List<String>, scrollState: LazyListState) {
    var expanded by remember { mutableStateOf(false) }
    Column(modifier = Modifier.fillMaxWidth().padding(8.dp).clip(RoundedCornerShape(8.dp)).background(Color(0xFF222222)).animateContentSize()) {
        Row(modifier = Modifier.clickable { expanded = !expanded }.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(if (expanded) Icons.Default.KeyboardArrowDown else Icons.Default.KeyboardArrowUp, "Expand", tint = Color.Gray)
            Spacer(Modifier.width(8.dp)); Text("Reasoning Console", color = Color.Gray, fontSize = 12.sp)
        }
        if (expanded) {
            Box(modifier = Modifier.height(120.dp).padding(horizontal = 12.dp, vertical = 4.dp)) {
                LazyColumn(state = scrollState) {
                    items(logs) { log -> 
                        val isCommand = log.startsWith("[COMMAND]")
                        val displayLog = if (isCommand) log.substringAfter("[COMMAND] ") else log
                        val textColor = if (isCommand) Color.Cyan else Color(0xFF00FF00)
                        
                        Text(
                            text = "> $displayLog", 
                            color = textColor, 
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
    Box(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), contentAlignment = if (isUser) Alignment.CenterEnd else Alignment.CenterStart) {
        Text(
            text = text, 
            modifier = Modifier
                .clip(RoundedCornerShape(12.dp))
                .background(if (isUser) MaterialTheme.colors.primary else MaterialTheme.colors.surface)
                .padding(12.dp), 
            color = MaterialTheme.colors.onPrimary.takeIf { isUser } ?: MaterialTheme.colors.onSurface, 
            fontSize = 14.sp
        )
    }
}

@Composable
fun ChatInput(value: String, onValueChange: (String) -> Unit, onSend: () -> Unit) {
    Surface(elevation = 8.dp, color = MaterialTheme.colors.surface) {
        Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextField(
                value = value, 
                onValueChange = onValueChange, 
                modifier = Modifier.weight(1f), 
                colors = TextFieldDefaults.textFieldColors(
                    backgroundColor = MaterialTheme.colors.onSurface.copy(alpha = 0.1f), 
                    textColor = MaterialTheme.colors.onSurface, 
                    focusedIndicatorColor = Color.Transparent, 
                    unfocusedIndicatorColor = Color.Transparent
                ), 
                shape = RoundedCornerShape(24.dp), 
                placeholder = { Text("Ask Ronin...", color = MaterialTheme.colors.onSurface.copy(alpha = 0.4f)) }
            )
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = onSend, 
                shape = RoundedCornerShape(24.dp), 
                colors = ButtonDefaults.buttonColors(backgroundColor = MaterialTheme.colors.primary)
            ) { 
                Text("Send", color = MaterialTheme.colors.onPrimary) 
            }
        }
    }
}

@Composable
fun SettingsDialog(
    onDismiss: () -> Unit,
    onSelectModel: () -> Unit,
    currentModelPath: String,
    providers: List<CloudProvider>,
    discoveredModels: List<String>,
    primaryProvider: String,
    onPrimaryProviderChange: (String) -> Unit,
    onDeleteProvider: (String) -> Unit,
    onDeleteModel: (String) -> Unit,
    onAddProvider: () -> Unit,
    offlineMode: Boolean,
    onOfflineModeChange: (Boolean) -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text("Ronin Kernel Settings", color = MaterialTheme.colors.onSurface)
        },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Offline-only Mode", modifier = Modifier.weight(1f), color = MaterialTheme.colors.onSurface)
                    Switch(checked = offlineMode, onCheckedChange = onOfflineModeChange)
                }
                Spacer(Modifier.height(8.dp))
                Text("Reasoning Brains", fontWeight = FontWeight.Bold, color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f))
                if (discoveredModels.isEmpty()) {
                    Text("No models detected in internal storage.", color = Color.Red, fontSize = 10.sp)
                } else {
                    discoveredModels.forEach { path ->
                        val name = path.substringAfterLast("/")
                        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                            RadioButton(selected = path == currentModelPath, onClick = { /* Picked via OpenDocument */ })
                            Text(name, fontSize = 10.sp, color = MaterialTheme.colors.onSurface, modifier = Modifier.weight(1f))
                            IconButton(onClick = { onDeleteModel(path) }) {
                                Icon(Icons.Default.Delete, contentDescription = "Delete", tint = Color.Red.copy(alpha = 0.5f), modifier = Modifier.size(16.dp))
                            }
                        }
                    }
                }
                Button(onClick = onSelectModel, modifier = Modifier.padding(top = 4.dp)) {
                    Text("Load External Model")
                }

                Spacer(Modifier.height(16.dp))
                Text("Cloud Registry", fontWeight = FontWeight.Bold, color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f))
                providers.forEach { provider ->
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)
                    ) {
                        RadioButton(selected = provider.name == primaryProvider, onClick = { onPrimaryProviderChange(provider.name) })
                        Text("${provider.name} (${provider.modelId})", modifier = Modifier.weight(1f).clickable { onPrimaryProviderChange(provider.name) }, color = MaterialTheme.colors.onSurface)
                        IconButton(onClick = { onDeleteProvider(provider.name) }) {
                            Icon(Icons.Default.Delete, contentDescription = "Delete", tint = Color.Red.copy(alpha = 0.7f))
                        }
                    }
                }
                Button(onClick = onAddProvider, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
                    Text("Add Live Provider")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { 
                Text("Close") 
            } 
        }, 
        backgroundColor = MaterialTheme.colors.surface, 
        contentColor = MaterialTheme.colors.onSurface
    )
}

@Composable
fun AddProviderDialog(engine: NativeEngine, onDismiss: () -> Unit, onSave: (CloudProvider, String) -> Unit) {
    var name by remember { mutableStateOf("") }
    var endpoint by remember { mutableStateOf("") }
    var modelId by remember { mutableStateOf("") }
    var apiKey by remember { mutableStateOf("") }
    var expanded by remember { mutableStateOf(false) }
    var isFetching by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    
    val fetchedModels = remember { mutableStateListOf<JSONObject>() }

    AlertDialog(
        onDismissRequest = onDismiss, 
        title = { 
            Text("Add Cloud Provider (Phase 5.0)", color = MaterialTheme.colors.onSurface) 
        }, 
        text = {
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextField(
                        value = apiKey, 
                        onValueChange = { apiKey = it }, 
                        label = { Text("API Key (Required to Fetch)") },
                        modifier = Modifier.weight(1f),
                        colors = TextFieldDefaults.textFieldColors(textColor = MaterialTheme.colors.onSurface)
                    )
                    Spacer(Modifier.width(8.dp))
                    Button(
                        onClick = {
                            if (apiKey.isNotEmpty()) {
                                isFetching = true
                                scope.launch {
                                    val models = engine.fetchAvailableModels(apiKey)
                                    fetchedModels.clear()
                                    fetchedModels.addAll(models)
                                    isFetching = false
                                    if (models.isNotEmpty()) expanded = true
                                }
                            }
                        },
                        enabled = !isFetching
                    ) {
                        Text(if (isFetching) "..." else "Fetch")
                    }
                }
                
                Spacer(Modifier.height(8.dp))
                
                Box {
                    OutlinedButton(onClick = { if (fetchedModels.isNotEmpty()) expanded = true }, modifier = Modifier.fillMaxWidth()) {
                        Text(if (name.isEmpty()) "Select Live Model" else "Model: $name")
                    }
                    DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                        fetchedModels.forEach { model ->
                            val displayName = model.optString("displayName", "Unknown")
                            val id = model.getString("name").substringAfter("models/")
                            DropdownMenuItem(onClick = {
                                name = displayName
                                modelId = id
                                endpoint = "https://generativelanguage.googleapis.com/v1/models/$id:generateContent"
                                expanded = false
                            }) {
                                Text(displayName)
                            }
                        }
                    }
                }
                
                Spacer(Modifier.height(8.dp))
                
                TextField(
                    value = endpoint, 
                    onValueChange = { endpoint = it }, 
                    label = { Text("Endpoint URL") },
                    colors = TextFieldDefaults.textFieldColors(textColor = MaterialTheme.colors.onSurface)
                )
                TextField(
                    value = modelId, 
                    onValueChange = { modelId = it }, 
                    label = { Text("Model ID") },
                    colors = TextFieldDefaults.textFieldColors(textColor = MaterialTheme.colors.onSurface)
                )
            }
        }, 
        confirmButton = { 
            Button(onClick = { 
                if (apiKey.isNotEmpty() && name.isNotEmpty()) {
                    // Sync Registry to Kernel (Requirement 2)
                    val metadataJson = JSONArray().apply {
                        fetchedModels.forEach { put(it) }
                    }.toString()
                    engine.updateModelRegistry(metadataJson)
                    
                    onSave(CloudProvider(name, endpoint, modelId, "api_key"), apiKey)
                    onDismiss()
                }
            }) { 
                Text("Verify & Save") 
            } 
        }, 
        dismissButton = { 
            TextButton(onClick = onDismiss) { 
                Text("Cancel") 
            } 
        }, 
        backgroundColor = MaterialTheme.colors.surface, 
        contentColor = MaterialTheme.colors.onSurface
    )
}

@Composable
fun StabilityMeter(stability: Float) {
    val color = when { stability > 0.7f -> Color.Green; stability > 0.4f -> Color.Yellow; else -> Color.Red }
    Column(horizontalAlignment = Alignment.End, modifier = Modifier.padding(end = 16.dp)) {
        Text("Stability", style = MaterialTheme.typography.caption, color = MaterialTheme.colors.onSurface)
        LinearProgressIndicator(progress = stability, color = color, backgroundColor = MaterialTheme.colors.onSurface.copy(alpha = 0.1f), modifier = Modifier.width(100.dp))
    }
}
