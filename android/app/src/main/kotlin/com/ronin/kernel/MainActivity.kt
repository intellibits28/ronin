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
    val name: String, // Profile Name
    val providerType: String, // Gemini, OpenRouter, Custom
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
    
    // Command Intelligence
    var showCommandSuggestions by mutableStateOf(false)
    val commandSuggestions = mutableStateListOf<String>()

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
    var primaryCloudProvider by mutableStateOf("Gemini-Flash")
    val cloudProviders = mutableStateListOf<CloudProvider>()
    val discoveredModels = mutableStateListOf<String>()

    // Cloud Intelligence
    var showApiKeyDialog by mutableStateOf(false)
    var showAddCloudDialog by mutableStateOf(false)
    var pendingProviderName by mutableStateOf("")
    var pendingProviderType by mutableStateOf("Gemini")
}

class MainActivity : ComponentActivity() {
    internal lateinit var nativeEngine: NativeEngine
    
    private lateinit var fusedLocationClient: FusedLocationProviderClient
    private lateinit var sharedPreferences: android.content.SharedPreferences
    private var lastPermissionState = false

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
                            input.copyTo(output, bufferSize = 1024 * 1024)
                        }
                    }
                    true
                } catch (e: Exception) {
                    Log.e("RoninImport", "Model import failed: ${e.message}")
                    false
                }
            }
            if (success) scanLocalModels()
        }
    }

    private fun scanLocalModels() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()
        val models = modelsDir.listFiles { file -> 
            file.name != "model.onnx" && !file.isDirectory
        }?.map { it.absolutePath }?.distinct() ?: emptyList()
        chatViewModel.discoveredModels.clear()
        chatViewModel.discoveredModels.addAll(models)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        nativeEngine = NativeEngine(this)
        val masterKey = MasterKey.Builder(this).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build()
        sharedPreferences = EncryptedSharedPreferences.create(this, "ronin_secure_prefs", masterKey, EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV, EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM)
        fusedLocationClient = LocationServices.getFusedLocationProviderClient(this)
        copyAssetsToFilesDir(filesDir)

        lifecycleScope.launch(Dispatchers.Main) {
            NativeEngine.initializeAsync()
            nativeEngine.initialize()
            registerComponentCallbacks(nativeEngine)
            setupHardwareCallbacks()
            loadCloudProvidersFromDisk()
            val lastProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini-Flash") ?: "Gemini-Flash"
            nativeEngine.setPrimaryCloudProviderSafe(lastProvider)
            val offline = sharedPreferences.getBoolean("offline_mode", false)
            nativeEngine.setOfflineModeSafe(offline)

            lastPermissionState = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) Environment.isExternalStorageManager() else checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED
            if (lastPermissionState) {
                scanLocalModels()
                val savedModelPath = sharedPreferences.getString("local_model_path", "")
                if (!savedModelPath.isNullOrEmpty()) hydrateModel(savedModelPath)
            }
        }

        setContent {
            val chatViewModel: ChatViewModel = viewModel()
            LaunchedEffect(Unit) {
                chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                chatViewModel.offlineMode = sharedPreferences.getBoolean("offline_mode", false)
                chatViewModel.primaryCloudProvider = sharedPreferences.getString("primary_cloud_provider", "Gemini-Flash") ?: "Gemini-Flash"
                while(true) {
                    val loaded = nativeEngine.isLoaded()
                    if (chatViewModel.isKernelHydrated != loaded) {
                        chatViewModel.isKernelHydrated = loaded
                        chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
                    }
                    delay(3000)
                }
            }
            RoninChatUI(engine = nativeEngine, chatViewModel = chatViewModel, modelPicker = modelPickerLauncher, onSaveOfflineMode = { saveOfflineMode(it) })
        }
    }

    private fun copyAssetsToFilesDir(filesDir: java.io.File) {
        val modelsDir = java.io.File(filesDir, "models")
        if (!modelsDir.exists()) modelsDir.mkdirs()
        try {
            val routerFile = java.io.File(modelsDir, "model.onnx")
            if (!routerFile.exists()) assets.open("models/model.onnx").use { input -> java.io.FileOutputStream(routerFile).use { output -> input.copyTo(output) } }
        } catch (e: Exception) {}
    }

    private fun loadCloudProvidersFromDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = java.io.File("/storage/emulated/0/Ronin/config")
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
            } catch (e: Exception) { Log.e("RoninBoot", "Failed to parse providers: ${e.message}") }
        } else {
            chatViewModel.cloudProviders.add(CloudProvider("Gemini-Flash", "Gemini", "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent", "gemini-1.5-flash", "key"))
            saveCloudProvidersToDisk()
        }
    }

    private fun saveCloudProvidersToDisk() {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val configDir = java.io.File("/storage/emulated/0/Ronin/config")
        if (!configDir.exists()) configDir.mkdirs()
        val providersFile = java.io.File(configDir, "providers.json")
        try {
            val array = JSONArray()
            chatViewModel.cloudProviders.forEach { p ->
                val obj = JSONObject()
                obj.put("name", p.name)
                obj.put("providerType", p.providerType)
                obj.put("endpoint", p.endpoint)
                obj.put("modelId", p.modelId)
                obj.put("authType", p.authType)
                array.put(obj)
            }
            providersFile.writeText(array.toString(2))
            nativeEngine.updateCloudProvidersSafe(array.toString())
        } catch (e: Exception) { Log.e("RoninUI", "Failed to save: ${e.message}") }
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
            chatViewModel.primaryCloudProvider = if (chatViewModel.cloudProviders.isNotEmpty()) chatViewModel.cloudProviders[0].name else "Gemini-Flash"
            savePrimaryCloudProvider(chatViewModel.primaryCloudProvider)
        }
        saveCloudProvidersToDisk()
    }

    fun updateCloudProvider(name: String, endpoint: String?, modelId: String?) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        val index = chatViewModel.cloudProviders.indexOfFirst { it.name == name }
        if (index != -1) {
            val old = chatViewModel.cloudProviders[index]
            chatViewModel.cloudProviders[index] = old.copy(endpoint = endpoint ?: old.endpoint, modelId = modelId ?: old.modelId)
            saveCloudProvidersToDisk()
        }
    }

    private fun setupHardwareCallbacks() {
        nativeEngine.getSecureApiKey = { provider -> sharedPreferences.getString(provider, "")?.trim() ?: "" }
        nativeEngine.onRequestHardwareData = { nodeId -> if (nodeId == 5) "GPS_MOCK" else "Error" }
        nativeEngine.executeHardwareAction = { nodeId, state -> 
            if (nodeId == 1) { nativeEngine.setSafeMode(!state); true } else false
        }
        nativeEngine.onSystemTiersUpdate = { temp, used, total ->
            val vm = ViewModelProvider(this)[ChatViewModel::class.java]
            vm.temperature = temp; vm.ramUsedGB = used; vm.ramTotalGB = total
        }
        nativeEngine.onKernelMessage = { msg -> ViewModelProvider(this)[ChatViewModel::class.java].reasoningLogs.add(0, msg) }
    }

    fun hydrateModel(path: String) {
        val chatViewModel = ViewModelProvider(this)[ChatViewModel::class.java]
        lifecycleScope.launch {
            if (nativeEngine.loadModel(path)) {
                chatViewModel.isKernelHydrated = true
                sharedPreferences.edit().putString("local_model_path", path).apply()
                chatViewModel.localModelPath = nativeEngine.getActiveModelPath()
            }
        }
    }

    private fun saveOfflineMode(offline: Boolean) {
        sharedPreferences.edit().putBoolean("offline_mode", offline).apply()
        nativeEngine.setOfflineModeSafe(offline)
    }

    fun getApiKey(provider: String): String = sharedPreferences.getString(provider, "") ?: ""
    fun saveApiKey(provider: String, key: String) = sharedPreferences.edit().putString(provider, key).apply()
    fun savePrimaryCloudProvider(provider: String) {
        sharedPreferences.edit().putString("primary_cloud_provider", provider).apply()
        nativeEngine.setPrimaryCloudProviderSafe(provider)
    }

    override fun onResume() {
        super.onResume()
        val perm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) Environment.isExternalStorageManager() else checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED
        if (perm && !lastPermissionState) scanLocalModels()
        lastPermissionState = perm
    }
}

