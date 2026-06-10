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
                
                // Filter for Documents and Downloads folders and common document extensions
                val selection = "(${android.provider.MediaStore.Files.FileColumns.DATA} LIKE ? OR ${android.provider.MediaStore.Files.FileColumns.DATA} LIKE ?) AND " +
                                "(${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.md' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.txt' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.pdf' OR " +
                                "${android.provider.MediaStore.Files.FileColumns.DATA} LIKE '%.doc%')"
                
                val selectionArgs = arrayOf("%/Documents/%", "%/Download/%")
                
                val cursor = contentResolver.query(
                    android.provider.MediaStore.Files.getContentUri("external"),
                    projection,
                    selection,
                    selectionArgs,
                    null
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
            android.Manifest.permission.WRITE_CALENDAR
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        
        // v11.3: Use manual request to avoid 16-bit requestCode crash in FragmentActivity
        val needed = permissions.filter { checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED }
        if (needed.isNotEmpty()) {
            requestPermissions(needed.toTypedArray(), 101)
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
            val actionName = params["action"]?.uppercase() ?: toolName
            Log.i("RoninKernel_MainActivity", "Executing Tool: $toolName (Action: $actionName) with params: $params")
            when (actionName) {
                "MEMORY", "SAVE_NOTE", "SAVE_FACT", "QUERY_FACT", "SAVE_VAULT", "QUERY_VAULT", 
                "SEARCH_NOTES", "SEARCH_EPISODES", "ADD_FACT", "STORE_FACT", "ADD_NOTE", "STORE_NOTE",
                "LOOKUP_FACT", "LOOKUP_VAULT", "ADD_VAULT", "STORE_VAULT" -> {
                    try {
                        when (actionName) {
                            "SAVE_NOTE", "ADD_NOTE", "STORE_NOTE" -> {
                                val title = params["note_title"] ?: params["title"] ?: params.keys.find { it.contains("title", true) }?.let { params[it] } ?: "Untitled Note"
                                val content = params["note_content"] ?: params["content"] ?: params["text"] ?: params.values.find { it.length > 5 } ?: ""
                                
                                if (content.isEmpty()) {
                                    val err = "Error: Note content is empty. Params: $params"
                                    nativeEngine.pushKernelMessage("[AGENT] $err")
                                    err
                                } else if (nativeEngine.storeNote(title, content, "")) "Note saved: $title" 
                                else "Error: Database failure during SAVE_NOTE."
                            }
                            "SAVE_FACT", "ADD_FACT", "STORE_FACT" -> {
                                // v10.1.16: Intelligent parameter matching
                                val entity = params["entity"] ?: params["item"] ?: params.keys.find { it.contains("name", true) || it.contains("car", true) || it.contains("person", true) }?.let { params[it] } ?: "Unknown"
                                val rawAttr = params["attribute"] ?: params["property"] ?: params.keys.find { it.contains("type", true) || it.contains("field", true) }?.let { params[it] } ?: "General"
                                
                                // v10.2.14: Semantic Attribute Normalization
                                val attr = normalizeAttribute(rawAttr)

                                // Try to find the most likely 'value'
                                val value = params["value"] ?: params["content"] ?: params.keys.find { 
                                    val k = it.lowercase()
                                    // v10.2.14: Exclude title/entity keys from value search to prevent corruption
                                    !k.contains("title") && !k.contains("entity") && !k.contains("name") && !k.contains("attribute") &&
                                    (k.contains("plate") || k.contains("medicine") || k.contains("number") || k.contains("result") || k.contains("val") || k.contains("data") || k.contains("content"))
                                }?.let { params[it] } ?: params.values.firstOrNull { it != entity && it != rawAttr && it.length > 2 } ?: ""
                                
                                if (value.isEmpty()) {
                                    val err = "Error: Fact value is empty. Params: $params"
                                    nativeEngine.pushKernelMessage("[AGENT] $err")
                                    err
                                } else if (nativeEngine.storeFact(entity, attr, value)) {
                                    "Fact saved: $entity.$attr = $value (Source: User)"
                                } else {
                                    val err = "Error: Database failure during SAVE_FACT."
                                    nativeEngine.pushKernelMessage("[AGENT] $err")
                                    err
                                }
                            }
                            "SAVE_VAULT", "ADD_VAULT", "STORE_VAULT" -> {
                                val title = params["vault_title"] ?: params["title"] ?: params.keys.find { it.contains("name", true) || it.contains("title", true) }?.let { params[it] } ?: "Secret"
                                
                                val content = params["vault_content"] ?: params["content"] ?: params["value"] ?: params.keys.find {
                                    val k = it.lowercase()
                                    // v10.2.14: Exclude title keys from content search to prevent corruption
                                    !k.contains("title") && !k.contains("name") &&
                                    (k.contains("key") || k.contains("token") || k.contains("password") || k.contains("secret") || k.contains("pin"))
                                }?.let { params[it] } ?: ""
                                
                                if (content.isEmpty()) {
                                    val err = "Error: Vault content is empty. Params: $params"
                                    nativeEngine.pushKernelMessage("[AGENT] $err")
                                    err
                                } else authenticateAndExecute("Store Secret", "Authenticate to encrypt and store in Vault") {
                                    val encrypted = nativeEngine.encryptSecret(content)
                                    if (nativeEngine.storeVault(title, encrypted)) "Vault entry saved: $title" 
                                    else {
                                        val err = "Error: Database failure during SAVE_VAULT."
                                        nativeEngine.pushKernelMessage("[AGENT] $err")
                                        err
                                    }
                                }
                            }
                            "QUERY_VAULT", "LOOKUP_VAULT" -> {
                                val title = params["vault_title"] ?: params["title"] ?: params["entity"] ?: params["query"] ?: 
                                            params.keys.find { 
                                                val k = it.lowercase()
                                                k.contains("name") || k.contains("title") || k.contains("subject") || k.contains("item")
                                            }?.let { params[it] } ?: ""
                                if (title.isEmpty()) "Error: Vault search title is empty. Params: $params"
                                else authenticateAndExecute("Access Vault", "Authenticate to retrieve secret: $title") {
                                    val encrypted = nativeEngine.lookupVault(title)
                                    if (encrypted.isNotEmpty()) {
                                        val decrypted = nativeEngine.decryptSecret(encrypted)
                                        val uiMsg = "\n[VAULT] $title: $decrypted"
                                        nativeEngine.pushTokenToUI(uiMsg, true)
                                        nativeEngine.pushKernelMessage("[AGENT] Vault Found: $title")
                                        decrypted
                                    } else "Error: No vault entry found for '$title'."
                                }
                            }
                            "SEARCH_NOTES" -> {
                                val query = params["query"] ?: ""
                                val results = nativeEngine.searchNotes(query)
                                if (results.isNotEmpty()) {
                                    val formatted = results.joinToString("\n---\n")
                                    val uiMsg = "\n[NOTES FOUND]\n$formatted"
                                    nativeEngine.pushTokenToUI(uiMsg, true)
                                    nativeEngine.pushKernelMessage("[AGENT] Found Notes")
                                    "Found ${results.size} notes matching '$query'."
                                } else "Error: No notes found matching '$query'."
                            }
                            "SEARCH_EPISODES" -> {
                                val query = params["query"] ?: ""
                                val results = nativeEngine.searchEpisodes(query)
                                if (results.isNotEmpty()) {
                                    val formatted = results.joinToString("\n---\n")
                                    val uiMsg = "\n[HISTORY FOUND]\n$formatted"
                                    nativeEngine.pushTokenToUI(uiMsg, true)
                                    nativeEngine.pushKernelMessage("[AGENT] Found Episodes")
                                    "Found ${results.size} episodes matching '$query'."
                                } else "Error: No history found matching '$query'."
                            }
                            "QUERY_FACT", "MEMORY", "LOOKUP_FACT" -> {
                                val entity = params["entity"] ?: params["item"] ?: params.keys.find { it.contains("name", true) || it.contains("car", true) || it.contains("person", true) }?.let { params[it] } ?: "Unknown"
                                val rawAttr = params["attribute"] ?: params["property"] ?: params.keys.find { it.contains("type", true) || it.contains("field", true) || it.contains("attribute", true) }?.let { params[it] } ?: "General"
                                
                                // v10.2.14: Semantic Attribute Normalization
                                val attr = normalizeAttribute(rawAttr)

                                val isSensitive = attr.lowercase().let { it.contains("key") || it.contains("password") || it.contains("pin") || it.contains("secret") }
                                
                                val executeLookup = {
                                    val res = nativeEngine.lookupFact(entity, attr)
                                    if (res.isNotEmpty()) {
                                        val uiMsg = "\n[FACT] $entity's $attr is $res"
                                        nativeEngine.pushTokenToUI(uiMsg, true)
                                        nativeEngine.pushKernelMessage("[AGENT] Fact Found: $entity's $attr")
                                        res 
                                    } else "Error: No information found for $entity's $attr."
                                }

                                if (isSensitive) {
                                    authenticateAndExecute("Access Sensitive Fact", "Authenticate to retrieve $entity's $attr", executeLookup)
                                } else {
                                    executeLookup()
                                }
                            }
                            else -> "Error: Unknown memory action $toolName"
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
                "LOCATION", "GET_LOCATION" -> {
                    try {
                        val task = fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, CancellationTokenSource().token)
                        val res = try { Tasks.await(task, 5, TimeUnit.SECONDS) } catch (e: Exception) { Tasks.await(fusedLocationClient.lastLocation, 2, TimeUnit.SECONDS) }
                        res?.let { JSONObject().put("lat", it.latitude).put("lon", it.longitude).toString() } ?: "Error: GPS Unavailable"
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "MAP", "OPEN_MAP", "show_map" -> {
                    try {
                        val locJson = params["context_result_LOCATION"] ?: params["payload"]
                        var lat = "0.0"; var lon = "0.0"
                        if (locJson != null && locJson.startsWith("{")) {
                            val j = JSONObject(locJson); lat = j.opt("lat").toString(); lon = j.opt("lon").toString()
                        } else {
                            // v12.4: Smart Fallback - get last known location if not provided
                            val task = fusedLocationClient.lastLocation
                            val res = try { Tasks.await(task, 2, TimeUnit.SECONDS) } catch (e: Exception) { null }
                            res?.let { lat = it.latitude.toString(); lon = it.longitude.toString() }
                        }
                        val uri = Uri.parse("geo:$lat,$lon?q=$lat,$lon")
                        val intent = Intent(Intent.ACTION_VIEW, uri).apply {
                            setPackage("com.google.android.apps.maps")
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        runOnUiThread { 
                            try { startActivity(intent) } 
                            catch (e: Exception) { startActivity(Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
                        }
                        "Opened Map at $lat, $lon"
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "ALARM", "SET_ALARM", "alarm" -> {
                    try {
                        val timeStr = params["time"] ?: params["hour"] ?: "06:00"
                        val message = params["message"] ?: params["label"] ?: "Ronin Alarm"
                        val parts = timeStr.split(":")
                        val hour = parts.getOrNull(0)?.toIntOrNull() ?: 6
                        val minute = parts.getOrNull(1)?.toIntOrNull() ?: 0
                        val intent = Intent(android.provider.AlarmClock.ACTION_SET_ALARM).apply {
                            putExtra(android.provider.AlarmClock.EXTRA_MESSAGE, message)
                            putExtra(android.provider.AlarmClock.EXTRA_HOUR, hour)
                            putExtra(android.provider.AlarmClock.EXTRA_MINUTES, minute)
                            putExtra(android.provider.AlarmClock.EXTRA_SKIP_UI, false)
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        runOnUiThread { startActivity(intent) }
                        "Set alarm for $hour:$minute with message: $message"
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "CALENDAR", "ADD_EVENT", "event", "set_calendar" -> {
                    try {
                        val title = params["title"] ?: params["event"] ?: "Ronin Event"
                        var desc = params["description"] ?: params["details"] ?: ""
                        val time = params["time"] ?: ""
                        val timezone = params["timezone"] ?: ""
                        val attendees = params["attendees"] ?: ""
                        val shareVia = params["share_via"] ?: ""

                        val cal = java.util.Calendar.getInstance()
                        var beginTime = cal.timeInMillis
                        val isTomorrow = time.contains("tomorrow", true) || time.contains("မနက်ဖြန်", true)
                        
                        // v12.1: Robust time parsing (Regex matches 11:00, 11နာရီ, 11:30, 11နာရီခွဲ)
                        val timeRegex = Regex("(\\d{1,2})[:\\s]*(\\d{2})?|(\\d{1,2})\\s*(နာရီ|နာရီခွဲ|am|pm)")
                        val match = timeRegex.find(time.lowercase())
                        if (match != null) {
                            if (isTomorrow) cal.add(java.util.Calendar.DAY_OF_YEAR, 1)
                            
                            val h1 = match.groupValues[1].takeIf { it.isNotEmpty() }?.toIntOrNull()
                            val m1 = match.groupValues[2].takeIf { it.isNotEmpty() }?.toIntOrNull() ?: 0
                            val h2 = match.groupValues[3].takeIf { it.isNotEmpty() }?.toIntOrNull()
                            val m2 = if (match.groupValues[4].contains("ခွဲ")) 30 else 0
                            
                            var hour = h1 ?: h2 ?: 9
                            val minute = if (h1 != null) m1 else m2
                            
                            if (time.lowercase().contains("pm") && hour < 12) hour += 12
                            
                            cal.set(java.util.Calendar.HOUR_OF_DAY, hour)
                            cal.set(java.util.Calendar.MINUTE, minute)
                            cal.set(java.util.Calendar.SECOND, 0)
                            beginTime = cal.timeInMillis
                        } else if (isTomorrow) {
                            cal.add(java.util.Calendar.DAY_OF_YEAR, 1)
                            cal.set(java.util.Calendar.HOUR_OF_DAY, 10) // Default 10 AM tomorrow
                            beginTime = cal.timeInMillis
                        }

                        if (time.isNotEmpty()) desc += "\nOriginal Time: $time"
                        if (timezone.isNotEmpty()) desc += " ($timezone)"
                        if (attendees.isNotEmpty()) desc += "\nAttendees: $attendees"

                        val intent = Intent(Intent.ACTION_INSERT).apply {
                            data = android.provider.CalendarContract.Events.CONTENT_URI
                            putExtra(android.provider.CalendarContract.Events.TITLE, title)
                            putExtra(android.provider.CalendarContract.Events.DESCRIPTION, desc)
                            putExtra(android.provider.CalendarContract.EXTRA_EVENT_BEGIN_TIME, beginTime)
                            putExtra(android.provider.CalendarContract.EXTRA_EVENT_END_TIME, beginTime + 60 * 60 * 1000)
                            if (timezone.isNotEmpty() && timezone != "system_default") {
                                putExtra(android.provider.CalendarContract.Events.EVENT_TIMEZONE, timezone)
                            }
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        
                        runOnUiThread { 
                            startActivity(intent) 
                            if (shareVia.contains("sms", true)) {
                                val smsIntent = Intent(Intent.ACTION_SENDTO).apply {
                                    data = Uri.parse("smsto:")
                                    putExtra("sms_body", "Meeting: $title\nTime: $time\nDetails: $desc")
                                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                }
                                try { startActivity(smsIntent) } catch (e: Exception) {}
                            }
                        }
                        "Scheduled $title for ${java.text.DateFormat.getDateTimeInstance().format(cal.time)}"
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "READ_CALENDAR", "QUERY_CALENDAR" -> {
                    try {
                        val keyword = params["keyword"] ?: params["query"] ?: params["event"] ?: ""
                        if (checkSelfPermission(android.Manifest.permission.READ_CALENDAR) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                            requestPermissions(arrayOf(android.Manifest.permission.READ_CALENDAR), 101)
                            "Error: Missing READ_CALENDAR permission."
                        } else {
                            val projection = arrayOf(
                                android.provider.CalendarContract.Events.TITLE,
                                android.provider.CalendarContract.Events.DTSTART,
                                android.provider.CalendarContract.Events.EVENT_LOCATION
                            )
                            // v12.4: Search a wider range (1 year) to allow "searching back"
                            val rangeStart = System.currentTimeMillis() - 365L * 24 * 60 * 60 * 1000
                            val selection = "${android.provider.CalendarContract.Events.DTSTART} >= ?"
                            val selectionArgs = arrayOf(rangeStart.toString())
                            val cursor = contentResolver.query(
                                android.provider.CalendarContract.Events.CONTENT_URI,
                                projection, selection, selectionArgs, "${android.provider.CalendarContract.Events.DTSTART} DESC LIMIT 20"
                            )
                            val results = mutableListOf<String>()
                            cursor?.use {
                                val titleIdx = it.getColumnIndexOrThrow(android.provider.CalendarContract.Events.TITLE)
                                val timeIdx = it.getColumnIndexOrThrow(android.provider.CalendarContract.Events.DTSTART)
                                val locIdx = it.getColumnIndexOrThrow(android.provider.CalendarContract.Events.EVENT_LOCATION)
                                while (it.moveToNext()) {
                                    val t = it.getString(titleIdx)
                                    val time = java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.getDefault()).format(java.util.Date(it.getLong(timeIdx)))
                                    val loc = it.getString(locIdx) ?: ""
                                    if (keyword.isEmpty() || t.contains(keyword, true)) {
                                        results.add("Event: $t\nTime: $time\nLocation: $loc")
                                    }
                                }
                            }
                            val output = if (results.isEmpty()) "No events found matching '$keyword'." else results.joinToString("\n---\n")
                            nativeEngine.pushTokenToUI("\n[CALENDAR RESULTS]\n$output", true)
                            output
                        }
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "SMS", "SEND_SMS", "send_sms" -> {
                    try {
                        val locJson = params["context_result_LOCATION"]
                        val conJson = params["context_result_CONTACTS"]
                        var recipient = params["recipient_number"] ?: params["recipient"] ?: params["recipient_name"] ?: ""
                        var body = params["message"] ?: params["sms_body"] ?: params["text"] ?: ""

                        if (conJson != null && conJson.startsWith("{")) {
                            try {
                                val j = JSONObject(conJson)
                                recipient = j.optString("phone_number", j.optString("message", recipient))
                            } catch (e: Exception) {}
                        } else if (conJson != null && !conJson.startsWith("Error")) {
                            recipient = conJson
                        }

                        if (locJson != null && locJson.startsWith("{")) {
                             val j = JSONObject(locJson); val lat = j.opt("lat").toString(); val lon = j.opt("lon").toString()
                             val mapsLink = "📍 My Location\nLat: $lat\nLon: $lon\n\nhttps://maps.google.com/?q=$lat,$lon"
                             // v12.10: Overwrite generic placeholder generated by LLM if location is requested
                             if (body.isEmpty() || body.contains("တည်နေရာ") || body.contains("location")) {
                                 body = mapsLink
                             } else {
                                 body += "\n\n$mapsLink"
                             }
                        }
                        
                        if (!recipient.matches(Regex("^[+]?[0-9\\- ]{5,}+$"))) {
                             val resolved = resolveContactName(recipient)
                             if (!resolved.startsWith("NOT_FOUND") && resolved != "PERMISSION_DENIED") {
                                 recipient = resolved
                             }
                        }

                        val cleanRecipient = if (recipient.matches(Regex("^[+]?[0-9\\- ]{7,}+$"))) recipient else ""
                        
                        val reqId = params["request_id"]
                        if (reqId != null) {
                            runOnUiThread {
                                val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
                                chatViewModel.hitlIntentName = "Send SMS"
                                chatViewModel.hitlMessage = "To: $cleanRecipient\n\n$body"
                                chatViewModel.onHITLResult = { approved ->
                                    if (approved) {
                                        try {
                                            val intent = Intent(Intent.ACTION_SENDTO).apply {
                                                data = Uri.parse("smsto:$cleanRecipient")
                                                putExtra("sms_body", body)
                                                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                            }
                                            this@MainActivity.startActivity(intent)
                                            nativeEngine.submitCapabilityResponseSafe(reqId, true, "Opened SMS composer for $cleanRecipient")
                                            nativeEngine.pushKernelMessage("[AGENT] Opened SMS Composer")
                                        } catch (e: Exception) {
                                            nativeEngine.submitCapabilityResponseSafe(reqId, false, "SMS Failed: ${e.message}")
                                        }
                                    } else {
                                        nativeEngine.submitCapabilityResponseSafe(reqId, false, "Cancelled by user")
                                        nativeEngine.pushKernelMessage("[AGENT] SMS Cancelled")
                                    }
                                }
                                chatViewModel.showHITLDialog = true
                            }
                            "[ASYNC_PENDING]"
                        } else {
                            // Fallback to composer
                            runOnUiThread {
                                val intent = Intent(Intent.ACTION_SENDTO).apply {
                                    data = Uri.parse("smsto:${Uri.encode(cleanRecipient)}")
                                    putExtra("sms_body", body)
                                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                }
                                startActivity(intent)
                            }
                            "Opened SMS composer for $cleanRecipient."
                        }
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "FILE_SEARCH", "FILES", "SEARCH_FILES" -> {
                    try {
                        val query = params["query"] ?: params["keyword"] ?: ""
                        val noteResults = nativeEngine.searchNotes(query)
                        val epResults = nativeEngine.searchEpisodes(query)
                        val fileResults = nativeEngine.searchFiles(query)
                        
                        val combined = mutableListOf<String>()
                        if (noteResults != null) combined.addAll(noteResults.toList())
                        if (epResults != null) combined.addAll(epResults.toList())
                        if (fileResults != null) combined.addAll(fileResults.toList())
                        
                        val output = if (combined.isEmpty()) "No files or documents found matching: $query"
                        else "Found ${combined.size} items:\n" + combined.take(5).joinToString("\n---\n")
                        
                        nativeEngine.pushTokenToUI("\n[SEARCH RESULTS]\n$output", true)
                        output
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                "SENSOR", "SENSOR_ANALYSIS", "GET_SENSOR_ANALYSIS", "get_sensor_analysis" -> {
                    try {
                        val sensorType = params["sensor_type"] ?: "accelerometer"
                        val analysis = sensorDriver.execute(JSONObject().put("action", "get_sensor_analysis").put("sensor_type", sensorType))
                        val output = analysis.toString()
                        nativeEngine.pushTokenToUI("\n[SENSOR ANALYSIS]\n$output", true)
                        output
                    } catch (e: Exception) { "Error: ${e.message}" }
                }
                else -> "Tool $toolName executed with params: $params"
            }
        }
    }

    override fun onResume() { super.onResume(); scanLocalModels() }
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
                        Box(modifier = Modifier.height(150.dp).fillMaxWidth().background(Color(0xFF12141C)).padding(8.dp).border(1.dp, Color.Cyan.copy(alpha = 0.2f), RoundedCornerShape(8.dp))) { 
                            val scrollState = rememberScrollState()
                            LaunchedEffect(chatViewModel.reasoningLogsText) { scrollState.animateScrollTo(scrollState.maxValue) }
                            Text(chatViewModel.reasoningLogsText, color = Color.Cyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.verticalScroll(scrollState))
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
                        Text(msg.content, color = Color.White, fontSize = 15.sp, lineHeight = 20.sp)
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
