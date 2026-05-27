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
import kotlinx.coroutines.withContext

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
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
    initialThoughtContent: String = ""
) {
    var content by mutableStateOf(initialContent)
    var isThinking by mutableStateOf(initialIsThinking)
    var thoughtContent by mutableStateOf(initialThoughtContent)

    fun copy(
        content: String = this.content,
        isThinking: Boolean = this.isThinking,
        thoughtContent: String = this.thoughtContent
    ) = ChatMessage(id, sender, content, isThinking, thoughtContent)
}

enum class WizardState { MISSING_CORE, IMPORTING, VERIFYING, ACTIVE }

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<ChatMessage>()
    var reasoningLogsText by mutableStateOf("") // v3.5: Unified console text
    
    var showSysInfo by mutableStateOf(false)
    var showReasoning by mutableStateOf(true) // Default show for feedback
    var lmkPressure by mutableStateOf(0)
    
    var wizardState by mutableStateOf(WizardState.MISSING_CORE)
    var isGemmaReady by mutableStateOf(false)
    var showCommandSuggestions by mutableStateOf(false)
    
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

    var systemPrompt by mutableStateOf("You are Ronin. Always reason inside [THINK] [/THINK] and then reply inside [REPLY] [/REPLY] in Myanmar.")

    var showAddCloudDialog by mutableStateOf(false)
    var editingProvider by mutableStateOf<CloudProvider?>(null)
    var isSelectingType by mutableStateOf(true)
    var fetchedModels = mutableStateListOf<String>()
    var isFetchingModels by mutableStateOf(false)
    var kernelStatus by mutableStateOf("Initializing...")
    var isGenerating by mutableStateOf(false)
}

class MainActivity : ComponentActivity() {
    internal lateinit var nativeEngine: NativeEngine
    private lateinit var sharedPreferences: android.content.SharedPreferences
    private lateinit var fusedLocationClient: FusedLocationProviderClient
    // ... rest of the class remains same but I need to include the modified methods
    // I will replace larger blocks to be safe.