@Composable
fun RoninChatUI(engine: NativeEngine, chatViewModel: ChatViewModel, modelPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit) {
    val context = LocalContext.current
    val scaffoldState = rememberScaffoldState()
    val scope = rememberCoroutineScope()
    var currentInput by remember { mutableStateOf("") }

    Scaffold(
        scaffoldState = scaffoldState,
        topBar = { TopAppBar(title = { Text("Ronin Kernel", fontWeight = FontWeight.Bold) }, actions = {
            IconButton(onClick = { chatViewModel.showSysInfo = !chatViewModel.showSysInfo }) { Icon(Icons.Default.BarChart, null) }
            IconButton(onClick = { chatViewModel.showSettings = true }) { Icon(Icons.Default.Settings, null) }
        }) }
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize().background(Color(0xFF0F111A))) {
            if (chatViewModel.showSysInfo) SystemInfoPanel(chatViewModel)
            Box(modifier = Modifier.weight(0.3f).fillMaxWidth().background(Color.Black.copy(alpha = 0.3f)).padding(8.dp)) {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    items(chatViewModel.reasoningLogs) { Text(it, color = Color.Gray, fontSize = 11.sp, fontFamily = FontFamily.Monospace) }
                }
            }
            Box(modifier = Modifier.weight(0.7f).fillMaxWidth()) {
                LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp), reverseLayout = true) {
                    items(chatViewModel.messages.reversed()) { ChatBubble(it) }
                }
            }
            Surface(elevation = 8.dp, color = Color(0xFF1A1C2C)) {
                Row(modifier = Modifier.fillMaxWidth().padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                    TextField(value = currentInput, onValueChange = { currentInput = it }, modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)), colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White), trailingIcon = {
                        IconButton(onClick = {
                            if (currentInput.isNotBlank()) {
                                val input = currentInput; chatViewModel.messages.add("User: $input"); currentInput = ""
                                scope.launch { val res = engine.processInputAsync(input); chatViewModel.messages.add("Ronin: $res") }
                            }
                        }) { Icon(Icons.Default.Send, null, tint = Color(0xFF64B5F6)) }
                    })
                }
            }
        }
    }

    if (chatViewModel.showSettings) SettingsDialog(chatViewModel, modelPicker, onSaveOfflineMode, { (context as MainActivity).deleteModel(it) }, { (context as MainActivity).hydrateModel(it) })
}

