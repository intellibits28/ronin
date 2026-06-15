package com.ronin.kernel

import android.os.Bundle
import android.widget.Toast
import android.content.Context
import android.content.ContextWrapper
import android.app.ActivityManager
import android.os.BatteryManager
import android.content.IntentFilter
import android.hardware.camera2.CameraManager
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.CancellationTokenSource
import com.google.android.gms.tasks.Tasks
import android.os.Build
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.FragmentActivity
import androidx.compose.animation.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
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
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import android.provider.ContactsContract
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class CloudProvider(
    val name: String, 
    val providerType: String,
    val endpoint: String,
    val modelId: String,
    val authType: String
)

class ChatMessage(
    val id: Long,
    val sender: String,
    initialContent: String,
    initialIsThinking: Boolean = false,
    initialThoughtContent: String = "",
    var sessionId: String = "" // v10.2.17
) {
    var content by mutableStateOf(initialContent)
    var isThinking by mutableStateOf(initialIsThinking)
    var thoughtContent by mutableStateOf(initialThoughtContent)
    var isTruncated by mutableStateOf(false)
    var isContinuing by mutableStateOf(false)
    var feedbackGiven by mutableStateOf(false) // v10.2.17

    fun copy(
        content: String = this.content,
        isThinking: Boolean = this.isThinking,
        thoughtContent: String = this.thoughtContent,
        isTruncated: Boolean = this.isTruncated,
        isContinuing: Boolean = this.isContinuing
    ) = ChatMessage(id, sender, content, isThinking, thoughtContent).apply {
        this.isTruncated = isTruncated
        this.isContinuing = isContinuing
    }
}

enum class WizardState { MISSING_CORE, IMPORTING, VERIFYING, ACTIVE }

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<ChatMessage>()
    var reasoningLogsText by mutableStateOf("") // v3.5: Unified console text
    
    var showSysInfo by mutableStateOf(false)
    var showReasoning by mutableStateOf(false) // Default hide reasoning console
    var lmkPressure by mutableStateOf(0)
    
    var wizardState by mutableStateOf(WizardState.MISSING_CORE)
    var isGemmaReady by mutableStateOf(false)
    var showCommandSuggestions by mutableStateOf(false)
    
    // v7.0: Agent Mode HITL State
    var showHITLDialog by mutableStateOf(false)
    var hitlMessage by mutableStateOf("")
    var hitlIntentName by mutableStateOf("")
    var onHITLResult: ((Boolean) -> Unit)? = null
    
    var samplingTemperature by mutableStateOf(0.7f)
    var topK by mutableStateOf(40)
    var topP by mutableStateOf(0.9f)
    var maxTokens by mutableStateOf(1024)
    var isThinkingEnabled by mutableStateOf(true)

    var systemTemperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)

    var offlineMode by mutableStateOf(false)
    var cloudOnlyMode by mutableStateOf(false)
    var localModelPath by mutableStateOf("")
    var primaryCloudProvider by mutableStateOf("Gemini")
    val cloudProviders = mutableStateListOf<CloudProvider>()
    val discoveredModels = mutableStateListOf<String>()

    // Phase 5: Developer HUD State
    var showDevHUD by mutableStateOf(false)
    var hudIntent by mutableStateOf("NONE")
    var hudConfidence by mutableStateOf(0.0f)
    var hudPlan by mutableStateOf("")
    var hudState by mutableStateOf("IDLE")
    
    // v1.0 Sensor DSP HUD
    var sensorFreqHz by mutableStateOf(0.0f)
    var sensorPsdDb by mutableStateOf(-100.0f)
    var sensorAnomaly by mutableStateOf(false)

    var systemPrompt by mutableStateOf("You are Ronin. Always reason inside [THINK] [/THINK] and then reply inside [REPLY] [/REPLY] in Myanmar.")

    var showAddCloudDialog by mutableStateOf(false)
    var editingProvider by mutableStateOf<CloudProvider?>(null)
    var isSelectingType by mutableStateOf(true)
    var fetchedModels = mutableStateListOf<String>()
    var isFetchingModels by mutableStateOf(false)
    var kernelStatus by mutableStateOf("Initializing...")
    var isGenerating by mutableStateOf(false)
}

class MainActivity : FragmentActivity() {
    internal lateinit var nativeEngine: NativeEngine
    private lateinit var sensorDriver: SensorDriver
    private lateinit var sharedPreferences: android.content.SharedPreferences
    private lateinit var fusedLocationClient: FusedLocationProviderClient
    // ... rest of the class remains same but I need to include the modified methods
    // I will replace larger blocks to be safe.

