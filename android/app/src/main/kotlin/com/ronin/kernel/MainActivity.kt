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
import android.util.Log
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

data class CloudProvider(
    val name: String, 
    val providerType: String,
    val endpoint: String,
    val modelId: String,
    val authType: String
)

enum class WizardState { MISSING_CORE, IMPORTING, VERIFYING, ACTIVE }

class ChatViewModel : ViewModel() {
    val messages = mutableStateListOf<String>()
    val reasoningLogs = mutableStateListOf<String>()
    var showSysInfo by mutableStateOf(false)
    var lmkPressure by mutableStateOf(0)
    
    var wizardState by mutableStateOf(WizardState.MISSING_CORE)
    var isGemmaReady by mutableStateOf(false)
    
    var showCommandSuggestions by mutableStateOf(false)
    var temperature by mutableStateOf(0f)
    var ramUsedGB by mutableStateOf(0f)
    var ramTotalGB by mutableStateOf(0f)

    var showSettings by mutableStateOf(false)
    var offlineMode by mutableStateOf(false)
    var isThinkingEnabled by mutableStateOf(false)
    var localModelPath by mutableStateOf("")
    var primaryCloudProvider by mutableStateOf("Gemini")
    val cloudProviders = mutableStateListOf<CloudProvider>()
    val discoveredModels = mutableStateListOf<String>()

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
                    val inputStream = contentResolver.openInputStream(uri)
                    val modelsDir = java.io.File(filesDir, "models")
                    if (!modelsDir.exists()) modelsDir.mkdirs()
                    val fileName = uri.lastPathSegment?.substringAfterLast("/") ?: "imported_model.bin"
                    val targetFile = java.io.File(modelsDir, fileName)
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
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()

        val modelFiles = modelsDir.listFiles { file -> 
            !file.isDirectory && file.length() > 1024 
        } ?: emptyArray()
        
        val uniquePaths = modelFiles.map { it.canonicalPath }.distinct().sorted()
        
        // Use a temp list to avoid re-composition loops
        if (chatViewModel.discoveredModels.toList() != uniquePaths) {
            chatViewModel.discoveredModels.clear()
            chatViewModel.discoveredModels.addAll(uniquePaths)
            Log.d("Ronin_UI", "Models synced: ${uniquePaths.size}")
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
            if (!savedModelPath.isNullOrEmpty()) hydrateModel(savedModelPath)
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
        val assetsDir = java.io.File(filesDir, "assets")
        if (!assetsDir.exists()) assetsDir.mkdirs()
        try {
            val capFile = java.io.File(assetsDir, "capabilities.json")
            if (!capFile.exists()) {
                assets.open("capabilities.json").use { input ->
                    java.io.FileOutputStream(capFile).use { output -> input.copyTo(output) }
                }
            }
        } catch (e: Exception) { Log.e("RoninBoot", "Asset copy failed.") }
    }

    private fun loadCloudProvidersFromDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = java.io.File(filesDir, "config")
        val providersFile = java.io.File(configDir, "providers.json")
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
        val configDir = java.io.File(filesDir, "config")
        if (!configDir.exists()) configDir.mkdirs()
        val providersFile = java.io.File(configDir, "providers.json")
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
        val file = java.io.File(path)
        if (file.exists() && file.delete()) {
            Toast.makeText(this, "Model deleted.", Toast.LENGTH_SHORT).show()
            scanLocalModels()
        }
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
        nativeEngine.getSecureApiKey = { provider -> sharedPreferences.getString(provider, "")?.trim() ?: "" }
        nativeEngine.onRequestHardwareData = { nodeId -> "Hardware Node $nodeId Not Implemented" }
        nativeEngine.executeHardwareAction = { nodeId, state -> false }
        nativeEngine.onSystemTiersUpdate = { temp, used, total ->
            vm.temperature = temp; vm.ramUsedGB = used; vm.ramTotalGB = total
        }
        nativeEngine.onKernelMessage = { msg -> vm.reasoningLogs.add(0, msg) }
    }

    override fun onResume() { super.onResume(); scanLocalModels() }
}