@Composable
fun SystemInfoPanel(chatViewModel: ChatViewModel) {
    Surface(color = Color(0xFF161922), modifier = Modifier.fillMaxWidth().padding(16.dp)) {
        Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) {
            InfoItem("Thermal", "${chatViewModel.temperature}°C", if (chatViewModel.temperature > 40) Color.Red else Color.Green)
            InfoItem("RAM", "${"%.2f".format(chatViewModel.ramUsedGB)}GB", Color.White)
            InfoItem("Stability", "${(chatViewModel.stability * 100).toInt()}%", Color.Cyan)
        }
    }
}

@Composable
fun InfoItem(l: String, v: String, c: Color) { Column { Text(l, fontSize = 10.sp, color = Color.Gray); Text(v, fontSize = 14.sp, color = c, fontWeight = FontWeight.Bold) } }

@Composable
fun ChatBubble(m: String) {
    val isUser = m.startsWith("User:")
    val content = m.substringAfter(": ")
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalAlignment = if (isUser) Alignment.End else Alignment.Start) {
        Surface(color = if (isUser) Color(0xFF2D3142) else Color(0xFF64B5F6).copy(alpha = 0.1f), shape = RoundedCornerShape(12.dp)) {
            Text(content, modifier = Modifier.padding(12.dp), color = Color.White, fontSize = 14.sp)
        }
    }
}