    private val brainPicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { importModelFromUri(it) }
    }

    private fun importModelFromUri(uri: Uri) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            chatViewModel.wizardState = WizardState.IMPORTING
            val success = withContext(Dispatchers.IO) {
                try {
                    val returnCursor = contentResolver.query(uri, null, null, null, null)
                    val nameIndex = returnCursor?.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    returnCursor?.moveToFirst()
                    val fileName = nameIndex?.let { returnCursor.getString(it) } ?: "imported_model.litertlm"
                    returnCursor?.close()
                    val inputStream = contentResolver.openInputStream(uri)
                    val targetFile = File(File(filesDir, "models").apply { if (!exists()) mkdirs() }, fileName)
                    inputStream?.use { input -> java.io.FileOutputStream(targetFile).use { output -> input.copyTo(output) } }
                    nativeEngine.isValidModel(targetFile.absolutePath)
                } catch (e: Exception) { false }
            }
            if (success) { scanLocalModels(); Toast.makeText(this@MainActivity, "Brain Imported", Toast.LENGTH_SHORT).show() }
            else { chatViewModel.wizardState = WizardState.MISSING_CORE; Toast.makeText(this@MainActivity, "Import Failed", Toast.LENGTH_LONG).show() }
        }
    }

    fun scanLocalModels() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val modelFiles = File(filesDir, "models").apply { if (!exists()) mkdirs() }.listFiles { file -> !file.isDirectory && file.length() > 1024 && (file.name.endsWith(".litertlm") || file.name.endsWith(".bin")) } ?: emptyArray()
        val uniquePaths = modelFiles.map { it.absolutePath }.distinct().sorted()
        if (chatViewModel.discoveredModels.toList() != uniquePaths) { chatViewModel.discoveredModels.clear(); chatViewModel.discoveredModels.addAll(uniquePaths) }
        chatViewModel.wizardState = if (uniquePaths.isNotEmpty()) WizardState.ACTIVE else WizardState.MISSING_CORE
    }

    fun updateSensorGuardrails(temp: Float, batteryPct: Int, isCharging: Boolean) {
        if (::sensorDriver.isInitialized) {
            sensorDriver.updateGuardrails(temp, batteryPct, isCharging)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        nativeEngine = NativeEngine(this)
        sensorDriver = SensorDriver(this, nativeEngine)
        val masterKey = MasterKey.Builder(this).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build()
        sharedPreferences = EncryptedSharedPreferences.create(this, "ronin_secure_prefs", masterKey, EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV, EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM)
        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)
        copyAssetsToFilesDir()

        // Load settings
        chatViewModel.systemPrompt = sharedPreferences.getString("system_prompt", chatViewModel.systemPrompt) ?: chatViewModel.systemPrompt
        chatViewModel.maxTokens = sharedPreferences.getInt("max_tokens", 1024)
        chatViewModel.samplingTemperature = sharedPreferences.getFloat("temperature", 0.7f)
        chatViewModel.topK = sharedPreferences.getInt("top_k", 40)
        chatViewModel.topP = sharedPreferences.getFloat("top_p", 0.9f)
        chatViewModel.isThinkingEnabled = sharedPreferences.getBoolean("is_thinking_enabled", true)
        chatViewModel.cloudOnlyMode = sharedPreferences.getBoolean("cloud_only_mode", false)

        lifecycleScope.launch(Dispatchers.Main) {
            chatViewModel.kernelStatus = "Booting Engine..."
            NativeEngine.initializeAsync(); nativeEngine.initialize()
            setupHardwareCallbacks(); loadCloudProvidersFromDisk()
            nativeEngine.loadCapabilities(File(filesDir, "assets/capabilities.json").absolutePath)
            nativeEngine.loadMyanmarDictionary(File(filesDir, "assets/myanmar_dictionary.txt").absolutePath)
            savePrimaryCloudProvider(sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini")
            saveOfflineMode(sharedPreferences.getBoolean("offline_mode", false))
            nativeEngine.updateSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, chatViewModel.topP)

            chatViewModel.kernelStatus = "Neural Bridge Active"
            checkAndRequestPermissions()
            scanFilesSmart() // v11.0 Smart Indexing
            startWorldStateInjection() // v11.3: Start cognitive telemetry
            scanLocalModels()
            val savedModelPath = sharedPreferences.getString("local_model_path", "")
            if (!savedModelPath.isNullOrEmpty() && File(savedModelPath).exists()) hydrateModel(savedModelPath)

            launch {
                nativeEngine.inferenceFlow.collect { packet ->
                    // v10.2.3: Process fragments even if final, and ensure UI thread safety
                    withContext(Dispatchers.Main) {
                        if (chatViewModel.messages.isNotEmpty()) {
                            val lastMsg = chatViewModel.messages.last()
                            if (lastMsg.sender == "Ronin") {
                                val frag = packet.fragment
                                if (frag.isNotEmpty()) {
                                    var processedFrag = frag
                                    if (processedFrag.contains("[THINK]")) {
                                        lastMsg.isThinking = true
                                        processedFrag = processedFrag.replace("[THINK]", "")
                                    }

                                    if (lastMsg.isThinking) {
                                        if (processedFrag.contains("[/THINK]") || processedFrag.contains("[REPLY]")) {
                                            val splitTag = if (processedFrag.contains("[REPLY]")) "[REPLY]" else "[/THINK]"
                                            val thoughtPart = processedFrag.substringBefore(splitTag)
                                            val replyPart = processedFrag.substringAfter(splitTag)
                                            lastMsg.thoughtContent += thoughtPart
                                            if (chatViewModel.isThinkingEnabled) chatViewModel.reasoningLogsText += thoughtPart
                                            lastMsg.isThinking = false
                                            lastMsg.content += replyPart.replace("[/REPLY]", "")
                                        } else {
                                            lastMsg.thoughtContent += processedFrag
                                            if (chatViewModel.isThinkingEnabled) chatViewModel.reasoningLogsText += processedFrag
                                        }
                                    } else {
                                        val cleanReply = processedFrag.replace("[REPLY]", "").replace("[/REPLY]", "").replace("[/THINK]", "")
                                        lastMsg.content += cleanReply
                                    }
                                }
                                
                                if (packet.isFinal) {
                                    chatViewModel.isGenerating = false
                                    val text = lastMsg.content.trim()
                                    val endMarkers = listOf("။", ".", "?", "!", "\"", ")", "]", "}", "*", "_", ">", "`", ":", ";", "၊", "}")
                                    lastMsg.isTruncated = text.isNotEmpty() && endMarkers.none { text.endsWith(it) }
                                }
                            }
                        }
                    }
                }
            }
        }

        setContent {
            LaunchedEffect(Unit) {
                while(true) {
                    val loaded = nativeEngine.isLoaded()
                    if (chatViewModel.isGemmaReady != loaded) {
                        chatViewModel.isGemmaReady = loaded
                        chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                        chatViewModel.kernelStatus = if (loaded) "Kernel Ready" else "Bridge Active"
                    }
                    delay(3000)
                }
            }
            RoninChatUI(nativeEngine, chatViewModel, brainPicker, { saveOfflineMode(it) })
        }
    }

    private fun copyAssetsToFilesDir() {
        try {
            val assetsDir = File(filesDir, "assets").apply { if (!exists()) mkdirs() }
            val capFile = File(assetsDir, "capabilities.json")
            // v5.2: Force overwrite capabilities to sync keyword changes
            assets.open("capabilities.json").use { input -> java.io.FileOutputStream(capFile).use { output -> input.copyTo(output) } }
            
            val dictFile = File(assetsDir, "myanmar_dictionary.txt")
            if (!dictFile.exists()) assets.open("myanmar_dictionary.txt").use { input -> java.io.FileOutputStream(dictFile).use { output -> input.copyTo(output) } }
        } catch (e: Exception) { Log.e("RoninBoot", "Asset copy failed.") }
    }

    private fun loadCloudProvidersFromDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = File(filesDir, "config").apply { if (!exists()) mkdirs() }
        val providersFile = File(configDir, "providers.json")
        if (providersFile.exists()) {
            try {
                val array = JSONArray(providersFile.readText())
                chatViewModel.cloudProviders.clear()
                for (i in 0 until array.length()) { 
                    val obj = array.getJSONObject(i)
                    chatViewModel.cloudProviders.add(CloudProvider(obj.getString("name"), obj.optString("providerType", "Gemini"), obj.getString("endpoint"), obj.getString("modelId"), obj.getString("authType"))) 
                }
                nativeEngine.updateCloudProvidersSafe(array.toString())
            } catch (e: Exception) {}
        } else {
            // v3.5: Ensure default providers are available
            chatViewModel.cloudProviders.add(CloudProvider("Gemini", "Gemini", "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent", "gemini-1.5-flash", "key"))
            saveCloudProvidersToDisk()
        }
    }

    private fun saveCloudProvidersToDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = File(filesDir, "config").apply { if (!exists()) mkdirs() }
        try {
            val array = JSONArray()
            chatViewModel.cloudProviders.forEach { p -> array.put(JSONObject().put("name", p.name).put("providerType", p.providerType).put("endpoint", p.endpoint).put("modelId", p.modelId).put("authType", p.authType)) }
            File(configDir, "providers.json").writeText(array.toString(2))
            nativeEngine.updateCloudProvidersSafe(array.toString())
        } catch (e: Exception) {}
    }

    fun hydrateModel(path: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            chatViewModel.kernelStatus = "Hydrating..."
            if (nativeEngine.loadModel(path)) {
                chatViewModel.isGemmaReady = true
                sharedPreferences.edit().putString("local_model_path", path).apply()
                chatViewModel.localModelPath = path; chatViewModel.kernelStatus = "Kernel Ready"
            } else { Toast.makeText(this@MainActivity, "Hydration Failed", Toast.LENGTH_SHORT).show(); chatViewModel.kernelStatus = "Bridge Active" }
        }
    }

    fun deleteLocalModel(path: String) { if (File(path).delete()) { Toast.makeText(this, "Deleted", Toast.LENGTH_SHORT).show(); scanLocalModels() } }
    fun clearModelCache() { File(filesDir, "models/compiled_cache").deleteRecursively(); codeCacheDir.deleteRecursively(); Toast.makeText(this, "Cache Cleared", Toast.LENGTH_SHORT).show() }
    fun getApiKey(provider: String): String = sharedPreferences.getString(provider, "") ?: ""
    fun savePrimaryCloudProvider(name: String) { sharedPreferences.edit().putString("primary_cloud_provider", name).apply(); nativeEngine.setPrimaryCloudProviderSafe(name) }
    fun saveApiKey(provider: String, key: String) { sharedPreferences.edit().putString(provider, key).apply() }
    fun addCloudProvider(n: String, t: String, e: String, m: String) { val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]; if (chatViewModel.cloudProviders.none { it.name == n }) { chatViewModel.cloudProviders.add(CloudProvider(n, t, e, m, "bearer")); saveCloudProvidersToDisk() } }
    fun deleteCloudProvider(name: String) { val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]; chatViewModel.cloudProviders.removeAll { it.name == name }; saveCloudProvidersToDisk() }
    fun saveOfflineMode(offline: Boolean) { sharedPreferences.edit().putBoolean("offline_mode", offline).apply(); nativeEngine.setOfflineModeSafe(offline) }
    fun saveSystemPrompt(p: String) { sharedPreferences.edit().putString("system_prompt", p).apply() }
    fun saveMaxTokens(t: Int) { sharedPreferences.edit().putInt("max_tokens", t).apply() }
    fun saveSamplingParams(t: Float, k: Int, p: Float) { sharedPreferences.edit().putFloat("temperature", t).putInt("top_k", k).putFloat("top_p", p).apply(); nativeEngine.updateSamplingParams(t, k, p) }
    fun saveThinkingToggle(enabled: Boolean) { sharedPreferences.edit().putBoolean("is_thinking_enabled", enabled).apply() }
    fun saveCloudOnlyMode(enabled: Boolean) { sharedPreferences.edit().putBoolean("cloud_only_mode", enabled).apply() }

    fun fetchModels(provider: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val apiKey = getApiKey(provider)
        if (apiKey.isEmpty()) { Toast.makeText(this, "API Key Required", Toast.LENGTH_SHORT).show(); return }
        
        lifecycleScope.launch {
            chatViewModel.isFetchingModels = true
            chatViewModel.fetchedModels.clear()
            val result = nativeEngine.fetchAvailableModels(apiKey, provider)
            if (result.error == null) {
                result.models.forEach { m -> 
                    val id = if (provider.equals("Gemini", true)) m.getString("name").removePrefix("models/") else m.getString("id")
                    chatViewModel.fetchedModels.add(id)
                }
            } else { Toast.makeText(this@MainActivity, "Fetch Failed: ${result.error}", Toast.LENGTH_SHORT).show() }
            chatViewModel.isFetchingModels = false
        }
    }

    fun scanFilesSmart() {
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val paths = mutableListOf<String>()
                val names = mutableListOf<String>()
                val dates = mutableListOf<Long>()
                
                val projection = arrayOf(
                    android.provider.MediaStore.Files.FileColumns.DATA,
                    android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME,
                    android.provider.MediaStore.Files.FileColumns.DATE_MODIFIED
                )
                
                // Filter for common document extensions anywhere on storage
                val selection = "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.md' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.txt' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.pdf' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.doc%'"
                
                val cursor = contentResolver.query(
                    android.provider.MediaStore.Files.getContentUri("external"),
                    projection,
                    selection,
                    null,
                    "${android.provider.MediaStore.Files.FileColumns.DATE_MODIFIED} DESC"
                )

                cursor?.use {
                    val dataIndex = it.getColumnIndexOrThrow(android.provider.MediaStore.Files.FileColumns.DATA)
                    val nameIndex = it.getColumnIndexOrThrow(android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME)
                    val dateIndex = it.getColumnIndexOrThrow(android.provider.MediaStore.Files.FileColumns.DATE_MODIFIED)
                    
                    while (it.moveToNext()) {
                        paths.add(it.getString(dataIndex))
                        names.add(it.getString(nameIndex))
                        dates.add(it.getLong(dateIndex))
                    }
                }
                
                if (paths.isNotEmpty()) {
                    nativeEngine.indexFilesSafe(paths.toTypedArray(), names.toTypedArray(), dates.toLongArray())
                    Log.i("RoninKernel_MainActivity", "Smart Indexing Complete: ${paths.size} files discovered via MediaStore.")
                }
            } catch (e: Exception) {
                Log.e("RoninKernel_MainActivity", "Smart Indexing FAILED: ${e.message}")
            }
        }
    }

    private fun startWorldStateInjection() {
        lifecycleScope.launch(Dispatchers.Default) {
            val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val memoryInfo = ActivityManager.MemoryInfo()
            
            while (true) {
                try {
                    val batteryStatus = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
                    val level = batteryStatus?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
                    val scale = batteryStatus?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
                    val batteryPct = level * 100 / scale.toFloat()
                    val isCharging = batteryStatus?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) == BatteryManager.BATTERY_STATUS_CHARGING

                    activityManager.getMemoryInfo(memoryInfo)
                    val availableRamGB = memoryInfo.availMem / (1024f * 1024f * 1024f)
                    
                    val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
                    val netInfo = connectivityManager.activeNetworkInfo
                    val netAvailable = netInfo != null && netInfo.isConnected

                    nativeEngine.injectWorldState(
                        batteryPct, 
                        availableRamGB * 1024f, // Send in MB as per blueprint struct
                        true, // GPS available placeholder
                        netAvailable,
                        isCharging
                    )
                } catch (e: Exception) { Log.e("RoninWorldState", "Injection error: ${e.message}") }
                delay(1000) // 1Hz injection rate (Thermal safe)
            }
        }
    }

    private fun checkAndRequestPermissions() {
        val permissions = mutableListOf(
            android.Manifest.permission.ACCESS_FINE_LOCATION,
            android.Manifest.permission.ACCESS_COARSE_LOCATION,
            android.Manifest.permission.CAMERA,
            android.Manifest.permission.SEND_SMS,
            android.Manifest.permission.READ_CONTACTS,
            android.Manifest.permission.READ_CALENDAR,
            android.Manifest.permission.WRITE_CALENDAR,
            android.Manifest.permission.READ_EXTERNAL_STORAGE
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        
        // v11.3: Use manual request to avoid 16-bit requestCode crash in FragmentActivity
        val needed = permissions.filter { checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED }
        if (needed.isNotEmpty()) {
            requestPermissions(needed.toTypedArray(), 101)
        }
        
        // v12.35: Request Manage All Files permission for Android 11+ (Required for deep search)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!android.os.Environment.isExternalStorageManager()) {
                Toast.makeText(this, "Please allow 'All Files Access' for deep file search.", Toast.LENGTH_LONG).show()
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    intent.addCategory("android.intent.category.DEFAULT")
                    intent.data = Uri.parse(String.format("package:%s", packageName))
                    startActivity(intent)
                } catch (e: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        } else {
            scanLocalModels()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 101 && grantResults.all { it == android.content.pm.PackageManager.PERMISSION_GRANTED }) {
            scanLocalModels()
        }
    }

    private fun resolveContactName(name: String): String {
        if (name.isEmpty() || name == "Unknown") return ""
        if (name.matches(Regex("^[+]?[0-9\\- ]{5,}+$"))) return name
        if (checkSelfPermission(android.Manifest.permission.READ_CONTACTS) != android.content.pm.PackageManager.PERMISSION_GRANTED) return "PERMISSION_DENIED"
        try {
            val uri = ContactsContract.CommonDataKinds.Phone.CONTENT_URI
            val projection = arrayOf(ContactsContract.CommonDataKinds.Phone.NUMBER)
            val selections = listOf("${ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME} = ?", "${ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME} LIKE ?")
            for (selection in selections) {
                val arg = if (selection.contains("LIKE")) "%$name%" else name
                contentResolver.query(uri, projection, selection, arrayOf(arg), null)?.use { cursor ->
                    if (cursor.moveToFirst()) return cursor.getString(0)
                }
            }
        } catch (e: Exception) { Log.e("RoninKernel_MainActivity", "Resolver Error: ${e.message}") }
        return "NOT_FOUND:$name"
    }

    private fun normalizeAttribute(attr: String): String {
        val lower = attr.lowercase()
        return when {
            lower.contains("birthday") || lower.contains("မွေးနေ့") -> "မွေးနေ့"
            lower.contains("plate") || lower.contains("လိုင်စင်") || lower.contains("ကားနံပါတ်") -> "ကားနံပါတ်"
            lower.contains("medicine") || lower.contains("ဆေး") -> "ဆေးအချက်အလက်"
            lower.contains("phone") || lower.contains("ဖုန်း") -> "ဖုန်းနံပါတ်"
            lower.contains("address") || lower.contains("လိပ်စာ") -> "လိပ်စာ"
            lower.contains("key") || lower.contains("api") || lower.contains("token") -> "API/Key"
            else -> attr
        }
    }

    private fun authenticateAndExecute(title: String, subtitle: String, onAuthSuccess: () -> String): String {
        val biometricManager = BiometricManager.from(this)
        if (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL) != BiometricManager.BIOMETRIC_SUCCESS) {
            return onAuthSuccess() // Fallback if no biometric set up
        }

        val latch = java.util.concurrent.CountDownLatch(1)
        var result = "Error: Authentication failed."
        
        runOnUiThread {
            try {
                val executor = ContextCompat.getMainExecutor(this)
                val biometricPrompt = BiometricPrompt(this, executor, object : BiometricPrompt.AuthenticationCallback() {
                    override fun onAuthenticationSucceeded(authResult: BiometricPrompt.AuthenticationResult) {
                        super.onAuthenticationSucceeded(authResult)
                        result = onAuthSuccess()
                        latch.countDown()
                    }
                    override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                        super.onAuthenticationError(errorCode, errString)
                        result = "Error: $errString"
                        latch.countDown()
                    }
                    override fun onAuthenticationFailed() {
                        super.onAuthenticationFailed()
                        // Keep waiting for success or explicit error
                    }
                })

                val promptInfo = BiometricPrompt.PromptInfo.Builder()
                    .setTitle(title)
                    .setSubtitle(subtitle)
                    .setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL)
                    .build()

                biometricPrompt.authenticate(promptInfo)
            } catch (e: Exception) {
                result = "Error: Biometric UI failed - ${e.message}"
                latch.countDown()
            }
        }
        
        latch.await(30, TimeUnit.SECONDS)
        return result
    }

    private fun setupHardwareCallbacks() {
        val vm = ViewModelProvider(this)[ChatViewModel::class.java]
        nativeEngine.getSecureApiKeyProvider = { provider -> sharedPreferences.getString(provider, "")?.trim() ?: "" }
        nativeEngine.onRequestHardwareDataCallback = { nodeId -> if (nodeId == 5) { try { val location = Tasks.await(fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, CancellationTokenSource().token)); location?.let { "${it.latitude}, ${it.longitude}" } ?: "Error: GPS Timeout" } catch (e: Exception) { "Error: ${e.message}" } } else "Data $nodeId" }
        nativeEngine.executeHardwareActionCallback = { nodeId, state -> when (nodeId) { 4 -> { try { val cm = getSystemService(Context.CAMERA_SERVICE) as CameraManager; cm.setTorchMode(cm.cameraIdList[0], state); true } catch (e: Exception) { false } } 6 -> { try { startActivity(Intent(if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) Settings.Panel.ACTION_INTERNET_CONNECTIVITY else Settings.ACTION_WIFI_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); true } catch (e: Exception) { false } } 7 -> { try { startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); true } catch (e: Exception) { false } } else -> false } }
        nativeEngine.onSystemTiersUpdateCallback = { temp, used, total -> 
            // vm.systemTemperature = temp; // Handled by BatteryManager
            vm.ramUsedGB = used; vm.ramTotalGB = total 
        }
        nativeEngine.onKernelMessageCallback = { msg -> 
            runOnUiThread {
                vm.reasoningLogsText += "\n> $msg"
                
                // v12.30: If it's a bracketed tool result (e.g. [SENSOR ANALYSIS]), push to chat bubbles too
                if (msg.startsWith("[") && !msg.startsWith("[AGENT]")) {
                    vm.messages.add(ChatMessage(System.currentTimeMillis(), "Ronin", msg))
                }
            }
        }
        
        // v7.0: Agent Mode Callbacks
        nativeEngine.requestHITLConfirmationCallback = { intentName, message, callback ->
            vm.hitlIntentName = intentName
            vm.hitlMessage = message
            vm.onHITLResult = callback
            vm.showHITLDialog = true
        }

        // Phase 5: Developer HUD Callback
        nativeEngine.onDevHUDUpdateCallback = { state, intent, conf, plan ->
            runOnUiThread {
                vm.hudState = state
                vm.hudIntent = intent
                vm.hudConfidence = conf
                vm.hudPlan = plan
            }
        }
        
        nativeEngine.executeAgentToolCallback = { toolName, params ->
            val actionName = (params["action"] ?: toolName).uppercase()
            val intentContext = (params["intent"] ?: "").uppercase()
            Log.i("RoninKernel_MainActivity", "Executing Tool: $toolName (Action: $actionName, Intent: $intentContext)")
            
            when {
                // 1. MEMORY & DATA (Facts, Notes, Vault)
                actionName.contains("NOTE") || actionName.contains("FACT") || actionName.contains("VAULT") || 
                actionName.contains("MEMORY") || actionName.contains("DATABASE") || actionName.contains("RECORD") ||
                actionName.contains("STORE") || actionName.contains("SAVE") || actionName.contains("LOOKUP") || 
                actionName.contains("QUERY") || actionName.contains("ACCESS") || toolName == "MEMORY" -> {
                    try {
                        when {
                            actionName.contains("SAVE_NOTE") || actionName.contains("ADD_NOTE") -> {
                                val title = params["note_title"] ?: params["title"] ?: "Untitled Note"
                                val content = params["note_content"] ?: params["content"] ?: params["text"] ?: ""
                                if (content.isEmpty()) "Error: Note content is empty."
                                else if (nativeEngine.storeNote(title, content, "")) {
                                    nativeEngine.pushKernelMessage("[AGENT] Note saved: $title")
                                    "Note saved: $title" 
                                } else "Error: Database failure."
                            }
                            actionName.contains("SAVE_FACT") || actionName.contains("ADD_FACT") -> {
                                val entity = params["entity"] ?: params["item"] ?: params.keys.find { it.contains("name", true) || it.contains("car", true) }?.let { params[it] } ?: "Unknown"
                                val rawAttr = params["attribute"] ?: params["property"] ?: "General"
                                val attr = normalizeAttribute(rawAttr)
                                val value = params["value"] ?: params["content"] ?: params.values.firstOrNull { it != entity && it != rawAttr && it.length > 2 } ?: ""
                                if (value.isEmpty()) "Error: Fact value is empty."
                                else if (nativeEngine.storeFact(entity, attr, value)) {
                                    nativeEngine.pushKernelMessage("[AGENT] Fact saved: $entity.$attr = $value")
                                    "Fact saved: $entity.$attr = $value"
                                } else "Error: Database failure."
                            }
                            actionName.contains("VAULT") && (actionName.contains("SAVE") || actionName.contains("ADD")) -> {
                                val title = params["vault_title"] ?: params["title"] ?: "Secret"
                                val content = params["vault_content"] ?: params["content"] ?: ""
                                authenticateAndExecute("Store Secret", "Authenticate to encrypt and store in Vault") {
                                    val encrypted = nativeEngine.encryptSecret(content)
                                    if (nativeEngine.storeVault(title, encrypted)) {
                                        nativeEngine.pushKernelMessage("[AGENT] Vault entry saved: $title")
                                        "Vault entry saved: $title" 
                                    } else "Error: Database failure."
                                }
                            }
                            actionName.contains("VAULT") && (actionName.contains("QUERY") || actionName.contains("LOOKUP") || actionName.contains("CHECK") || actionName.contains("READ") || actionName.contains("ACCESS")) -> {
                                val rawTitle = params["vault_title"] ?: params["title"] ?: params["query"] ?: ""
                                authenticateAndExecute("Access Vault", "Authenticate to retrieve secret: $rawTitle") {
                                    var encrypted = nativeEngine.lookupVault(rawTitle)
                                    if (encrypted.isEmpty()) {
                                        listOf("card", "visa", "bank", "password", "key", "secret").forEach { kw ->
                                            if (rawTitle.lowercase().contains(kw)) {
                                                val res = nativeEngine.lookupVault(kw)
                                                if (res.isNotEmpty()) { encrypted = res; return@forEach }
                                            }
                                        }
                                    }
                                    if (encrypted.isNotEmpty()) {
                                        val decrypted = nativeEngine.decryptSecret(encrypted)
                                        nativeEngine.pushKernelMessage("\n[VAULT] Result: $decrypted")
                                        decrypted
                                    } else "Error: No vault entry found."
                                }
                            }
                            actionName.contains("SEARCH_NOTES") || actionName.contains("QUERY_NOTES") -> {
                                val results = nativeEngine.searchNotes(params["query"] ?: "")
                                if (results.isNotEmpty()) {
                                    nativeEngine.pushKernelMessage("\n[NOTES FOUND]\n" + results.joinToString("\n---\n"))
                                    "Found ${results.size} notes."
                                } else "Error: No notes found."
                            }
                            else -> {
                                val entity = params["entity"] ?: params["item"] ?: "Unknown"
                                val attr = normalizeAttribute(params["attribute"] ?: "General")
                                val res = nativeEngine.lookupFact(entity, attr)
                                if (res.isNotEmpty()) {
                                    nativeEngine.pushKernelMessage("\n[FACT FOUND] $entity's $attr is $res")
                                    res
                                } else "Error: No information found for $entity."
                            }
                        }
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "CONTACTS", "RESOLVE_CONTACT" -> {
                    val name = params["recipient_name"] ?: params["recipient"] ?: params["contact_name"] ?: "Unknown"
                    val resolved = resolveContactName(name)
                    if (resolved == "PERMISSION_DENIED") "Error: Permission Denied"
                    else if (resolved.startsWith("NOT_FOUND")) "Error: Contact not found: $name"
                    else resolved
                }
                
                // 3. LOCATION & MAPS
                actionName.contains("LOCATION") || actionName.contains("MAP") || actionName.contains("GPS") ||
                intentContext.contains("MAP") || intentContext.contains("LOCATION") || toolName == "LOCATION" -> {
                    try {
                        val isMapReq = actionName.contains("MAP") || actionName.contains("SHOW") || actionName.contains("OPEN") || 
                                      intentContext.contains("MAP") || intentContext.contains("SHOW")
                        if (isMapReq) {
                            val locJson = params["context_result_LOCATION"] ?: params["payload"]
                            var lat = "0.0"; var lon = "0.0"
                            if (locJson != null && locJson.startsWith("{")) {
                                val j = JSONObject(locJson); lat = j.opt("lat")?.toString() ?: "0.0"; lon = j.opt("lon")?.toString() ?: "0.0"
                            } else {
                                val task = fusedLocationClient.lastLocation
                                val res = try { Tasks.await(task, 2, TimeUnit.SECONDS) } catch (e: Exception) { null }
                                res?.let { lat = it.latitude.toString(); lon = it.longitude.toString() }
                            }
                            val uri = Uri.parse("geo:$lat,$lon?q=$lat,$lon")
                            val intent = Intent(Intent.ACTION_VIEW, uri).apply { setPackage("com.google.android.apps.maps"); addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
                            runOnUiThread { try { startActivity(intent) } catch (e: Exception) { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://www.google.com/maps/search/?api=1&query=$lat,$lon")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } }
                            val msg = "Opened Map at $lat, $lon"
                            nativeEngine.pushKernelMessage("[AGENT] $msg")
                            msg
                        } else {
                            val task = fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, CancellationTokenSource().token)
                            val res = try { Tasks.await(task, 5, TimeUnit.SECONDS) } catch (e: Exception) { Tasks.await(fusedLocationClient.lastLocation, 2, TimeUnit.SECONDS) }
                            res?.let { JSONObject().put("lat", it.latitude).put("lon", it.longitude).toString() } ?: "Error: GPS Unavailable"
                        }
                    } catch (e: Exception) { "Error: ${e.message}" }
                }

                // 4. ALARM
                actionName.contains("ALARM") || actionName.contains("WAKE") || actionName.contains("နှိုး") || toolName == "ALARM" -> {
                    try {
                        val timeStr = params["value"] ?: params["time"] ?: "06:00"
                        val message = params["message"] ?: params["label"] ?: "Ronin Alarm"
                        val parts = timeStr.split(":")
                        val hour = parts.getOrNull(0)?.trim()?.toIntOrNull() ?: 6
                        val minute = parts.getOrNull(1)?.trim()?.toIntOrNull() ?: 0
                        val intent = Intent(android.provider.AlarmClock.ACTION_SET_ALARM).apply { putExtra(android.provider.AlarmClock.EXTRA_MESSAGE, message); putExtra(android.provider.AlarmClock.EXTRA_HOUR, hour); putExtra(android.provider.AlarmClock.EXTRA_MINUTES, minute); addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
                        runOnUiThread { startActivity(intent) }
                        val msg = "Set alarm for $hour:$minute"
                        nativeEngine.pushKernelMessage("[AGENT] $msg")
                        msg
                    } catch (e: Exception) { "Error: ${e.message}" }
                }

                // 5. CALENDAR
                actionName.contains("CALENDAR") || actionName.contains("EVENT") || actionName.contains("MEETING") || toolName == "CALENDAR" -> {
                    try {
                        val isRead = actionName.contains("READ") || actionName.contains("QUERY") || actionName.contains("CHECK") || actionName.contains("LIST") || actionName.contains("GET")
                        if (isRead) {
                            var keyword = params["keyword"] ?: params["query"] ?: ""
                            val cal = java.util.Calendar.getInstance()
                            if (params["time"]?.contains("tomorrow") == true || params["original_query"]?.contains("မက်ဖြန်") == true) cal.add(java.util.Calendar.DAY_OF_YEAR, 1)
                            cal.set(java.util.Calendar.HOUR_OF_DAY, 0); cal.set(java.util.Calendar.MINUTE, 0)
                            val start = cal.timeInMillis; cal.set(java.util.Calendar.HOUR_OF_DAY, 23); cal.set(java.util.Calendar.MINUTE, 59)
                            val end = cal.timeInMillis
                            val cursor = contentResolver.query(android.provider.CalendarContract.Events.CONTENT_URI, arrayOf("title", "dtstart"), "dtstart >= ? AND dtstart <= ?", arrayOf(start.toString(), end.toString()), "dtstart ASC")
                            val results = mutableListOf<String>()
                            cursor?.use { while (it.moveToNext()) { val t = it.getString(0); val time = java.text.SimpleDateFormat("HH:mm").format(java.util.Date(it.getLong(1))); if (keyword.isEmpty() || t.contains(keyword, true)) results.add("[$time] $t") } }
                            val output = if (results.isEmpty()) "No events found." else results.joinToString("\n")
                            nativeEngine.pushKernelMessage("\n[CALENDAR]\n$output"); output
                        } else {
                            val title = params["title"] ?: params["event"] ?: "Ronin Event"
                            var desc = params["description"] ?: params["details"] ?: ""
                            val cal = java.util.Calendar.getInstance()
                            var beginTime = cal.timeInMillis
                            if (params["time"]?.contains("tomorrow") == true || params["original_query"]?.contains("မနက်ဖြန်") == true) cal.add(java.util.Calendar.DAY_OF_YEAR, 1)
                            val intent = Intent(Intent.ACTION_INSERT).apply { data = android.provider.CalendarContract.Events.CONTENT_URI; putExtra(android.provider.CalendarContract.Events.TITLE, title); putExtra(android.provider.CalendarContract.Events.DESCRIPTION, desc); putExtra(android.provider.CalendarContract.EXTRA_EVENT_BEGIN_TIME, beginTime); putExtra(android.provider.CalendarContract.EXTRA_EVENT_END_TIME, beginTime + 60 * 60 * 1000); addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
                            runOnUiThread { startActivity(intent) }
                            val msg = "Scheduled $title"
                            nativeEngine.pushKernelMessage("[AGENT] $msg")
                            msg
                        }
                    } catch (e: Exception) { "Error: ${e.message}" }
                }

                // 6. SENSOR & ANALYSIS
                actionName.contains("SENSOR") || actionName.contains("VIBRATION") || actionName.contains("RESONANCE") || 
                actionName.contains("ANALYSIS") || actionName.contains("STATUS") || actionName.contains("CONCEPT") || toolName == "SENSOR" -> {
                    try {
                        val analysis = nativeEngine.getSensorAnalysis("RESONANCE")
                        val output = if (analysis.startsWith("{")) JSONObject(analysis).optString("summary", analysis) else analysis
                        nativeEngine.pushKernelMessage("\n[SENSOR ANALYSIS]\n$output")
                        output
                    } catch (e: Exception) { "Error: ${e.message}" }
                }

                // 7. SMS
                actionName.contains("SMS") || actionName.contains("MESSAGE") || toolName == "SMS" -> {
                    try {
                        val locJson = params["context_result_LOCATION"]; val conJson = params["context_result_CONTACTS"]
                        var recipient = params["recipient_name"] ?: params["recipient_number"] ?: params["recipient"] ?: ""
                        var body = params["message"] ?: params["sms_body"] ?: ""
                        if (conJson != null && conJson.startsWith("{")) recipient = JSONObject(conJson).optString("phone_number", recipient)
                        else if (conJson != null && !conJson.startsWith("Error")) recipient = conJson
                        if (locJson != null && locJson.startsWith("{")) { val j = JSONObject(locJson); val lat = j.opt("lat")?.toString() ?: "0.0"; val lon = j.opt("lon")?.toString() ?: "0.0"; body += "\n\n📍 My Location: https://maps.google.com/?q=$lat,$lon" }
                        if (!recipient.matches(Regex("^[+]?[0-9\\- ]{5,}+$"))) { val resolved = resolveContactName(recipient); if (!resolved.startsWith("NOT_FOUND")) recipient = resolved }
                        runOnUiThread { val intent = Intent(Intent.ACTION_SENDTO).apply { data = Uri.parse("smsto:${Uri.encode(recipient)}"); putExtra("sms_body", body); addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }; startActivity(intent) }
                        "Opened SMS composer for $recipient"
                    } catch (e: Exception) { "Error: ${e.message}" }
                }

                else -> "Tool $toolName executed with params: $params"
            }
        }
    }

    override fun onResume() { 
        super.onResume()
        scanLocalModels()
        startWorldStateSync()
    }

    private val android.os.BatteryManager.isCharging: Boolean
        get() {
            return if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_STATUS) == android.os.BatteryManager.BATTERY_STATUS_CHARGING ||
                getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_STATUS) == android.os.BatteryManager.BATTERY_STATUS_FULL
            } else {
                val intent = registerReceiver(null, android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED))
                val status = intent?.getIntExtra(android.os.BatteryManager.EXTRA_STATUS, -1) ?: -1
                status == android.os.BatteryManager.BATTERY_STATUS_CHARGING || status == android.os.BatteryManager.BATTERY_STATUS_FULL
            }
        }

    private fun startWorldStateSync() {
        lifecycleScope.launch(Dispatchers.IO) {
            while (true) {
                try {
                    val batteryManager = getSystemService(Context.BATTERY_SERVICE) as android.os.BatteryManager
                    val battery = batteryManager.getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY).toFloat()
                    
                    val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
                    val memoryInfo = android.app.ActivityManager.MemoryInfo()
                    activityManager.getMemoryInfo(memoryInfo)
                    val ram = memoryInfo.availMem / (1024f * 1024f) // MB
                    
                    val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
                    val net = cm.activeNetworkInfo?.isConnected ?: false
                    
                    val gps = try {
                        val lm = getSystemService(Context.LOCATION_SERVICE) as android.location.LocationManager
                        lm.isProviderEnabled(android.location.LocationManager.GPS_PROVIDER)
                    } catch (e: Exception) { false }

                    val charging = batteryManager.isCharging
                    
                    nativeEngine.injectWorldState(battery, ram, gps, net, charging)
                } catch (e: Exception) { 
                    Log.w("RoninKernel", "WorldState Sync failed: ${e.message}")
                }
                delay(10000) // 10s cycle
            }
        }
    }
}

