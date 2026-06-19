#include "ronin_jni.h"
#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "ronin_kernel.hpp"
#include "ronin_log.h"
#include "jni_utils.h"
#include "jni_gateway.h"
#include "intent_engine.h"
#include "failure_telemetry_bus.h"
#include "execution_checkpoint_store.h"
#include "runtime_healing_controller.h"
#include "adaptive_budget_controller.h"
#include "session_manager.h"
#include "agent_scheduler.h"
#include "graph_executor.h"
#include "long_term_memory.h"
#include "memory_manager.h"
#include "capabilities/hardware_bridge.h"
#include "dsp/resonance_analyzer.h"
#include "jni/ronin_jni_context.h"

#define TAG "RoninKernel_JNI"

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Reasoning;
using namespace Ronin::Kernel::Capability;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::JNI;
using namespace Ronin::Kernel::DSP;

// --- Global Contexts ---
static std::shared_ptr<RoninKernel> g_kernel = nullptr;
static std::shared_ptr<IntentEngine> g_intent_engine = nullptr;
std::shared_ptr<LongTermMemory> g_ltm = nullptr;
static std::shared_ptr<ResonanceAnalyzer> g_resonance_analyzer = nullptr;
static std::unique_ptr<GraphStorage> g_graph_storage = nullptr;
static std::unique_ptr<CapabilityGraph> g_cap_graph = nullptr;
std::unique_ptr<GraphExecutor> g_graph_executor = nullptr;
static std::unique_ptr<MemoryManager> g_memory_manager = nullptr;
jobject g_instance = nullptr;
JavaVM* g_vm = nullptr;

struct LLMContext {
    Model::InferenceEngine* engine = nullptr;
};
static LLMContext g_llm_context;

// v1.6 World State
static WorldState g_world_state;

// --- Helper: exec_handler to bridge Kernel to JNI ---
static Result exec_handler(uint32_t nodeId, const CognitiveState& state) {
    if (g_intent_engine) {
        std::string param = "";
        // Extract the most relevant parameter based on common naming from current_plan parameters
        if (state.current_plan.parameters.count("query")) param = state.current_plan.parameters.at("query");
        else if (state.current_plan.parameters.count("value")) param = state.current_plan.parameters.at("value");
        else if (state.current_plan.parameters.count("entity")) param = state.current_plan.parameters.at("entity");
        else if (state.current_plan.parameters.count("time")) param = state.current_plan.parameters.at("time");
        
        g_intent_engine->executeSkill(nodeId, param); 
        return {true, 200};
    }
    return {false, 500};
}

// --- JNI Implementation ---