@Composable
fun SettingsDialog(chatViewModel: ChatViewModel, modelPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>, onSaveOfflineMode: (Boolean) -> Unit, onDeleteModel: (String) -> Unit, onSelectModel: (String) -> Unit) {
    val context = LocalContext.current
    AlertDialog(
        onDismissRequest = { chatViewModel.showSettings = false },
        title = { Text("Ronin Configuration", fontWeight = FontWeight.Bold) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text("Reasoning Brains (Internal)", fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                chatViewModel.discoveredModels.forEach { path ->
                    val filename = java.io.File(path).name
                    val isActive = path == chatViewModel.localModelPath && chatViewModel.isKernelHydrated
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = path == chatViewModel.localModelPath, onClick = { onSelectModel(path) }, colors = androidx.compose.material.RadioButtonDefaults.colors(selectedColor = if (isActive) Color.Green else Color(0xFF64B5F6)))
                        Text(filename, modifier = Modifier.weight(1f), color = if (isActive) Color.Green else Color.White)
                        IconButton(onClick = { onDeleteModel(path) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray) }
                    }
                }
                OutlinedButton(onClick = { modelPicker.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) { Text("Import Model") }
                
                Spacer(Modifier.height(16.dp)); Divider()
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Cloud Reasoning Models", fontWeight = FontWeight.SemiBold, fontSize = 14.sp, modifier = Modifier.weight(1f))
                    IconButton(onClick = { chatViewModel.showAddCloudDialog = true }) { Icon(Icons.Default.Add, null, tint = Color(0xFF64B5F6)) }
                }
                chatViewModel.cloudProviders.forEach { profile ->
                    val isActive = profile.name == chatViewModel.primaryCloudProvider && !chatViewModel.offlineMode
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = profile.name == chatViewModel.primaryCloudProvider, onClick = { (context as MainActivity).savePrimaryCloudProvider(profile.name); chatViewModel.primaryCloudProvider = profile.name }, colors = androidx.compose.material.RadioButtonDefaults.colors(selectedColor = if (isActive) Color.Green else Color(0xFF64B5F6)))
                        Column(modifier = Modifier.weight(1f)) { Text(profile.name, color = if (isActive) Color.Green else Color.White); Text(profile.modelId, fontSize = 10.sp, color = Color.Gray) }
                        IconButton(onClick = { chatViewModel.pendingProviderName = profile.name; chatViewModel.pendingProviderType = profile.providerType; chatViewModel.showApiKeyDialog = true }) { Icon(Icons.Default.Edit, null, tint = Color.Gray, modifier = Modifier.size(18.dp)) }
                        if (profile.name != "Gemini-Flash") IconButton(onClick = { (context as MainActivity).deleteCloudProvider(profile.name) }) { Icon(Icons.Default.Delete, null, tint = Color.Gray, modifier = Modifier.size(18.dp)) }
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Offline-Only", modifier = Modifier.weight(1f))
                    Switch(checked = chatViewModel.offlineMode, onCheckedChange = { chatViewModel.offlineMode = it; onSaveOfflineMode(it) })
                }
            }
        },
        confirmButton = { TextButton(onClick = { chatViewModel.showSettings = false }) { Text("CLOSE") } }
    )
    if (chatViewModel.showApiKeyDialog) ApiKeyDialog(chatViewModel.pendingProviderName, chatViewModel.pendingProviderType, (context as MainActivity).nativeEngine, { chatViewModel.showApiKeyDialog = false }, { (context as MainActivity).saveApiKey(chatViewModel.pendingProviderName, it); chatViewModel.showApiKeyDialog = false }, { (context as MainActivity).updateCloudProvider(chatViewModel.pendingProviderName, null, it) })
    if (chatViewModel.showAddCloudDialog) AddCloudProviderDialog({ chatViewModel.showAddCloudDialog = false }, { n, t, e, m -> (context as MainActivity).addCloudProvider(n, t, e, m); chatViewModel.showAddCloudDialog = false })
}