@Composable
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current; val scope = rememberCoroutineScope()
    var currentInput by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager; val mi = ActivityManager.MemoryInfo()
        withContext(Dispatchers.IO) {
            while (true) {
                try {
                    am.getMemoryInfo(mi)
                    val total = mi.totalMem / 1073741824f; val avail = mi.availMem / 1073741824f; val used = total - avail
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
        topBar = { 
            TopAppBar(
                title = { 
                    Column {
                        Text("Ronin Kernel", fontWeight = FontWeight.Bold)
                        Text(chatViewModel.kernelStatus, fontSize = 10.sp, color = if (chatViewModel.isGemmaReady) Color.Green else Color.Yellow)
                    }
                }, 
                actions = { 
                    IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) { Icon(Icons.Default.Info, null) }
                    IconButton(onClick = { chatViewModel.showSettings = true }) { Icon(Icons.Default.Settings, null) } 
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

                    Box(modifier = Modifier.weight(0.3f).fillMaxWidth().background(Color.Black.copy(alpha = 0.3f)).padding(8.dp)) {
                        LazyColumn(modifier = Modifier.fillMaxSize()) { items(chatViewModel.reasoningLogs) { Text(it, color = Color.Gray, fontSize = 11.sp, fontFamily = FontFamily.Monospace) } }
                    }

                    Box(modifier = Modifier.weight(0.7f).fillMaxWidth()) { 
                        LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) { items(chatViewModel.messages.reversed()) { ChatBubble(it) } } 

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
                                            chatViewModel.messages.add("User: $rawInput")
                                            currentInput = ""
                                            chatViewModel.isGenerating = true
                                            
                                            scope.launch { 
                                                if (!chatViewModel.isGemmaReady) {
                                                    chatViewModel.reasoningLogs.add("> [SYSTEM] Local Brain missing. Escalating to Cloud Fallback.")
                                                    val apiKey = engine.getSecureApiKey?.invoke(chatViewModel.primaryCloudProvider) ?: ""
                                                    val res = engine.performCloudInference(rawInput, chatViewModel.primaryCloudProvider, apiKey)
                                                    chatViewModel.messages.add("Ronin: $res")
                                                } else {
                                                    val res = engine.processInputAsync(rawInput)
                                                    chatViewModel.messages.add("Ronin: $res")
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
    if (chatViewModel.showSettings) SettingsDialog(chatViewModel, brainPicker, onSaveOfflineMode, { (context.findActivity() as? MainActivity)?.deleteLocalModel(it) }, { (context.findActivity() as? MainActivity)?.hydrateModel(it) })
}

@Composable
fun BootstrapWizard(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>) {
    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Icon(Icons.Default.Settings, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(64.dp))
        Spacer(Modifier.height(24.dp))
        Text("Ronin Kernel: Setup Mode", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(16.dp))
        Text("Single Gemma 4 Architecture: Import a '.litertlm' or '.bin' model to activate reasoning.", fontSize = 14.sp, color = Color.Gray, textAlign = androidx.compose.ui.text.style.TextAlign.Center, modifier = Modifier.padding(horizontal = 32.dp))
        Spacer(Modifier.height(32.dp))
        
        when (chatViewModel.wizardState) {
            WizardState.IMPORTING -> CircularProgressIndicator(color = Color(0xFF64B5F6))
            WizardState.VERIFYING -> CircularProgressIndicator(color = Color.Green)
            else -> {
                Button(onClick = { brainPicker.launch(arrayOf("*/*")) }, colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6))) {
                    Text("IMPORT REASONING BRAIN", color = Color.Black)
                }
            }
        }
    }
}

@Composable
fun ChatBubble(m: String) {
    val isUser = m.startsWith("User:"); val content = m.substringAfter(": ")
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        Surface(color = if (isUser) Color(0xFF2D3142) else Color(0xFF64B5F6).copy(alpha = 0.1f), shape = RoundedCornerShape(12.dp)) { 
            SelectionContainer { Text(content, modifier = Modifier.padding(12.dp), color = Color.White, fontSize = 14.sp) }
        }
    }
}

@Composable
fun SettingsDialog(chatViewModel: ChatViewModel, brainPicker: ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit, onDeleteModel: (String) -> Unit, onSelectModel: (String) -> Unit) {
    val context = LocalContext.current
    AlertDialog(onDismissRequest = { chatViewModel.showSettings = false }, title = { Text("Ronin Configuration") }, text = {
        Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
            Text("Reasoning Brains (Internal)", fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
            chatViewModel.discoveredModels.forEach { path ->
                val file = java.io.File(path)
                val isActive = path == chatViewModel.localModelPath
                Row(verticalAlignment = Alignment.CenterVertically) { 
                    RadioButton(selected = isActive, onClick = { onSelectModel(path) })
                    Text(file.name, modifier = Modifier.weight(1f), color = if (isActive) Color.Green else Color.White)
                    IconButton(onClick = { onDeleteModel(path) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray) }
                }
            }
            OutlinedButton(onClick = { brainPicker.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) { Text("Import Reasoning Brain") }
            Spacer(Modifier.height(16.dp)); Divider()
            Row(verticalAlignment = Alignment.CenterVertically) { 
                Text("Cloud Reasoning Models", fontWeight = FontWeight.SemiBold, fontSize = 14.sp, modifier = Modifier.weight(1f)); 
                IconButton(onClick = { chatViewModel.showAddCloudDialog = true }) { Icon(Icons.Default.Add, null, tint = Color(0xFF64B5F6)) } 
            }
            chatViewModel.cloudProviders.forEach { profile ->
                val isActive = profile.name == chatViewModel.primaryCloudProvider && !chatViewModel.offlineMode
                Row(verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(selected = profile.name == chatViewModel.primaryCloudProvider, onClick = { (context.findActivity() as? MainActivity)?.savePrimaryCloudProvider(profile.name); chatViewModel.primaryCloudProvider = profile.name })
                    Column(modifier = Modifier.weight(1f)) { Text(profile.name, color = if (isActive) Color.Green else Color.White); Text(profile.modelId, fontSize = 10.sp, color = Color.Gray) }
                    IconButton(onClick = { (context.findActivity() as? MainActivity)?.deleteCloudProvider(profile.name) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray, modifier = Modifier.size(18.dp)) }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) { Text("Offline-Only", modifier = Modifier.weight(1f)); Switch(checked = chatViewModel.offlineMode, onCheckedChange = { chatViewModel.offlineMode = it; onSaveOfflineMode(it) }) }
        }
    }, confirmButton = { TextButton(onClick = { chatViewModel.showSettings = false }) { Text("CLOSE") } })
    if (chatViewModel.showAddCloudDialog) AddCloudProviderDialog(onDismiss = { chatViewModel.showAddCloudDialog = false }, onAdd = { n, t, e, m, k -> (context.findActivity() as? MainActivity)?.saveApiKey(n, k); (context.findActivity() as? MainActivity)?.addCloudProvider(n, t, e, m); chatViewModel.showAddCloudDialog = false })
}

@Composable
fun AddCloudProviderDialog(onDismiss: () -> Unit, onAdd: (String, String, String, String, String) -> Unit) {
    val context = LocalContext.current; val engine = (context.findActivity() as? MainActivity)?.nativeEngine; val scope = rememberCoroutineScope()
    var selectedTemplate by remember { mutableStateOf("Gemini") }; var expanded by remember { mutableStateOf(false) }
    var apiKey by remember { mutableStateOf("") }; var isFetching by remember { mutableStateOf(false) }
    var fetchedModels by remember { mutableStateOf<List<JSONObject>>(emptyList()) }; var selectedModelId by remember { mutableStateOf("") }
    var customEndpoint by remember { mutableStateOf("") }; var customProfileName by remember { mutableStateOf("") }
    var fetchError by remember { mutableStateOf<String?>(null) }

    AlertDialog(onDismissRequest = onDismiss, title = { Text("Add Cloud Profile") }, text = {
        Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
            Box {
                OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) { Text(selectedTemplate); Icon(Icons.Default.ArrowDropDown, null) }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    listOf("Gemini", "OpenRouter", "Custom").forEach { t -> DropdownMenuItem(onClick = { selectedTemplate = t; expanded = false; selectedModelId = ""; apiKey = ""; fetchedModels = emptyList(); fetchError = null }) { Text(t) } }
                }
            }
            Spacer(Modifier.height(8.dp))
            if (selectedTemplate == "Custom") {
                TextField(value = customProfileName, onValueChange = { customProfileName = it }, label = { Text("Profile Name") })
                TextField(value = customEndpoint, onValueChange = { customEndpoint = it }, label = { Text("Endpoint URL") })
                TextField(value = selectedModelId, onValueChange = { selectedModelId = it }, label = { Text("Model ID") })
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                TextField(value = apiKey, onValueChange = { apiKey = it }, modifier = Modifier.weight(1f), label = { Text("API Key") }, visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation())
                if (selectedTemplate != "Custom") {
                    IconButton(onClick = { 
                        if (apiKey.isNotBlank()) { 
                            isFetching = true; fetchError = null; scope.launch { val res = engine?.fetchAvailableModels(apiKey, selectedTemplate); fetchedModels = res?.models ?: emptyList(); fetchError = res?.error; isFetching = false } 
                        } 
                    }) { Icon(Icons.Default.Refresh, null, tint = Color(0xFF64B5F6)) }
                }
            }
            if (isFetching) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            if (fetchedModels.isNotEmpty()) {
                fetchedModels.forEach { obj -> 
                    val mId = if (selectedTemplate == "Gemini") obj.getString("name").substringAfterLast("/") else obj.getString("id")
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.clickable { selectedModelId = mId }) { RadioButton(selected = selectedModelId == mId, onClick = { selectedModelId = mId }); Text(mId, fontSize = 11.sp) } 
                }
            }
        }
    }, confirmButton = { Button(onClick = {
        val endpoint = when(selectedTemplate) { 
            "Gemini" -> "https://generativelanguage.googleapis.com/v1beta/models/$selectedModelId:generateContent"
            "OpenRouter" -> "https://openrouter.ai/api/v1/chat/completions"
            else -> customEndpoint 
        }
        val name = if (selectedTemplate == "Custom") customProfileName else selectedModelId
        if (name.isNotBlank() && selectedModelId.isNotBlank() && apiKey.isNotBlank()) onAdd(name, selectedTemplate, endpoint, selectedModelId, apiKey)
    }) { Text("SAVE") } })
}

@Composable
fun SystemInfoPanel(chatViewModel: ChatViewModel) {
    Surface(color = Color(0xFF161922), modifier = Modifier.fillMaxWidth().padding(16.dp)) { Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) { InfoItem("Thermal", "${chatViewModel.temperature} deg C", if (chatViewModel.temperature > 40) Color.Red else Color.Green); InfoItem("RAM", "${"%.2f".format(chatViewModel.ramUsedGB)}GB", Color.White); InfoItem("LMK", "${chatViewModel.lmkPressure}%", Color.Cyan) } }
}

@Composable
fun InfoItem(l: String, v: String, c: Color) { Column { Text(l, fontSize = 10.sp, color = Color.Gray); Text(v, fontSize = 14.sp, color = c, fontWeight = FontWeight.Bold) } }

@Composable
fun Divider(color: Color = Color.Gray.copy(alpha = 0.2f), modifier: Modifier = Modifier) { androidx.compose.material.Divider(color = color, modifier = modifier) }

fun Context.findActivity(): ComponentActivity? {
    var context = this
    while (context is ContextWrapper) {
        if (context is ComponentActivity) return context
        context = context.baseContext
    }
    return null
}