extern "C" {

JNIEXPORT void JNICALL native_initializeKernel(JNIEnv *env, jobject thiz, jstring filesDir, jstring libDir, jboolean isWorker) {
    std::string base_path = ConvertJStringToString(env, filesDir);
    std::string native_lib_path = ConvertJStringToString(env, libDir);
    if (!isWorker) {
        if (g_instance) {
            env->DeleteGlobalRef(g_instance);
            g_instance = nullptr;
        }
        g_instance = env->NewGlobalRef(thiz);
        g_ltm = std::make_shared<LongTermMemory>(base_path + "/ronin_cognitive.db");
        Execution::FailureTelemetryBus::getInstance().setMemory(g_ltm.get());
        Execution::ExecutionCheckpointStore::getInstance().initialize(g_ltm->getDatabase());
        Execution::RuntimeHealingController::getInstance().initialize(&AgentScheduler::getInstance());
        g_resonance_analyzer = std::make_shared<ResonanceAnalyzer>(1024);
        g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
        g_cap_graph = std::make_unique<CapabilityGraph>();
        g_graph_storage->loadGraph(*g_cap_graph);
        
        // v12.20: Ensure Core Nodes exist in graph
        if (!g_cap_graph->getNodeByID("SENSOR")) g_cap_graph->addNode(3, "SENSOR");
        if (!g_cap_graph->getNodeByID("FILES")) g_cap_graph->addNode(6, "FILES");
        if (!g_cap_graph->getNodeByID("LOCATION")) g_cap_graph->addNode(1, "LOCATION");
        if (!g_cap_graph->getNodeByID("CALENDAR")) g_cap_graph->addNode(12, "CALENDAR");
        
        g_graph_executor = std::make_unique<GraphExecutor>(*g_cap_graph, *g_graph_storage, g_ltm.get());
        AgentScheduler::getInstance().setExecutor(g_graph_executor.get());
        g_memory_manager = std::make_unique<MemoryManager>(2048);
        g_memory_manager->setLongTermMemory(g_ltm.get());
        g_intent_engine = std::make_shared<IntentEngine>(g_ltm.get());
        g_intent_engine->setMemoryManager(g_memory_manager.get()); 
        if (g_graph_executor) g_intent_engine->setBeliefState(&g_graph_executor->getBeliefState());
        
        auto engine = std::make_unique<Model::InferenceEngine>("hybrid_mode");
        engine->setLibPath(native_lib_path);
        engine->setBasePath(base_path);
        g_llm_context.engine = engine.get();
        g_intent_engine->setInferenceEngine(std::move(engine));
        if (g_graph_executor) g_graph_executor->getReflectionEngine().setInferenceEngine(g_llm_context.engine);
        
        HardwareBridge::initialize(g_vm, g_instance);
        HandlerRegistry registry;
        registry.intentProcessor = [](const Input &input) -> CognitiveIntent {
            if (g_intent_engine) return g_intent_engine->process(std::string(input.data, input.length), "");
            return {0, 0.0f, false, IntentCategory::UNKNOWN};
        };
        registry.execProcessor = exec_handler;
        registry.shutdownProcessor = nullptr;
        struct DummyCapManager : public CapabilityManager {
            bool canExecute(uint32_t nodeId) const override { return true; }
        };
        static DummyCapManager dummyCap;
        g_kernel = std::make_shared<RoninKernel>(registry, dummyCap);
    }
}

JNIEXPORT void JNICALL native_setEngineInstance(JNIEnv *env, jobject thiz) {}
JNIEXPORT void JNICALL native_stopLowPriorityTasks(JNIEnv *env, jobject thiz) { if (g_intent_engine) g_intent_engine->stopLowPriorityTasks(); }

// --- Forward Declarations ---
JNIEXPORT void JNICALL native_runNightlyReflection(JNIEnv *env, jobject thiz);

JNIEXPORT jstring JNICALL native_processInput(JNIEnv *env, jobject thiz, jstring jSessionId, jstring jExecId, jstring jCorrId, jstring input, jstring systemPrompt) {
    auto uec_result = JniExecutionGateway::getInstance().createAndValidateContext(env, jSessionId, jExecId, jCorrId);
    if (!uec_result.isOk()) {
        LOGE("RoninJNI", "Execution Context Validation Failed: %s", uec_result.error().c_str());
        return env->NewStringUTF("{\"result\":\"Execution Blocked by Governance Layer.\"}");
    }
    auto exec_ctx = uec_result.value();

    if (!g_intent_engine) return env->NewStringUTF("{\"result\":\"Error\"}");
    std::string rawInput = ConvertJStringToString(env, input);
    if (g_graph_executor) g_graph_executor->clearContext();
    std::string cmdOutput;
    if (g_intent_engine->handleCommand(rawInput, cmdOutput)) {
        if (rawInput == "/reset" && g_llm_context.engine) g_llm_context.engine->purgeKVCache();
        if (rawInput == "/reflect") native_runNightlyReflection(env, thiz);
        nlohmann::json cmdRes; cmdRes["result"] = cmdOutput;
        return env->NewStringUTF(cmdRes.dump().c_str());
    }
    std::string customSystem = ConvertJStringToString(env, systemPrompt);
    if (!customSystem.empty() && g_kernel) g_kernel->setSuggestedSubject(customSystem);
    auto intent = g_intent_engine->process(rawInput, "");
    std::string result = "";
    std::string sid = "";
    if (intent.category == IntentCategory::AGENT_PLAN && g_intent_engine->getPlanner()) {
        HardwareBridge::setInferenceSilence(true);
        auto plan = g_intent_engine->getPlanner()->createPlan(rawInput);
        HardwareBridge::setInferenceSilence(false);
        if (plan.intent_name == "fallback_chat") { 
            if (g_graph_executor) {
                g_graph_executor->recordEpisode("PLANNING_FAILURE", "Agent failed to generate a valid plan for input: " + rawInput, "{}", false);
            }
            return env->NewStringUTF("{\"result\":\"Agent planning failed.\"}"); 
        }
        auto session = SessionManager::getInstance().createSession(plan.intent_name);
        sid = session->getSessionId();
        session->bindExecutionContext(exec_ctx); // v1.4 Bind UEC
        session->setPlan(plan.plan_steps);
        for (const auto& [k, v] : plan.parameters) session->setParameter(k, v);
        bool is_safe = true;
        std::string i_lower = plan.intent_name;
        std::transform(i_lower.begin(), i_lower.end(), i_lower.begin(), ::tolower);
        bool needs_sms_hitl = (i_lower.find("sms") != std::string::npos || i_lower.find("message") != std::string::npos || i_lower.find("ပို့") != std::string::npos);
        bool needs_cal_hitl = (i_lower.find("calendar") != std::string::npos || i_lower.find("event") != std::string::npos || i_lower.find("meeting") != std::string::npos);
        if (i_lower.find("map") != std::string::npos && !plan.plan_steps.empty()) {
            bool has_send_sms = false;
            for (const auto& step : plan.plan_steps) {
                std::string s_step = step;
                std::transform(s_step.begin(), s_step.end(), s_step.begin(), ::tolower);
                if (s_step.find("send_sms") != std::string::npos) { has_send_sms = true; break; }
            }
            if (!has_send_sms) needs_sms_hitl = false;
        }

        if (needs_sms_hitl || needs_cal_hitl) {
            jclass cls = env->GetObjectClass(thiz);
            jmethodID mid = env->GetMethodID(cls, "requestHITLConfirmation", "(Ljava/lang/String;Ljava/lang/String;)Z");
            if (mid) {
                jstring ji = env->NewStringUTF(plan.intent_name.c_str());
                std::string msg = needs_sms_hitl ? "Allow SMS?" : "Allow Calendar Event?";
                jstring jm = env->NewStringUTF(msg.c_str());
                if (env->CallBooleanMethod(thiz, mid, ji, jm) == JNI_FALSE) { 
                    is_safe = false; 
                    result = "Cancelled."; 
                    LOGW("RoninJNI", "[HITL] User explicitly REJECTED the action: %s", plan.intent_name.c_str());
                    
                    // v1.6: Log HITL Denial and Apply Bayesian Penalty (Phase 1.2 RLHF)
                    Execution::FailureTelemetryBus::getInstance().logFailure(exec_ctx->execution_id, plan.intent_name, FailureType::HITL_DENIED, "User rejected confirmation dialog.");
                    
                    if (g_graph_executor) {
                        // Apply high-risk penalty to the root node (0) for this intent selection
                        // This will decrease the probability of selecting this path/tool in similar contexts
                        g_graph_executor->reportOutcome(0, intent.id, false, RiskLevel::HIGH);
                    }
                }
            }
        }
        if (is_safe) { AgentScheduler::getInstance().schedule(session, 5); result = "Executing plan: " + plan.intent_name; }
    } else { result = g_intent_engine->executeSkill(intent.id, rawInput); }
    nlohmann::json fr; fr["result"] = result; fr["session_id"] = sid;
    return env->NewStringUTF(fr.dump().c_str());
}

JNIEXPORT jboolean JNICALL native_isLoaded(JNIEnv *env, jobject thiz) { return (g_llm_context.engine && g_llm_context.engine->isLoaded()) ? JNI_TRUE : JNI_FALSE; }
JNIEXPORT void JNICALL native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) { if (g_intent_engine) g_intent_engine->notifyTrimMemory(level); }
JNIEXPORT jstring JNICALL native_getActiveModelPath(JNIEnv *env, jobject thiz) { return env->NewStringUTF(g_llm_context.engine ? g_llm_context.engine->getModelPath().c_str() : ""); }
JNIEXPORT void JNICALL native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) { if (g_intent_engine) g_intent_engine->updateLocation(lat, lon); }
JNIEXPORT jboolean JNICALL native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) { HardwareBridge::reportSystemHealth(temp, used, total); return JNI_TRUE; }
JNIEXPORT void JNICALL native_cancelExecution(JNIEnv *env, jobject thiz, jstring execId) {
    std::string exec_id = ConvertJStringToString(env, execId);
    if (!exec_id.empty()) {
        Ronin::Kernel::JNI::JniExecutionGateway::getInstance().propagateCancellation(exec_id);
    }
}

