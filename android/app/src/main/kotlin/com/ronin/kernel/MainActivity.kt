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

data class ChatMessage(
    val id: Long,
    val sender: String,
    var content: String,
    var isThinking: Boolean = false,
    var thoughtContent: String = ""
)

enum class WizardState { MISSING_CORE, IMPORTING, VERIFYING, ACTIVE }

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<ChatMessage>()
    val reasoningLogs = mutableStateListOf<String>()
    
    var showSysInfo by mutableStateOf(false)
    var showReasoning by mutableStateOf(false)
    var lmkPressure by mutableStateOf(0)
    
    var wizardState by mutableStateOf(WizardState.MISSING_CORE)
    var isGemmaReady by mutableStateOf(false)
    
    var showCommandSuggestions by mutableStateOf(false)
    var temperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)

    var offlineMode by mutableStateOf(false)
    var localModelPath by mutableStateOf("")
    var primaryCloudProvider by mutableStateOf("Gemini")
    val cloudProviders = mutableStateListOf<CloudProvider>()
    val discoveredModels = mutableStateListOf<String>()

    // Hardened v3.2 Settings
    var systemPrompt by mutableStateOf("You are Ronin. Access tools via 'CALL: tool_name(\"args\")'. TOOLS: search_memory(query), archive_memory(text). MYANMAR: Always reason [THINK] and REPLY in Myanmar if the user does.")
    var maxTokens by mutableStateOf(768)

    var showAddCloudDialog by mutableStateOf(false)
    var kernelStatus by mutableStateOf("Initializing...")
    var isGenerating by mutableStateOf(false)
}

class MainActivity : ComponentActivity() {
    internal lateinit var nativeEngine: NativeEngine
    private lateinit var sharedPreferences: android.content.SharedPreferences
    private lateinit var fusedLocationClient: FusedLocationProviderClient

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
                    val modelsDir = File(filesDir, "models")
                    if (!modelsDir.exists()) modelsDir.mkdirs()
                    