@Composable
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current; val activity = context.findActivity() as? MainActivity
    val scope = rememberCoroutineScope(); val scaffoldState = rememberScaffoldState()
    var currentInput by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager; val mi = ActivityManager.MemoryInfo()
        val batteryManager = context.getSystemService(Context.BATTERY_SERVICE) as BatteryManager
        withContext(Dispatchers.IO) { 
            while (true) { 
                try { 
                    am.getMemoryInfo(mi)
                    val used = (mi.totalMem - mi.availMem) / 1073741824f
                    val total = mi.totalMem / 1073741824f
                    val batteryIntent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
                    val temp = batteryIntent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.let { it / 10f } ?: 35f
                    val level = batteryIntent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
                    val scale = batteryIntent?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
                    val batteryPct = if (level >= 0 && scale > 0) (level * 100) / scale else 50
                    val isCharging = batteryIntent?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) == BatteryManager.BATTERY_STATUS_CHARGING

                    withContext(Dispatchers.Main) { 
                        chatViewModel.ramUsedGB = used; chatViewModel.ramTotalGB = total
                        chatViewModel.systemTemperature = temp
                        chatViewModel.lmkPressure = engine.getLMKPressureSafe() 
                        // v12.9: Update Sensor Driver Guardrails
                        activity?.updateSensorGuardrails(temp, batteryPct, isCharging)
                    }
                    engine.updateSystemHealthSafe(temp, used, total) 
                } catch (e: Exception) {} ; delay(5000) 
            } 
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        drawerContent = { ModalDrawerSheet(chatViewModel, brainPicker, onSaveOfflineMode) },
        topBar = { TopAppBar(navigationIcon = { IconButton(onClick = { scope.launch { scaffoldState.drawerState.open() } }) { Icon(Icons.Default.Menu, null) } }, title = { Column { Text("Ronin Kernel", fontWeight = FontWeight.Bold); Text(chatViewModel.kernelStatus, fontSize = 10.sp, color = if (chatViewModel.isGemmaReady) Color.Green else Color.Yellow) } }, actions = { IconButton(onClick = { chatViewModel.showReasoning = !chatViewModel.showReasoning }) { Icon(if (chatViewModel.showReasoning) Icons.Default.Visibility else Icons.Default.VisibilityOff, null) }; IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) { Icon(Icons.Default.Info, null) } }) }
    ) { padding ->
        Box(modifier = Modifier.padding(padding).fillMaxSize().background(Color(0xFF0F111A))) {
            if (chatViewModel.wizardState != WizardState.ACTIVE) { BootstrapWizard(chatViewModel, brainPicker) }
            else {
                Column(modifier = Modifier.fillMaxSize()) {
                    if (chatViewModel.showSysInfo) SystemInfoPanel(chatViewModel)
                    AnimatedVisibility(visible = chatViewModel.showReasoning) { 
                        Box(modifier = Modifier.height(180.dp).fillMaxWidth().background(Color(0xFF12141C)).padding(8.dp).border(1.dp, Color.Cyan.copy(alpha = 0.2f), RoundedCornerShape(8.dp))) { 
                            Column {
                                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp)) {
                                    Text("Reasoning Console", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                                    IconButton(onClick = { 
                                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                                        val clip = android.content.ClipData.newPlainText("Ronin Console", chatViewModel.reasoningLogsText)
                                        clipboard.setPrimaryClip(clip)
                                        Toast.makeText(context, "Console logs copied to clipboard", Toast.LENGTH_SHORT).show()
                                    }, modifier = Modifier.size(24.dp)) {
                                        Icon(Icons.Default.ContentCopy, "Copy", tint = Color.Cyan, modifier = Modifier.size(14.dp))
                                    }
                                }
                                val scrollState = rememberScrollState()
                                LaunchedEffect(chatViewModel.reasoningLogsText) { scrollState.animateScrollTo(scrollState.maxValue) }
                                androidx.compose.foundation.text.selection.SelectionContainer {
                                    Text(chatViewModel.reasoningLogsText, color = Color.Cyan, fontSize = 9.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.verticalScroll(scrollState).weight(1f))
                                }
                            }
                        } 
                    }
                    Box(modifier = Modifier.weight(1f).fillMaxWidth()) { 
                        LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) { 
                            items(chatViewModel.messages.asReversed()) { msg -> 
                                ChatBubble(msg, onContinue = {
                                    if (!chatViewModel.isGenerating && !msg.isContinuing) {
                                        msg.isContinuing = true
                                        chatViewModel.isGenerating = true
                                        scope.launch {
                                            val continuePrompt = "ဆက်ရေးပေးပါ။"
                                            val result = engine.processInputAsync(continuePrompt, chatViewModel.systemPrompt)
                                            msg.content += result.result
                                            msg.isTruncated = result.result.isNotEmpty() && !result.result.trim().let { it.endsWith("။") || it.endsWith(".") || it.endsWith("?") || it.endsWith("!") }
                                            msg.isContinuing = false
                                            chatViewModel.isGenerating = false
                                        }
                                    }
                                }, onFeedback = { helpful ->
                                    engine.applyHumanFeedback(msg.sessionId, helpful)
                                }) 
                            }
 
                        } 
                        if (chatViewModel.showCommandSuggestions) {
                            val suggestions = listOf("/status", "/skills", "/model", "/reset").filter { it.startsWith(currentInput.lowercase()) }
                            if (suggestions.isNotEmpty()) { Surface(color = Color(0xFF25283D), modifier = Modifier.align(Alignment.BottomStart).padding(16.dp).fillMaxWidth(0.7f).clip(RoundedCornerShape(12.dp)), elevation = 8.dp) { LazyColumn(modifier = Modifier.heightIn(max = 200.dp)) { items(suggestions) { s -> TextButton(onClick = { currentInput = "$s "; chatViewModel.showCommandSuggestions = false }, modifier = Modifier.fillMaxWidth()) { Text(s, color = Color.White) } } } } }
                        }
                        
                        // Phase 5: Developer HUD Overlay
                        if (chatViewModel.showDevHUD) {
                            Box(modifier = Modifier.align(Alignment.TopEnd).padding(16.dp)) {
                                Surface(
                                    color = Color(0xCC000000L), 
                                    shape = RoundedCornerShape(8.dp),
                                    border = androidx.compose.foundation.BorderStroke(1.dp, Color(0xFF64B5F6))
                                ) {
                                    Column(modifier = Modifier.padding(12.dp)) {
                                        Text("COG HUD (1Hz)", color = Color.Yellow, fontSize = 10.sp, fontWeight = FontWeight.Bold)
                                        Spacer(modifier = Modifier.height(4.dp))
                                        Text("STATE: ${chatViewModel.hudState}", color = Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                        Text("INTENT: ${chatViewModel.hudIntent}", color = Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                        Text("CONF: ${"%.2f".format(chatViewModel.hudConfidence)}", color = if(chatViewModel.hudConfidence > 0.5f) Color.Green else Color.Red, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                        if (chatViewModel.hudPlan.isNotEmpty()) {
                                            Text("PLAN: ${chatViewModel.hudPlan}", color = Color.Cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                        }
                                        
                                        // Phase 5: Sensor Analytics Visualization
                                        if (chatViewModel.sensorFreqHz > 0) {
                                            Divider(color = Color.DarkGray, modifier = Modifier.padding(vertical = 4.dp))
                                            Text("DSP SENSOR DATA", color = Color(0xFFFFA726), fontSize = 9.sp, fontWeight = FontWeight.Bold)
                                            val anomalyColor = if(chatViewModel.sensorAnomaly) Color.Red else Color.Green
                                            Text("PEAK: ${"%.1f".format(chatViewModel.sensorFreqHz)}Hz", color = anomalyColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                            Text("PSD: ${"%.1f".format(chatViewModel.sensorPsdDb)}dB", color = anomalyColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Surface(elevation = 8.dp, color = Color(0xFF1A1C2C)) {
                        Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                            TextField(value = currentInput, onValueChange = { currentInput = it; chatViewModel.showCommandSuggestions = it.startsWith("/") }, modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)), colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White), trailingIcon = { IconButton(onClick = { 
                                if (chatViewModel.isGenerating) {
                                    chatViewModel.isGenerating = false
                                    engine.stopInference()
                                } else if (currentInput.isNotBlank()) { 
                                    val raw = currentInput; chatViewModel.messages.add(ChatMessage(System.currentTimeMillis(), "User", raw)); currentInput = ""; chatViewModel.isGenerating = true
                                    chatViewModel.reasoningLogsText = "> Processing: $raw"
                                    scope.launch { 
                                        val isCommand = raw.trim().startsWith("/")
                                        val roninMsg = ChatMessage(System.currentTimeMillis() + 1, "Ronin", "")
                                        chatViewModel.messages.add(roninMsg)
                                        
                                        try {
                                            if (!isCommand && (chatViewModel.cloudOnlyMode || !chatViewModel.isGemmaReady)) { 
                                                val apiKey = engine.getSecureApiKeyProvider?.invoke(chatViewModel.primaryCloudProvider) ?: ""
                                                val res = engine.performCloudInferenceAsync(raw, chatViewModel.primaryCloudProvider, apiKey)
                                                roninMsg.content = res
                                            } else { 
                                                val result = engine.processInputAsync(raw, chatViewModel.systemPrompt)
                                                if (isCommand || result.result.startsWith("Executing plan:")) {
                                                    roninMsg.content = result.result
                                                } else if (roninMsg.content.isEmpty()) {
                                                    roninMsg.content = result.result
                                                }
                                                roninMsg.sessionId = result.sessionId
                                            }
                                        } finally {
                                            chatViewModel.isGenerating = false
                                        }
                                    } 
                                } 
                            }) { Icon(if (chatViewModel.isGenerating) Icons.Default.Stop else Icons.Default.Send, null, tint = if (chatViewModel.isGenerating) Color.Red else Color(0xFF64B5F6)) } })
                        }
                    }
                }
            }
        }
    }
    if (chatViewModel.showAddCloudDialog) {
        CloudProviderDialog(chatViewModel, activity)
    }
    
    // v7.0: HITL Dialog
    if (chatViewModel.showHITLDialog) {
        AlertDialog(
            onDismissRequest = { 
                chatViewModel.showHITLDialog = false
                chatViewModel.onHITLResult?.invoke(false)
            },
            backgroundColor = Color(0xFF1E2130),
            title = { 
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Warning, null, tint = Color.Yellow)
                    Spacer(Modifier.width(8.dp))
                    Text("Safety Confirmation", color = Color.White) 
                }
            },
            text = { Text(chatViewModel.hitlMessage, color = Color.White) },
            confirmButton = {
                Button(
                    onClick = {
                        Log.i("RoninKernel_MainActivity", "HITL: User Approved action.")
                        chatViewModel.showHITLDialog = false
                        chatViewModel.onHITLResult?.invoke(true)
                    },
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color.Green)
                ) { Text("Approve", color = Color.Black, fontWeight = FontWeight.Bold) }
            },
            dismissButton = {
                TextButton(onClick = { 
                    Log.i("RoninKernel_MainActivity", "HITL: User Rejected action.")
                    chatViewModel.showHITLDialog = false
                    chatViewModel.onHITLResult?.invoke(false)
                }) { Text("Reject", color = Color.Red) }
            }
        )
    }
}

@Composable
fun ModalDrawerSheet(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current; val activity = context.findActivity() as? MainActivity
    Column(modifier = Modifier.fillMaxSize().background(Color(0xFF1A1C2C)).verticalScroll(rememberScrollState()).padding(16.dp)) {
        Text("Ronin Configuration", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White); Spacer(Modifier.height(24.dp))
        
        Text("Operation Mode", fontSize = 12.sp, color = Color.Gray)
        Row(verticalAlignment = Alignment.CenterVertically) { Text("Cloud Only Mode", modifier = Modifier.weight(1f), color = Color.White, fontSize = 14.sp); Switch(checked = chatViewModel.cloudOnlyMode, onCheckedChange = { chatViewModel.cloudOnlyMode = it; activity?.saveCloudOnlyMode(it) }) }
        Row(verticalAlignment = Alignment.CenterVertically) { Text("Reasoning Logs", modifier = Modifier.weight(1f), color = Color.White, fontSize = 14.sp); Switch(checked = chatViewModel.showReasoning, onCheckedChange = { chatViewModel.showReasoning = it }) }
        Row(verticalAlignment = Alignment.CenterVertically) { Text("Developer HUD (1Hz)", modifier = Modifier.weight(1f), color = Color.White, fontSize = 14.sp); Switch(checked = chatViewModel.showDevHUD, onCheckedChange = { chatViewModel.showDevHUD = it }) }
        
        Divider(Modifier.padding(vertical = 16.dp))
        Text("Sampling Parameters (T,P,K)", fontSize = 12.sp, color = Color.Gray)
        Text("Temperature: ${"%.2f".format(chatViewModel.samplingTemperature)}", color = Color.White, fontSize = 13.sp)
        Slider(value = chatViewModel.samplingTemperature, onValueChange = { chatViewModel.samplingTemperature = it; activity?.saveSamplingParams(it, chatViewModel.topK, chatViewModel.topP) }, valueRange = 0.1f..1.5f, colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6)))
        
        Text("Top-K: ${chatViewModel.topK}", color = Color.White, fontSize = 13.sp)
        Slider(value = chatViewModel.topK.toFloat(), onValueChange = { chatViewModel.topK = it.toInt(); activity?.saveSamplingParams(chatViewModel.samplingTemperature, it.toInt(), chatViewModel.topP) }, valueRange = 1f..100f, colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6)))
        
        Text("Top-P: ${"%.2f".format(chatViewModel.topP)}", color = Color.White, fontSize = 13.sp)
        Slider(value = chatViewModel.topP, onValueChange = { chatViewModel.topP = it; activity?.saveSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, it) }, valueRange = 0.1f..1.0f, colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6)))
        
        Text("Max Tokens: ${chatViewModel.maxTokens}", color = Color.White, fontSize = 13.sp)
        Slider(value = chatViewModel.maxTokens.toFloat(), onValueChange = { chatViewModel.maxTokens = it.toInt(); activity?.saveMaxTokens(it.toInt()) }, valueRange = 128f..2048f, colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6)))

        Divider(Modifier.padding(vertical = 16.dp))
        Text("System Prompt", fontSize = 12.sp, color = Color.Gray)
        TextField(value = chatViewModel.systemPrompt, onValueChange = { chatViewModel.systemPrompt = it; activity?.saveSystemPrompt(it) }, modifier = Modifier.fillMaxWidth().height(100.dp), colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White), textStyle = androidx.compose.ui.text.TextStyle(fontSize = 11.sp))
        
        Divider(Modifier.padding(vertical = 16.dp))
        Text("Local Models", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color.White)
        chatViewModel.discoveredModels.forEach { path -> val isActive = path == chatViewModel.localModelPath; Row(verticalAlignment = Alignment.CenterVertically) { RadioButton(selected = isActive, onClick = { activity?.hydrateModel(path) }, colors = RadioButtonDefaults.colors(selectedColor = Color.Green)); Text(File(path).name, modifier = Modifier.weight(1f).clickable { activity?.hydrateModel(path) }, color = if (isActive) Color.Green else Color.White, fontSize = 12.sp); IconButton(onClick = { activity?.deleteLocalModel(path) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray, modifier = Modifier.size(16.dp)) } } }
        OutlinedButton(onClick = { brainPicker.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) { Text("Import Brain") }
        
        Divider(Modifier.padding(vertical = 16.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Cloud Profiles", modifier = Modifier.weight(1f), fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color.White)
            IconButton(onClick = { chatViewModel.editingProvider = null; chatViewModel.showAddCloudDialog = true }) { Icon(Icons.Default.Add, null, tint = Color.Cyan) }
        }
        chatViewModel.cloudProviders.forEach { profile -> 
            val isSelected = profile.name == chatViewModel.primaryCloudProvider
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) { 
                    RadioButton(selected = isSelected, onClick = { activity?.savePrimaryCloudProvider(profile.name); chatViewModel.primaryCloudProvider = profile.name }); 
                    Column(modifier = Modifier.weight(1f).clickable { activity?.savePrimaryCloudProvider(profile.name); chatViewModel.primaryCloudProvider = profile.name }) { 
                        Text(profile.name, color = Color.White, fontSize = 12.sp); 
                        Text(profile.modelId, fontSize = 9.sp, color = Color.Gray) 
                    } 
                    IconButton(onClick = { chatViewModel.editingProvider = profile; chatViewModel.showAddCloudDialog = true }) { Icon(Icons.Default.Edit, null, tint = Color.Gray, modifier = Modifier.size(16.dp)) }
                    IconButton(onClick = { activity?.deleteCloudProvider(profile.name) }) { Icon(Icons.Default.Delete, null, tint = Color.Red, modifier = Modifier.size(16.dp)) }
                }
                if (isSelected) {
                    var apiKey by remember(profile.name) { mutableStateOf(activity?.getApiKey(profile.name) ?: "") }
                    TextField(
                        value = apiKey,
                        onValueChange = { apiKey = it; activity?.saveApiKey(profile.name, it) },
                        placeholder = { Text("Enter API Key", fontSize = 10.sp, color = Color.Gray) },
                        modifier = Modifier.fillMaxWidth().padding(start = 32.dp, end = 8.dp, bottom = 8.dp),
                        colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White),
                        textStyle = androidx.compose.ui.text.TextStyle(fontSize = 11.sp),
                        singleLine = true
                    )
                }
            }
        }
        
        Spacer(Modifier.weight(1f)); TextButton(onClick = { activity?.clearModelCache() }) { Text("Clear All Cache", color = Color.Red, fontSize = 12.sp) }
    }
}