JNIEXPORT void JNICALL native_requestCancellation(JNIEnv *env, jobject thiz) { if (g_llm_context.engine) g_llm_context.engine->requestCancellation(); }
JNIEXPORT void JNICALL native_setInferenceSilence(JNIEnv *env, jobject thiz, jboolean silent) { HardwareBridge::setInferenceSilence(silent == JNI_TRUE); }
JNIEXPORT void JNICALL native_injectWorldState(JNIEnv *env, jobject thiz, jfloat b, jfloat r, jboolean g, jboolean n, jboolean c, jint h) {
    g_world_state.battery_percent = b;
    g_world_state.ram_available_mb = r;
    g_world_state.gps_available = (g == JNI_TRUE);
    g_world_state.network_available = (n == JNI_TRUE);
    g_world_state.charging = (c == JNI_TRUE);
    g_world_state.hour_of_day = h;
    g_world_state.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    if (g_kernel) g_kernel->updateWorldState(g_world_state);
    Execution::AdaptiveBudgetController::getInstance().updateWorldState(g_world_state);
}
JNIEXPORT jfloat JNICALL native_getFreeRamGB(JNIEnv *env, jobject thiz) { return HardwareBridge::getFreeRamGB(); }
JNIEXPORT void JNICALL native_shutdownKernel(JNIEnv *env, jobject thiz) {
    if (g_kernel) g_kernel->shutdown();
    HardwareBridge::release(env);
    if (g_instance) {
        env->DeleteGlobalRef(g_instance);
        g_instance = nullptr;
    }
}
JNIEXPORT jint JNICALL native_getLMKPressure(JNIEnv *env, jobject thiz) { return g_memory_manager ? g_memory_manager->getPressureScore() : 0; }
JNIEXPORT jboolean JNICALL native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring p) { if (g_intent_engine) { g_intent_engine->loadCapabilities(ConvertJStringToString(env, p)); return JNI_TRUE; } return JNI_FALSE; }
JNIEXPORT jboolean JNICALL native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring j) { return JNI_TRUE; }
JNIEXPORT jboolean JNICALL native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring p) { return JNI_TRUE; }