                    val targetFile = File(modelsDir, fileName)
                    inputStream?.use { input -> 
                        java.io.FileOutputStream(targetFile).use { output -> 
                            input.copyTo(output, bufferSize = 1024 * 1024) 
                        } 
                    }
                    chatViewModel.wizardState = WizardState.VERIFYING
                    nativeEngine.isValidModel(targetFile.absolutePath)
                } catch (e: Exception) { false }
            }
            if (success) {
                scanLocalModels()
                Toast.makeText(this@MainActivity, "Brain Imported Successfully", Toast.LENGTH_SHORT).show()
            } else {
                chatViewModel.wizardState = WizardState.MISSING_CORE
                Toast.makeText(this@MainActivity, "Import Failed", Toast.LENGTH_LONG).show()
            }
        }
    }

    fun scanLocalModels() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val modelsDir = File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()

        val modelFiles = modelsDir.listFiles { file -> 
            !file.isDirectory && file.length() > 1024 && 
            (file.name.endsWith(".litertlm") || file.name.endsWith(".bin"))
        } ?: emptyArray()
        
        val uniquePaths = modelFiles.map { it.absolutePath }.distinct().sorted()
        
        if (chatViewModel.discoveredModels.toList() != uniquePaths) {
            chatViewModel.discoveredModels.clear()
            chatViewModel.discoveredModels.addAll(uniquePaths)
        }
        
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

        // Load saved state
        chatViewModel.systemPrompt = sharedPreferences.getString("system_prompt", chatViewModel.systemPrompt) ?: chatViewModel.systemPrompt
        chatViewModel.maxTokens = sharedPreferences.getInt("max_tokens", 768)

        lifecycleScope.launch(Dispatchers.Main) {
            chatViewModel.kernelStatus = "Booting Engine..."
            NativeEngine.initializeAsync()
            nativeEngine.initialize()
            setupHardwareCallbacks()
            loadCloudProvidersFromDisk()
            
            val lastProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini") ?: "Gemini"
            savePrimaryCloudProvider(lastProvider)
            
            val offline = sharedPreferences.getBoolean("offline_mode", false)
            saveOfflineMode(offline)

            chatViewModel.kernelStatus = "Neural Bridge Active"
            checkAndRequestPermissions()
            scanLocalModels()
            
            val savedModelPath = sharedPreferences.getString("local_model_path", "")
            if (!savedModelPath.isNullOrEmpty() && File(savedModelPath).exists()) hydrateModel(savedModelPath)

            // Start token collector
            launch {
                nativeEngine.inferenceFlow.collect { packet ->
                    if (chatViewModel.messages.isNotEmpty()) {
                        val lastMsg = chatViewModel.messages.last()
                        if (lastMsg.sender == "Ronin") {
                            // Filter Logic: Route tokens based on tag context
                            if (packet.fragment.contains("[THINK]")) {
                                lastMsg.isThinking = true
                            }
                            
                            if (lastMsg.isThinking) {
                                val cleanToken = packet.fragment.replace("[THINK]", "").replace("[/THINK]", "")
                                lastMsg.thoughtContent += cleanToken
                                chatViewModel.reasoningLogs.add(0, cleanToken)
                                if (packet.fragment.contains("[/THINK]")) lastMsg.isThinking = false
                            } else {
                                val cleanToken = packet.fragment.replace("[REPLY]", "").trimStart()
                                lastMsg.content += cleanToken
                            }
                            
                            // Trigger UI update
                            val idx = chatViewModel.messages.indexOf(lastMsg)
                            if (idx != -1) chatViewModel.messages[idx] = lastMsg.copy()
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
        val assetsDir = File(filesDir, "assets")
        if (!assetsDir.exists()) assetsDir.mkdirs()
        try {
            val capFile = File(assetsDir, "capabilities.json")
            if (!capFile.exists()) {
                assets.open("capabilities.json").use { input ->
                    java.io.FileOutputStream(capFile).use { output -> input.copyTo(output) }
                }
            }
        } catch (e: Exception) { Log.e("RoninBoot", "Asset copy failed.") }
    }

    private fun loadCloudProvidersFromDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = File(filesDir, "config")
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
        }
    }

    private fun saveCloudProvidersToDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = File(filesDir, "config")
        if (!configDir.exists()) configDir.mkdirs()
        val providersFile = File(configDir, "providers.json")
        try {
            val array = JSONArray()
            chatViewModel.cloudProviders.forEach { p ->
                val obj = JSONObject().put("name", p.name).put("providerType", p.providerType).put("endpoint", p.endpoint).put("modelId", p.modelId).put("authType", p.authType)
                array.put(obj)
            }
            providersFile.writeText(array.toString(2))
            nativeEngine.updateCloudProvidersSafe(array.toString())
        } catch (e: Exception) {}
    }

    fun hydrateModel(path: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            chatViewModel.kernelStatus = "Hydrating Brain..."
            if (nativeEngine.loadModel(path)) {
                chatViewModel.isGemmaReady = true
                sharedPreferences.edit().putString("local_model_path", path).apply()
                chatViewModel.localModelPath = path
                chatViewModel.kernelStatus = "Kernel Ready"
            } else {
                Toast.makeText(this@MainActivity, "Hydration Failed", Toast.LENGTH_SHORT).show()
                chatViewModel.kernelStatus = "Bridge Active"
            }
        }
    }

    fun deleteLocalModel(path: String) {
        val file = File(path)
        if (file.exists() && file.delete()) {
            Toast.makeText(this, "Model deleted.", Toast.LENGTH_SHORT).show()
            scanLocalModels()
        }
    }

    fun clearModelCache() {
        val cacheDir = File(filesDir, "models/compiled_cache")
        if (cacheDir.exists()) cacheDir.deleteRecursively()
        codeCacheDir.deleteRecursively()
        Toast.makeText(this, "System & Model cache cleared.", Toast.LENGTH_SHORT).show()
    }

    fun savePrimaryCloudProvider(name: String) {
        sharedPreferences.edit().putString("primary_cloud_provider", name).apply()
        nativeEngine.setPrimaryCloudProviderSafe(name)
    }

    fun saveApiKey(provider: String, key: String) {
        sharedPreferences.edit().putString(provider, key).apply()
    }

    fun addCloudProvider(name: String, providerType: String, endpoint: String, modelId: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        if (chatViewModel.cloudProviders.any { it.name == name }) return
        chatViewModel.cloudProviders.add(CloudProvider(name, providerType, endpoint, modelId, "bearer"))
        saveCloudProvidersToDisk()
    }

    fun deleteCloudProvider(name: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        chatViewModel.cloudProviders.removeAll { it.name == name }
        if (chatViewModel.primaryCloudProvider == name) {
            chatViewModel.primaryCloudProvider = if (chatViewModel.cloudProviders.isNotEmpty()) chatViewModel.cloudProviders[0].name else ""
            savePrimaryCloudProvider(chatViewModel.primaryCloudProvider)
        }
        saveCloudProvidersToDisk()
    }

    fun saveOfflineMode(offline: Boolean) {
        sharedPreferences.edit().putBoolean("offline_mode", offline).apply()
        nativeEngine.setOfflineModeSafe(offline)
    }

    fun saveSystemPrompt(prompt: String) {
        sharedPreferences.edit().putString("system_prompt", prompt).apply()
    }

    fun saveMaxTokens(tokens: Int) {
        sharedPreferences.edit().putInt("max_tokens", tokens).apply()
    }

    private fun checkAndRequestPermissions() {
        val permissions = mutableListOf(
            android.Manifest.permission.ACCESS_FINE_LOCATION,
            android.Manifest.permission.ACCESS_COARSE_LOCATION,
            android.Manifest.permission.CAMERA
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        }
        requestPermissionLauncher.launch(permissions.toTypedArray())
    }

    private fun setupHardwareCallbacks() {
        val vm = ViewModelProvider(this)[ChatViewModel::class.java]
        nativeEngine.getSecureApiKeyProvider = { provider -> sharedPreferences.getString(provider, "")?.trim() ?: "" }
        nativeEngine.onRequestHardwareDataCallback = { nodeId -> 
            if (nodeId == 5) {
                try {
                    val location = Tasks.await(fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, CancellationTokenSource().token))
                    location?.let { "${it.latitude}, ${it.longitude}" } ?: "Error: GPS Timeout"
                } catch (e: Exception) { "Error: ${e.message}" }
            } else "Data for node $nodeId"
        }
        nativeEngine.executeHardwareActionCallback = { nodeId, state -> 
            when (nodeId) {
                4 -> {
                    try {
                        val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
                        val cameraId = cameraManager.cameraIdList[0]
                        cameraManager.setTorchMode(cameraId, state)
                        true
                    } catch (e: Exception) { false }
                }
                6 -> {
                    try {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            val intent = Intent(Settings.Panel.ACTION_INTERNET_CONNECTIVITY)
                            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            startActivity(intent)
                        } else {
                            val intent = Intent(Settings.ACTION_WIFI_SETTINGS)
                            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            startActivity(intent)
                        }
                        true
                    } catch (e: Exception) { false }
                }
                7 -> {
                    try {
                        val intent = Intent(Settings.ACTION_BLUETOOTH_SETTINGS)
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        startActivity(intent)
                        true
                    } catch (e: Exception) { false }
                }
                else -> false
            }
        }
        nativeEngine.onSystemTiersUpdateCallback = { temp, used, total ->
            vm.temperature = temp; vm.ramUsedGB = used; vm.ramTotalGB = total
        }
        nativeEngine.onKernelMessageCallback = { msg -> 
            vm.reasoningLogs.add(0, msg)
            if (!vm.showReasoning) vm.showReasoning = true
        }
    }

    override fun onResume() { super.onResume(); scanLocalModels() }
}