@Composable
fun CloudProviderDialog(chatViewModel: ChatViewModel, activity: MainActivity?) {
    var name by remember { mutableStateOf(chatViewModel.editingProvider?.name ?: "") }
    var endpoint by remember { mutableStateOf(chatViewModel.editingProvider?.endpoint ?: "") }
    var modelId by remember { mutableStateOf(chatViewModel.editingProvider?.modelId ?: "gemini-1.5-flash-latest") }
    var providerType by remember { mutableStateOf(chatViewModel.editingProvider?.providerType ?: "Gemini") }
    
    // Auto-advance if editing
    LaunchedEffect(chatViewModel.editingProvider) {
        if (chatViewModel.editingProvider != null) {
            chatViewModel.isSelectingType = false
        }
    }

    AlertDialog(
        onDismissRequest = { 
            chatViewModel.showAddCloudDialog = false
            chatViewModel.isSelectingType = true
            chatViewModel.fetchedModels.clear()
        },
        backgroundColor = Color(0xFF1E2130),
        title = { Text(if (chatViewModel.isSelectingType) "Select Cloud Provider" else "Setup ${providerType}", color = Color.White) },
        text = {
            if (chatViewModel.isSelectingType) {
                Column {
                    val types = listOf("Gemini", "OpenAI", "OpenRouter", "Custom")
                    types.forEach { type ->
                        Surface(
                            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp).clickable { 
                                providerType = type
                                name = if (type == "Custom") "" else type
                                endpoint = when(type) {
                                    "Gemini" -> "https://generativelanguage.googleapis.com/v1beta"
                                    "OpenRouter" -> "https://openrouter.ai/api/v1"
                                    "OpenAI" -> "https://api.openai.com/v1"
                                    else -> ""
                                }
                                chatViewModel.isSelectingType = false
                            },
                            color = Color(0xFF25283D),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                                Icon(
                                    when(type) {
                                        "Gemini" -> Icons.Default.Cloud
                                        "OpenAI" -> Icons.Default.Memory
                                        "OpenRouter" -> Icons.Default.Public
                                        else -> Icons.Default.Settings
                                    },
                                    null, tint = Color.Cyan, modifier = Modifier.size(24.dp)
                                )
                                Spacer(Modifier.width(16.dp))
                                Text(type, color = Color.White, fontWeight = FontWeight.Bold)
                            }
                        }
                    }
                }
            } else {
                Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                    val isCustom = providerType == "Custom"
                    
                    if (isCustom) {
                        TextField(value = name, onValueChange = { name = it }, label = { Text("Provider Name") }, modifier = Modifier.fillMaxWidth())
                        Spacer(Modifier.height(8.dp))
                        TextField(value = endpoint, onValueChange = { endpoint = it }, label = { Text("Endpoint URL") }, modifier = Modifier.fillMaxWidth())
                        Spacer(Modifier.height(8.dp))
                    } else {
                        Text("Provider: $providerType", color = Color.Cyan, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(12.dp))
                    }
                    
                    var apiKey by remember { mutableStateOf(activity?.getApiKey(name) ?: "") }
                    TextField(
                        value = apiKey,
                        onValueChange = { apiKey = it; activity?.saveApiKey(name, it) },
                        label = { Text("API Key") },
                        modifier = Modifier.fillMaxWidth(),
                        colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White)
                    )
                    Spacer(Modifier.height(12.dp))

                    Row(verticalAlignment = Alignment.CenterVertically) {
                        TextField(value = modelId, onValueChange = { modelId = it }, label = { Text("Model ID") }, modifier = Modifier.weight(1f))
                        IconButton(onClick = { if (name.isNotEmpty()) activity?.fetchModels(name) }) { 
                            if (chatViewModel.isFetchingModels) CircularProgressIndicator(modifier = Modifier.size(24.dp))
                            else Icon(Icons.Default.Refresh, null, tint = Color.Cyan) 
                        }
                    }
                    
                    if (chatViewModel.fetchedModels.isNotEmpty()) {
                        Text("Available Models:", color = Color.Gray, fontSize = 12.sp, modifier = Modifier.padding(vertical = 8.dp))
                        chatViewModel.fetchedModels.forEach { m ->
                            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().clickable { modelId = m }.padding(vertical = 4.dp)) {
                                RadioButton(selected = modelId == m, onClick = { modelId = m })
                                Text(m, color = Color.White, fontSize = 12.sp)
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            if (!chatViewModel.isSelectingType) {
                TextButton(onClick = { 
                    if (name.isNotBlank() && endpoint.isNotBlank()) {
                        if (chatViewModel.editingProvider != null) {
                            activity?.deleteCloudProvider(chatViewModel.editingProvider!!.name)
                        }
                        activity?.addCloudProvider(name, providerType, endpoint, modelId)
                        chatViewModel.showAddCloudDialog = false
                        chatViewModel.isSelectingType = true
                        chatViewModel.fetchedModels.clear()
                    }
                }) { Text("SAVE", color = Color.Cyan) }
            }
        },
        dismissButton = {
            TextButton(onClick = { 
                if (!chatViewModel.isSelectingType && chatViewModel.editingProvider == null) {
                    chatViewModel.isSelectingType = true
                } else {
                    chatViewModel.showAddCloudDialog = false
                    chatViewModel.isSelectingType = true
                    chatViewModel.fetchedModels.clear()
                }
            }) { Text(if (!chatViewModel.isSelectingType && chatViewModel.editingProvider == null) "BACK" else "CANCEL", color = Color.Gray) }
        }
    )
}