JNIEXPORT jboolean JNICALL native_isValidModel(JNIEnv *env, jobject thiz, jstring p) {
    std::string path = ConvertJStringToString(env, p);
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return JNI_FALSE;
    char h[4]; size_t r = fread(h, 1, 4, f); fclose(f);
    return (r == 4 && memcmp(h, "TFL3", 4) == 0) || path.find(".litertlm") != std::string::npos;
}

JNIEXPORT void JNICALL native_resetContext(JNIEnv *env, jobject thiz) { if (g_kernel) g_kernel->clearSuggestedSubject(); }
JNIEXPORT void JNICALL native_reportOutcome(JNIEnv *env, jobject thiz, jint s, jint t, jboolean success, jint r) { if (g_graph_executor) g_graph_executor->reportOutcome(s, t, success == JNI_TRUE, static_cast<RiskLevel>(r)); }

JNIEXPORT void JNICALL native_reportSemanticFailure(JNIEnv *env, jobject thiz, jstring execId, jstring nodeId, jint failureType, jstring details) {
    std::string eid = ConvertJStringToString(env, execId);
    std::string nid = ConvertJStringToString(env, nodeId);
    std::string det = ConvertJStringToString(env, details);
    Execution::FailureTelemetryBus::getInstance().logFailure(eid, nid, static_cast<FailureType>(failureType), det);
}