@Composable
fun AddCloudProviderDialog(onDismiss: () -> Unit, onAdd: (String, String, String, String) -> Unit) {
    var selectedTemplate by remember { mutableStateOf("Gemini") }; var expanded by remember { mutableStateOf(false) }
    var profileName by remember { mutableStateOf("") }; var endpoint by remember { mutableStateOf(""); var modelId by remember { mutableStateOf("") }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Add Cloud Profile", fontWeight = FontWeight.Bold) }, text = {
        Column {
            Box {
                OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) { Text(selectedTemplate); Icon(Icons.Default.ArrowDropDown, null) }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    listOf("Gemini", "OpenRouter", "Custom").forEach { t -> DropdownMenuItem(onClick = { selectedTemplate = t; expanded = false; if (t == "Gemini") { endpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent"; modelId = "gemini-1.5-flash" } else if (t == "OpenRouter") { endpoint = "https://openrouter.ai/api/v1/chat/completions"; modelId = "meta-llama/llama-3.1-8b-instruct" } }) { Text(t) } }
                }
            }
            TextField(value = profileName, onValueChange = { profileName = it }, label = { Text("Profile Name") })
            if (selectedTemplate == "Custom") TextField(value = endpoint, onValueChange = { endpoint = it }, label = { Text("Endpoint URL") })
            TextField(value = modelId, onValueChange = { modelId = it }, label = { Text("Model ID") })
        }
    }, confirmButton = { Button(onClick = { if (profileName.isNotBlank()) onAdd(profileName, selectedTemplate, endpoint, modelId) }) { Text("VERIFY & SAVE") } })
}

@Composable
fun ApiKeyDialog(provider: String, type: String, engine: NativeEngine, onDismiss: () -> Unit, onSave: (String) -> Unit, onModelSelected: (String) -> Unit) {
    var key by remember { mutableStateOf("") }; var isFetching by remember { mutableStateOf(false) }; var fetchedModels by remember { mutableStateOf<List<String>>(emptyList()) }; var showModelPicker by remember { mutableStateOf(false) }; val scope = rememberCoroutineScope()
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Configure $provider", fontWeight = FontWeight.Bold) }, text = {
        Column {
            TextField(value = key, onValueChange = { key = it }, modifier = Modifier.fillMaxWidth(), placeholder = { Text("Paste API Key here...") }, visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation())
            if (isFetching) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }, confirmButton = { Row {
        if (type == "Gemini" || type == "OpenRouter") TextButton(onClick = { if (key.isNotBlank()) { isFetching = true; scope.launch { val models = engine.fetchAvailableModels(key, type); fetchedModels = if (type == "Gemini") models.map { it.getString("name").substringAfterLast("/") } else models.map { it.getString("id") }; isFetching = false; if (fetchedModels.isNotEmpty()) showModelPicker = true } } }) { Text("FETCH MODELS") }
        Button(onClick = { onSave(key) }) { Text("VERIFY & SAVE") }
    } })
    if (showModelPicker) AlertDialog(onDismissRequest = { showModelPicker = false }, title = { Text("Select Model") }, text = { LazyColumn { items(fetchedModels) { m -> TextButton(onClick = { onModelSelected(m); showModelPicker = false }, modifier = Modifier.fillMaxWidth()) { Text(m, modifier = Modifier.fillMaxWidth(), textAlign = androidx.compose.ui.text.style.TextAlign.Start) } } } }, confirmButton = { TextButton(onClick = { showModelPicker = false }) { Text("CLOSE") } })
}

@Composable
fun Divider(color: Color = Color.Gray.copy(alpha = 0.2f), modifier: Modifier = Modifier) { androidx.compose.material.Divider(color = color, modifier = modifier) }