@Composable
fun BootstrapWizard(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>) {
    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) { Icon(Icons.Default.AutoAwesome, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(80.dp)); Spacer(Modifier.height(24.dp)); Text("Ronin Kernel: Setup", fontSize = 24.sp, fontWeight = FontWeight.Bold, color = Color.White); Spacer(Modifier.height(16.dp)); Text("No Reasoning Spine detected.\nPlease import a Gemma 4 (.litertlm) model.", fontSize = 14.sp, color = Color.Gray, textAlign = androidx.compose.ui.text.style.TextAlign.Center, modifier = Modifier.padding(horizontal = 48.dp)); Spacer(Modifier.height(48.dp)); when (chatViewModel.wizardState) { WizardState.IMPORTING -> Column(horizontalAlignment = Alignment.CenterHorizontally) { CircularProgressIndicator(color = Color(0xFF64B5F6)); Text("Copying Data...", color = Color.Gray, fontSize = 11.sp) } ; WizardState.VERIFYING -> Column(horizontalAlignment = Alignment.CenterHorizontally) { CircularProgressIndicator(color = Color.Green); Text("Validating...", color = Color.Gray, fontSize = 11.sp) } ; else -> { Button(onClick = { brainPicker.launch(arrayOf("*/*")) }, colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6)), shape = RoundedCornerShape(24.dp)) { Text("IMPORT MODEL", color = Color.Black, fontWeight = FontWeight.Bold, modifier = Modifier.padding(horizontal = 16.dp)) } } } }
}