JNIEXPORT void JNICALL native_runNightlyReflection(JNIEnv *env, jobject thiz) {
    LOGI("RoninJNI", ">>> INITIATING NIGHTLY REFLECTION CYCLE [Phase 2] <<<");
    if (g_graph_executor) {
        g_graph_executor->getReflectionEngine().reflectOnRecentTasks();
    }
}

JNIEXPORT void JNICALL native_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    std::string modelPath = ConvertJStringToString(env, path);
    LOGI(TAG, "C++ Kernel Notified: Hybrid Model Ready at %s", modelPath.c_str());
    
    if (g_llm_context.engine) {
        g_llm_context.engine->loadModel(modelPath);
    }

    if (g_intent_engine) {
        g_intent_engine->setPriority(Ronin::Kernel::Capability::SkillPriority::HIGH);
    }
}
JNIEXPORT void JNICALL native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {
    if (enabled) {
        LOGE(TAG, "Entering Strict SafeMode.");
        AgentScheduler::getInstance().purgeQueue();
        Ronin::Kernel::JNI::JniExecutionGateway::getInstance().triggerSafeMode();
        if (g_ltm) g_ltm->setReadOnly(true);
        if (g_kernel) g_kernel->enterSafeMode();
    } else {
        if (g_ltm) g_ltm->setReadOnly(false);
    }
}
JNIEXPORT void JNICALL native_setPriority(JNIEnv *env, jobject thiz, jint priority) {}
JNIEXPORT jstring JNICALL native_checkFileAccess(JNIEnv *env, jobject thiz, jstring path) { return env->NewStringUTF(""); }
JNIEXPORT void JNICALL native_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {}
JNIEXPORT void JNICALL native_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {}

JNIEXPORT void JNICALL native_submitCapabilityResponse(JNIEnv *env, jobject thiz, jstring request_id, jboolean success, jstring payload) {
    Ronin::Kernel::CapabilityResponse response;
    response.request_id = ConvertJStringToString(env, request_id);
    response.success = (success == JNI_TRUE);
    response.payload_json = ConvertJStringToString(env, payload);
    Ronin::Kernel::CapabilityDispatcher::getInstance().onResponse(response);
}