    private val requestPermissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
        if (permissions.entries.all { it.value }) scanLocalModels()
    }

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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        nativeEngine = NativeEngine(this)
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
            savePrimaryCloudProvider(sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini")
            saveOfflineMode(sharedPreferences.getBoolean("offline_mode", false))
            nativeEngine.updateSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, chatViewModel.topP)

            chatViewModel.kernelStatus = "Neural Bridge Active"
            checkAndRequestPermissions(); scanLocalModels()
            val savedModelPath = sharedPreferences.getString("local_model_path", "")
            if (!savedModelPath.isNullOrEmpty() && File(savedModelPath).exists()) hydrateModel(savedModelPath)

            launch {
                nativeEngine.inferenceFlow.collect { packet ->
                    if (chatViewModel.messages.isNotEmpty()) {
                        val lastMsg = chatViewModel.messages.last()
                        if (lastMsg.sender == "Ronin") {
                            val frag = packet.fragment
                            
                            if (frag.contains("[THINK]")) { lastMsg.isThinking = true }
                            
                            if (lastMsg.isThinking) {
                                val clean = frag.replace("[THINK]", "").replace("[/THINK]", "")
                                lastMsg.thoughtContent += clean
                                if (chatViewModel.isThinkingEnabled) {
                                    chatViewModel.reasoningLogsText += clean
                                }
                                if (frag.contains("[/THINK]") || frag.contains("[REPLY]")) {
                                    lastMsg.isThinking = false
                                }
                            } else {
                                // v3.6: Strict streaming with tag stripping
                                val cleanReply = frag.replace("[REPLY]", "").replace("[/REPLY]", "").replace("[/THINK]", "")
                                lastMsg.content += cleanReply
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
            if (!capFile.exists()) assets.open("capabilities.json").use { input -> java.io.FileOutputStream(capFile).use { output -> input.copyTo(output) } }
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

    private fun checkAndRequestPermissions() {
        val permissions = mutableListOf(android.Manifest.permission.ACCESS_FINE_LOCATION, android.Manifest.permission.ACCESS_COARSE_LOCATION, android.Manifest.permission.CAMERA)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        requestPermissionLauncher.launch(permissions.toTypedArray())
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
            vm.reasoningLogsText += "\n> $msg"
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
                    val temp = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.let { it / 10f } ?: 35f
                    withContext(Dispatchers.Main) { 
                        chatViewModel.ramUsedGB = used; chatViewModel.ramTotalGB = total
                        chatViewModel.systemTemperature = temp
                        chatViewModel.lmkPressure = engine.getLMKPressureSafe() 
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
                        LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) { items(chatViewModel.messages.asReversed()) { ChatBubble(it) } } 
                        if (chatViewModel.showCommandSuggestions) {
                            val suggestions = listOf("/status", "/skills", "/model", "/reset").filter { it.startsWith(currentInput.lowercase()) }
                            if (suggestions.isNotEmpty()) { Surface(color = Color(0xFF25283D), modifier = Modifier.align(Alignment.BottomStart).padding(16.dp).fillMaxWidth(0.7f).clip(RoundedCornerShape(12.dp)), elevation = 8.dp) { LazyColumn(modifier = Modifier.heightIn(max = 200.dp)) { items(suggestions) { s -> TextButton(onClick = { currentInput = "$s "; chatViewModel.showCommandSuggestions = false }, modifier = Modifier.fillMaxWidth()) { Text(s, color = Color.White) } } } } }
                        }
                    }
                    Surface(elevation = 8.dp, color = Color(0xFF1A1C2C)) {
                        Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                            TextField(value = currentInput, onValueChange = { currentInput = it; chatViewModel.showCommandSuggestions = it.startsWith("/") }, modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)), colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White), trailingIcon = { IconButton(onClick = { 
                                if (currentInput.isNotBlank()) { 
                                    val raw = currentInput; chatViewModel.messages.add(ChatMessage(System.currentTimeMillis(), "User", raw)); currentInput = ""; chatViewModel.isGenerating = true
                                    chatViewModel.reasoningLogsText = "> Processing: $raw"
                                    scope.launch { 
                                        val isCommand = raw.trim().startsWith("/")
                                        val roninMsg = ChatMessage(System.currentTimeMillis() + 1, "Ronin", "")
                                        chatViewModel.messages.add(roninMsg)
                                        val msgIdx = chatViewModel.messages.size - 1

                                        if (!isCommand && (chatViewModel.cloudOnlyMode || !chatViewModel.isGemmaReady)) { 
                                            val apiKey = engine.getSecureApiKeyProvider?.invoke(chatViewModel.primaryCloudProvider) ?: ""
                                            val res = engine.performCloudInferenceAsync(raw, chatViewModel.primaryCloudProvider, apiKey)
                                            roninMsg.content = res
                                        } else { 
                                            val result = engine.processInputAsync(raw, chatViewModel.systemPrompt)
                                            if (roninMsg.content.isEmpty()) {
                                                roninMsg.content = result
                                            }
                                        } ; chatViewModel.isGenerating = false 
                                    } 
                                } 
                            }) { Icon(if (chatViewModel.isGenerating) Icons.Default.HourglassEmpty else Icons.Default.Send, null, tint = Color(0xFF64B5F6)) } })
                        }
                    }
                }
            }
        }
    }
    if (chatViewModel.showAddCloudDialog) {
        CloudProviderDialog(chatViewModel, activity)
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
fun ChatBubble(msg: ChatMessage) {
    val isUser = msg.sender == "User"
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        if (!isUser) Text("Ronin", fontSize = 10.sp, color = Color.Gray, modifier = Modifier.padding(start = 4.dp, bottom = 2.dp))
        Surface(color = if (isUser) Color(0xFF2D3142) else Color(0xFF1E2130), shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp, bottomStart = if (isUser) 16.dp else 4.dp, bottomEnd = if (isUser) 4.dp else 16.dp), elevation = 2.dp) { 
            SelectionContainer { 
                Column(modifier = Modifier.padding(12.dp)) {
                    if (!isUser && msg.content.isEmpty()) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.size(12.dp), strokeWidth = 2.dp, color = Color.Cyan)
                            Spacer(Modifier.width(8.dp))
                            Text("Ronin is reasoning...", color = Color.Cyan, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                    if (msg.content.isNotEmpty()) {
                        Text(msg.content, color = Color.White, fontSize = 15.sp, lineHeight = 20.sp)
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
e (context is ContextWrapper) { if (context is ComponentActivity) return context; context = context.baseContext }
    return null
}