@Composable
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current; val scope = rememberCoroutineScope()
    val scaffoldState = rememberScaffoldState()
    var currentInput by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager; val mi = ActivityManager.MemoryInfo()
        withContext(Dispatchers.IO) {
            while (true) {
                try {
                    am.getMemoryInfo(mi)
                    val used = (mi.totalMem - mi.availMem) / 1073741824f
                    val total = mi.totalMem / 1073741824f
                    withContext(Dispatchers.Main) { 
                        chatViewModel.ramUsedGB = used
                        chatViewModel.ramTotalGB = total
                        chatViewModel.lmkPressure = engine.getLMKPressureSafe()
                    }
                    engine.updateSystemHealthSafe(35f, used, total)
                } catch (e: Exception) {}
                delay(5000)
            }
        }
    }

    Scaffold(
        scaffoldState = scaffoldState,
        drawerContent = {
            ModalDrawerSheet(chatViewModel, brainPicker, onSaveOfflineMode)
        },
        topBar = { 
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = { scope.launch { scaffoldState.drawerState.open() } }) { Icon(Icons.Default.Menu, null) }
                },
                title = { 
                    Column {
                        Text("Ronin Kernel", fontWeight = FontWeight.Bold)
                        Text(chatViewModel.kernelStatus, fontSize = 10.sp, color = if (chatViewModel.isGemmaReady) Color.Green else Color.Yellow)
                    }
                }, 
                actions = { 
                    IconButton(onClick = { chatViewModel.showReasoning = !chatViewModel.showReasoning }) { Icon(if (chatViewModel.showReasoning) Icons.Default.Visibility else Icons.Default.VisibilityOff, null) }
                    IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) { Icon(Icons.Default.Info, null) }
                }
            ) 
        }
    ) { padding ->
        Box(modifier = Modifier.padding(padding).fillMaxSize().background(Color(0xFF0F111A))) {
            if (chatViewModel.wizardState != WizardState.ACTIVE) {
                BootstrapWizard(chatViewModel, brainPicker)
            } else {
                Column(modifier = Modifier.fillMaxSize()) {
                    if (chatViewModel.showSysInfo) SystemInfoPanel(chatViewModel)

                    AnimatedVisibility(visible = chatViewModel.showReasoning) {
                        Box(modifier = Modifier.height(180.dp).fillMaxWidth().background(Color(0xFF12141C)).padding(8.dp).border(1.dp, Color.Cyan.copy(alpha = 0.3f), RoundedCornerShape(8.dp))) {
                            LazyColumn(modifier = Modifier.fillMaxSize()) { 
                                items(chatViewModel.reasoningLogs) { 
                                    Text(it, color = Color.Cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(vertical = 2.dp)) 
                                } 
                            }
                        }
                    }

                    Box(modifier = Modifier.weight(1f).fillMaxWidth()) { 
                        LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) { 
                            items(chatViewModel.messages.reversed()) { ChatBubble(it) } 
                        } 

                        if (chatViewModel.showCommandSuggestions) {
                            val suggestions = listOf("/status", "/skills", "/model", "/reset", "/more").filter { it.startsWith(currentInput.lowercase()) }
                            if (suggestions.isNotEmpty()) {
                                Surface(color = Color(0xFF25283D).copy(alpha = 0.95f), modifier = Modifier.align(Alignment.BottomStart).padding(start = 16.dp, end = 16.dp, bottom = 8.dp).fillMaxWidth(0.8f).clip(RoundedCornerShape(12.dp)), elevation = 8.dp) {
                                    LazyColumn(modifier = Modifier.heightIn(max = 200.dp)) {
                                        items(suggestions) { s -> 
                                            TextButton(onClick = { currentInput = "$s "; chatViewModel.showCommandSuggestions = false }, modifier = Modifier.fillMaxWidth()) { 
                                                Text(s, color = Color.White, fontSize = 14.sp) 
                                            } 
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Surface(elevation = 8.dp, color = Color(0xFF1A1C2C)) {
                        Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                            TextField(
                                value = currentInput, 
                                onValueChange = { 
                                    currentInput = it
                                    chatViewModel.showCommandSuggestions = it.startsWith("/")
                                }, 
                                modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)), 
                                colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White), 
                                trailingIcon = { 
                                    IconButton(onClick = { 
                                        if (currentInput.isNotBlank()) { 
                                            val rawInput = currentInput
                                            chatViewModel.messages.add(ChatMessage(System.currentTimeMillis(), "User", rawInput))
                                            currentInput = ""
                                            chatViewModel.isGenerating = true
                                            
                                            scope.launch { 
                                                if (!chatViewModel.isGemmaReady) {
                                                    chatViewModel.reasoningLogs.add("> [SYSTEM] Local Brain missing. Escalating to Cloud Fallback.")
                                                    val apiKey = engine.getSecureApiKeyProvider?.invoke(chatViewModel.primaryCloudProvider) ?: ""
                                                    val res = engine.performCloudInference(rawInput, chatViewModel.primaryCloudProvider, apiKey)
                                                    chatViewModel.messages.add(ChatMessage(System.currentTimeMillis() + 1, "Ronin", res))
                                                } else {
                                                    // Prepare Ronin bubble for streaming
                                                    val roninMsg = ChatMessage(System.currentTimeMillis() + 1, "Ronin", "")
                                                    chatViewModel.messages.add(roninMsg)
                                                    
                                                    val finalResult = engine.processInputAsync(rawInput, chatViewModel.systemPrompt)
                                                    
                                                    // Ensure last sync state if streaming missed something
                                                    if (roninMsg.content.isEmpty()) {
                                                        roninMsg.content = finalResult.substringAfter("[/THINK]").replace("[REPLY]", "").trim()
                                                        val idx = chatViewModel.messages.indexOf(roninMsg)
                                                        if (idx != -1) chatViewModel.messages[idx] = roninMsg.copy()
                                                    }
                                                }
                                                chatViewModel.isGenerating = false
                                            } 
                                        } 
                                    }) { Icon(if (chatViewModel.isGenerating) Icons.Default.HourglassEmpty else Icons.Default.Send, null, tint = Color(0xFF64B5F6)) } 
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun ModalDrawerSheet(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current
    Column(modifier = Modifier.fillMaxSize().background(Color(0xFF1A1C2C)).verticalScroll(rememberScrollState()).padding(16.dp)) {
        Text("Ronin Configuration", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(16.dp))
        
        Text("System Prompt", fontSize = 12.sp, color = Color.Gray)
        TextField(
            value = chatViewModel.systemPrompt,
            onValueChange = { chatViewModel.systemPrompt = it; (context.findActivity() as? MainActivity)?.saveSystemPrompt(it) },
            modifier = Modifier.fillMaxWidth().height(100.dp),
            colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White),
            textStyle = androidx.compose.ui.text.TextStyle(fontSize = 11.sp)
        )
        
        Spacer(Modifier.height(16.dp))
        Text("Max Tokens: ${chatViewModel.maxTokens}", fontSize = 12.sp, color = Color.Gray)
        Slider(
            value = chatViewModel.maxTokens.toFloat(),
            onValueChange = { chatViewModel.maxTokens = it.toInt(); (context.findActivity() as? MainActivity)?.saveMaxTokens(it.toInt()) },
            valueRange = 256f..2048f,
            colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6))
        )
        
        Divider(Modifier.padding(vertical = 16.dp))
        Text("Local Models", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color.White)
        chatViewModel.discoveredModels.forEach { path ->
            val isActive = path == chatViewModel.localModelPath
            Row(verticalAlignment = Alignment.CenterVertically) { 
                RadioButton(selected = isActive, onClick = { (context.findActivity() as? MainActivity)?.hydrateModel(path) }, colors = RadioButtonDefaults.colors(selectedColor = Color.Green))
                Text(File(path).name, modifier = Modifier.weight(1f).clickable { (context.findActivity() as? MainActivity)?.hydrateModel(path) }, color = if (isActive) Color.Green else Color.White, fontSize = 12.sp)
                IconButton(onClick = { (context.findActivity() as? MainActivity)?.deleteLocalModel(path) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray, modifier = Modifier.size(16.dp)) }
            }
        }
        OutlinedButton(onClick = { brainPicker.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) { Text("Import Brain") }
        
        Divider(Modifier.padding(vertical = 16.dp))
        Text("Cloud Profiles", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = Color.White)
        chatViewModel.cloudProviders.forEach { profile ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                RadioButton(selected = profile.name == chatViewModel.primaryCloudProvider, onClick = { (context.findActivity() as? MainActivity)?.savePrimaryCloudProvider(profile.name); chatViewModel.primaryCloudProvider = profile.name })
                Column(modifier = Modifier.weight(1f)) { Text(profile.name, color = Color.White, fontSize = 12.sp); Text(profile.modelId, fontSize = 9.sp, color = Color.Gray) }
            }
        }
        TextButton(onClick = { chatViewModel.showAddCloudDialog = true }) { Text("+ Add Cloud Model", fontSize = 12.sp) }
        
        Spacer(Modifier.weight(1f))
        TextButton(onClick = { (context.findActivity() as? MainActivity)?.clearModelCache() }) { Text("Clear All Cache", color = Color.Red, fontSize = 12.sp) }
    }
}

@Composable
fun BootstrapWizard(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>) {
    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Icon(Icons.Default.AutoAwesome, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(80.dp))
        Spacer(Modifier.height(24.dp))
        Text("Ronin Kernel: Setup", fontSize = 24.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(16.dp))
        Text("No Reasoning Spine detected.\nPlease import a Gemma 4 (.litertlm) model.", fontSize = 14.sp, color = Color.Gray, textAlign = androidx.compose.ui.text.style.TextAlign.Center, modifier = Modifier.padding(horizontal = 48.dp))
        Spacer(Modifier.height(48.dp))
        
        when (chatViewModel.wizardState) {
            WizardState.IMPORTING -> Column(horizontalAlignment = Alignment.CenterHorizontally) { CircularProgressIndicator(color = Color(0xFF64B5F6)); Text("Copying Data...", color = Color.Gray, fontSize = 11.sp) }
            WizardState.VERIFYING -> Column(horizontalAlignment = Alignment.CenterHorizontally) { CircularProgressIndicator(color = Color.Green); Text("Validating Header...", color = Color.Gray, fontSize = 11.sp) }
            else -> {
                Button(onClick = { brainPicker.launch(arrayOf("*/*")) }, colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6)), shape = RoundedCornerShape(24.dp)) {
                    Text("IMPORT MODEL", color = Color.Black, fontWeight = FontWeight.Bold, modifier = Modifier.padding(horizontal = 16.dp))
                }
            }
        }
    }
}

@Composable
fun ChatBubble(msg: ChatMessage) {
    val isUser = msg.sender == "User"
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        if (!isUser) Text("Ronin", fontSize = 10.sp, color = Color.Gray, modifier = Modifier.padding(start = 4.dp, bottom = 2.dp))
        Surface(
            color = if (isUser) Color(0xFF2D3142) else Color(0xFF1E2130), 
            shape = RoundedCornerShape(
                topStart = 16.dp, topEnd = 16.dp, 
                bottomStart = if (isUser) 16.dp else 4.dp, 
                bottomEnd = if (isUser) 4.dp else 16.dp
            ),
            elevation = 2.dp
        ) { 
            SelectionContainer { 
                Text(msg.content, modifier = Modifier.padding(12.dp), color = Color.White, fontSize = 15.sp, lineHeight = 20.sp) 
            } 
        }
    }
}

@Composable
fun SystemInfoPanel(chatViewModel: ChatViewModel) {
    Surface(color = Color(0xFF161922).copy(alpha = 0.8f), modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).clip(RoundedCornerShape(12.dp))) { Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth().padding(12.dp)) { InfoItem("Thermal", "${chatViewModel.temperature}°C", if (chatViewModel.temperature > 40) Color.Red else Color.Green); InfoItem("RAM", "${"%.2f".format(chatViewModel.ramUsedGB)}GB", Color.White); InfoItem("LMK", "${chatViewModel.lmkPressure}%", Color.Cyan) } }
}

@Composable
fun InfoItem(l: String, v: String, c: Color) { Column(horizontalAlignment = Alignment.CenterHorizontally) { Text(l, fontSize = 9.sp, color = Color.Gray); Text(v, fontSize = 13.sp, color = c, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace) } }

@Composable
fun Divider(modifier: Modifier = Modifier) { androidx.compose.material.Divider(color = Color.Gray.copy(alpha = 0.15f), modifier = modifier) }

fun Context.findActivity(): ComponentActivity? {
    var context = this
    while (context is ContextWrapper) {
        if (context is ComponentActivity) return context
        context = context.baseContext
    }
    return null
}