JNIEXPORT jboolean JNICALL native_pushSensorSamples(JNIEnv *env, jobject thiz, jfloatArray jx, jfloatArray jy, jfloatArray jz, jstring jtype) {
    if (!g_resonance_analyzer) return JNI_FALSE;
    if (!jx || !jy || !jz) return JNI_FALSE;
    int len = env->GetArrayLength(jx);
    if (env->GetArrayLength(jy) != len || env->GetArrayLength(jz) != len) return JNI_FALSE;
    std::vector<float> x(len), y(len), z(len);
    env->GetFloatArrayRegion(jx, 0, len, x.data());
    env->GetFloatArrayRegion(jy, 0, len, y.data());
    env->GetFloatArrayRegion(jz, 0, len, z.data());
    g_resonance_analyzer->pushSamples(x, y, z);
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL native_getSensorAnalysis(JNIEnv *env, jobject thiz, jstring jtype) {
    if (!g_resonance_analyzer) return env->NewStringUTF("{ \"error\": \"DSP_NOT_READY\" }");
    std::string type = ConvertJStringToString(env, jtype);
    return env->NewStringUTF(g_resonance_analyzer->getAnalysisJson(type).c_str());
}

static JNINativeMethod g_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"setEngineInstanceNative", "()V", (void*)native_setEngineInstance},
    {"getChatHistoryNative", "(II)[Ljava/lang/String;", (void*)native_getChatHistory},
    {"stopLowPriorityTasksNative", "()V", (void*)native_stopLowPriorityTasks},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"processInputNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)native_processInput},
    {"isLoadedNative", "()Z", (void*)native_isLoaded},
    {"notifyTrimMemoryNative", "(I)V", (void*)native_notifyTrimMemory},
    {"getActiveModelPathNative", "()Ljava/lang/String;", (void*)native_getActiveModelPath},
    {"injectLocationNative", "(DD)V", (void*)native_injectLocation},
    {"updateSystemHealthNative", "(FFF)Z", (void*)native_updateSystemHealth},
    {"getLMKPressureNative", "()I", (void*)native_getLMKPressure},
    {"updateModelRegistryNative", "(Ljava/lang/String;)Z", (void*)native_updateModelRegistry},
    {"updateCloudProvidersNative", "(Ljava/lang/String;)Z", (void*)native_updateCloudProviders},
    {"scanSpecificPathNative", "(Ljava/lang/String;)Z", (void*)native_scanSpecificPath},
    {"isValidModelNative", "(Ljava/lang/String;)Z", (void*)native_isValidModel},
    {"resetContextNativeJNI", "()V", (void*)native_resetContext},
    {"loadMyanmarDictionaryNative", "(Ljava/lang/String;)Z", (void*)native_loadMyanmarDictionary},
    {"reportOutcomeNative", "(IIZI)V", (void*)native_reportOutcome},
    {"reportSemanticFailureNative", "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V", (void*)native_reportSemanticFailure},
    {"runNightlyReflectionNative", "()V", (void*)native_runNightlyReflection},
    {"cancelExecutionNative", "(Ljava/lang/String;)V", (void*)native_cancelExecution},
    {"requestCancellationNative", "()V", (void*)native_requestCancellation},
    {"setInferenceSilenceNative", "(Z)V", (void*)native_setInferenceSilence},
    {"storeNoteNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeNote},
    {"storeFactNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeFact},
    {"storeVaultNative", "(Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeVault},
    {"lookupFactNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)native_lookupFact},
    {"lookupVaultNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_lookupVault},
    {"searchNotesNative", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)native_searchNotes},
    {"searchEpisodesNative", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)native_searchEpisodes},
    {"searchFilesNative", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)native_searchFiles},
    {"pushSensorSamplesNative", "([F[F[FLjava/lang/String;)Z", (void*)native_pushSensorSamples},
    {"getSensorAnalysisNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_getSensorAnalysis},
    {"storePredictionNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;F)Z", (void*)native_storePrediction},
    {"injectWorldStateNative", "(FFZZZI)V", (void*)native_injectWorldState},
    {"applyHumanFeedbackNative", "(Ljava/lang/String;Z)V", (void*)native_applyHumanFeedback},
    {"notifyModelLoadedNative", "(Ljava/lang/String;)V", (void*)native_notifyModelLoaded},
    {"setSafeModeNative", "(Z)V", (void*)native_setSafeMode},
    {"setPriorityNative", "(I)V", (void*)native_setPriority},
    {"checkFileAccessNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_checkFileAccess},
    {"setOfflineModeNative", "(Z)V", (void*)native_setOfflineMode},
    {"setPrimaryCloudProviderNative", "(Ljava/lang/String;)V", (void*)native_setPrimaryCloudProvider},
    {"indexFilesNative", "([Ljava/lang/String;[Ljava/lang/String;[J)V", (void*)native_indexFiles},
    {"submitCapabilityResponseNative", "(Ljava/lang/String;ZLjava/lang/String;)V", (void*)native_submitCapabilityResponse}
};

static JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    auto rc = [&](const char* c, JNINativeMethod* m, int n) {
        jclass cl = env->FindClass(c);
        if (cl) env->RegisterNatives(cl, m, n);
    };
    rc("com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods)/sizeof(g_methods[0]));
    rc("com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods)/sizeof(g_worker_methods[0]));
    return JNI_VERSION_1_6;
}

} // extern "C"