@Composable
fun ChatBubble(msg: ChatMessage, onContinue: () -> Unit = {}, onFeedback: (Boolean) -> Unit = {}) {
    val isUser = msg.sender == "User"
    var isThoughtExpanded by remember { mutableStateOf(false) }
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        if (!isUser) Text("Ronin", fontSize = 10.sp, color = Color.Gray, modifier = Modifier.padding(start = 4.dp, bottom = 2.dp))
        Surface(color = if (isUser) Color(0xFF2D3142) else Color(0xFF1E2130), shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp, bottomStart = if (isUser) 16.dp else 4.dp, bottomEnd = if (isUser) 4.dp else 16.dp), elevation = 2.dp) { 
            SelectionContainer { 
                Column(modifier = Modifier.padding(12.dp)) {
                    if (!isUser && (msg.thoughtContent.isNotEmpty() || msg.isThinking)) {
                        // ... existing reasoning UI
                        Row(modifier = Modifier.fillMaxWidth().clickable { isThoughtExpanded = !isThoughtExpanded }.padding(bottom = if (isThoughtExpanded || msg.content.isNotEmpty()) 8.dp else 0.dp), verticalAlignment = Alignment.CenterVertically) {
                            Icon(if (isThoughtExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown, null, tint = Color.Gray, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(4.dp))
                            Text(if (msg.isThinking) "Reasoning..." else "Thought Process", color = Color.Gray, fontSize = 11.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                            if (msg.isThinking) {
                                Spacer(Modifier.width(8.dp))
                                CircularProgressIndicator(modifier = Modifier.size(10.dp), strokeWidth = 1.dp, color = Color.Gray)
                            }
                        }
                        AnimatedVisibility(visible = isThoughtExpanded) {
                            Text(msg.thoughtContent, color = Color.Cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(bottom = if (msg.content.isNotEmpty()) 8.dp else 0.dp, start = 8.dp))
                        }
                    }
                    if (!isUser && msg.content.isEmpty() && !msg.isThinking && msg.thoughtContent.isEmpty()) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.size(12.dp), strokeWidth = 2.dp, color = Color.Cyan)
                            Spacer(Modifier.width(8.dp))
                            Text("Ronin is preparing...", color = Color.Cyan, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                    if (msg.content.isNotEmpty()) {
                        Row(verticalAlignment = Alignment.Top) {
                            Text(msg.content, color = Color.White, fontSize = 15.sp, lineHeight = 20.sp, modifier = Modifier.weight(1f))
                            val context = LocalContext.current
                            IconButton(onClick = { 
                                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                                val clip = android.content.ClipData.newPlainText("Ronin Message", msg.content)
                                clipboard.setPrimaryClip(clip)
                                Toast.makeText(context, "Copied to clipboard", Toast.LENGTH_SHORT).show()
                            }, modifier = Modifier.size(20.dp).padding(start = 4.dp, top = 2.dp)) {
                                Icon(Icons.Default.ContentCopy, "Copy", tint = Color.Gray, modifier = Modifier.size(12.dp))
                            }
                        }
                    }

                    // v10.2.17: RLHF Feedback Row
                    if (!isUser && msg.content.isNotEmpty() && !msg.isThinking && !msg.feedbackGiven) {
                        Spacer(Modifier.height(12.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Helpful?", color = Color.Gray, fontSize = 10.sp)
                            Spacer(Modifier.width(8.dp))
                            IconButton(onClick = { msg.feedbackGiven = true; onFeedback(true) }, modifier = Modifier.size(24.dp)) {
                                Icon(Icons.Default.ThumbUp, null, tint = Color.Gray.copy(alpha = 0.6f), modifier = Modifier.size(14.dp))
                            }
                            Spacer(Modifier.width(4.dp))
                            IconButton(onClick = { msg.feedbackGiven = true; onFeedback(false) }, modifier = Modifier.size(24.dp)) {
                                Icon(Icons.Default.ThumbDown, null, tint = Color.Gray.copy(alpha = 0.6f), modifier = Modifier.size(14.dp))
                            }
                        }
                    } else if (!isUser && msg.feedbackGiven) {
                        Spacer(Modifier.height(8.dp))
                        Text("Thanks for the feedback!", color = Color(0xFF64B5F6), fontSize = 9.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                    }
                    
                    if (!isUser && msg.isTruncated) {
                        Spacer(Modifier.height(8.dp))
                        OutlinedButton(
                            onClick = onContinue,
                            modifier = Modifier.height(32.dp),
                            shape = RoundedCornerShape(16.dp),
                            border = BorderStroke(1.dp, Color(0xFF64B5F6)),
                            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp)
                        ) {
                            if (msg.isContinuing) {
                                CircularProgressIndicator(modifier = Modifier.size(12.dp), strokeWidth = 1.dp, color = Color(0xFF64B5F6))
                            } else {
                                Icon(Icons.Default.PlayArrow, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(14.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("ကျန်ရှိသည်များကို ဆက်လက်ဖတ်ရှုမည် (Continue)", color = Color(0xFF64B5F6), fontSize = 11.sp)
                            }
                        }
                    }
                }
            } 
        }
    }
}

@Composable
fun SystemInfoPanel(chatViewModel: ChatViewModel) { Surface(color = Color(0xFF161922).copy(alpha = 0.8f), modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).clip(RoundedCornerShape(12.dp))) { Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth().padding(12.dp)) { InfoItem("Thermal", "${chatViewModel.systemTemperature}°C", if (chatViewModel.systemTemperature > 40) Color.Red else Color.Green); InfoItem("RAM", "${"%.2f".format(chatViewModel.ramUsedGB)}GB", Color.White); InfoItem("LMK", "${chatViewModel.lmkPressure}%", Color.Cyan) } } }

@Composable
fun InfoItem(l: String, v: String, c: Color) { Column(horizontalAlignment = Alignment.CenterHorizontally) { Text(l, fontSize = 9.sp, color = Color.Gray); Text(v, fontSize = 13.sp, color = c, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace) } }

@Composable
fun Divider(modifier: Modifier = Modifier) { androidx.compose.material.Divider(color = Color.Gray.copy(alpha = 0.15f), modifier = modifier) }

fun Context.findActivity(): ComponentActivity? {
    var context = this
    while (context is ContextWrapper) { if (context is ComponentActivity) return context; context = context.baseContext }
    return null
}
