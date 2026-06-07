#include "ronin_jni.h"
#include <memory>
#include <string>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include "jni_utils.h"
#include "ronin_kernel.hpp"
#include "intent_engine.h"
#include "graph_executor.h"
#include "capability_graph.h"
#include "graph_storage.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "agent_scheduler.h"
#include "session_manager.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/chat_skill.h"
#include "models/inference_engine.h"
#include "ronin_log.h"
#include <nlohmann/json.hpp>

#ifdef __ANDROID__

#define TAG "RoninKernel_JNI"

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::Model;
using namespace Ronin::Kernel::Capability;
using namespace Ronin::Kernel::Reasoning;

// Concrete implementation of CapabilityManager to resolve abstract class error
class DefaultCapabilityManager : public CapabilityManager {
public:
    bool canExecute(uint32_t nodeId) const override { return true; }
};

static std::unique_ptr<RoninKernel> g_kernel = nullptr;
static std::shared_ptr<LongTermMemory> g_ltm = nullptr;
static std::shared_ptr<IntentEngine> g_intent_engine = nullptr;
static std::unique_ptr<MemoryManager> g_memory_manager = nullptr;

// v6.0 Adaptive Agent Core
static std::unique_ptr<Ronin::Kernel::Reasoning::GraphStorage> g_graph_storage = nullptr;
static std::unique_ptr<Ronin::Kernel::Reasoning::CapabilityGraph> g_cap_graph = nullptr;
static std::unique_ptr<Ronin::Kernel::Reasoning::GraphExecutor> g_graph_executor = nullptr;

JavaVM* g_vm = nullptr;
jobject g_instance = nullptr;

struct LLMContext {
    InferenceEngine* engine = nullptr;
} g_llm_context;

// --- Helper Functions ---

static std::string ConvertJStringToString(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* cstr = env->GetStringUTFChars(jstr, nullptr);
    if (!cstr) return "";
    std::string str(cstr);
    env->ReleaseStringUTFChars(jstr, cstr);
    return str;
}

// --- v6.0 Registry Handlers ---

static CognitiveIntent intent_handler(const Input& input) {
    if (!g_intent_engine) return {1, 0.6f, true, IntentCategory::CHAT_QUERY};
    return g_intent_engine->process(std::string(input.data, input.length), "");
}

static Result exec_handler(uint32_t nodeId, const CognitiveState& state) {
    // v6.0 execution via IntentEngine registry
    if (g_intent_engine) {
        g_intent_engine->executeSkill(nodeId, ""); // Param handling needs expansion
        return {true, 200};
    }
    return {false, 500};
}

// --- JNI Implementation ---

void native_initializeKernel(JNIEnv *env, jobject thiz, jstring filesDir, jstring libDir, jboolean isWorker) {
    std::string base_path = ConvertJStringToString(env, filesDir);
    std::string native_lib_path = ConvertJStringToString(env, libDir);

    if (!isWorker) {
        g_instance = env->NewGlobalRef(thiz);
        // UI Process Initialization
        g_ltm = std::make_shared<LongTermMemory>(base_path + "/ronin_cognitive.db");
        
        // v6.0 Adaptive Agent Core Initialization
        g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
        g_cap_graph = std::make_unique<CapabilityGraph>();
        g_graph_storage->loadGraph(*g_cap_graph);
        g_graph_executor = std::make_unique<GraphExecutor>(*g_cap_graph, *g_graph_storage, g_ltm.get());
        
        AgentScheduler::getInstance().setExecutor(g_graph_executor.get());

        // Fix MemoryManager initialization
        g_memory_manager = std::make_unique<MemoryManager>(2048);
        g_memory_manager->setLongTermMemory(g_ltm.get());
        
        g_intent_engine = std::make_shared<IntentEngine>(g_ltm.get());
        g_intent_engine->setMemoryManager(g_memory_manager.get()); 
        
        HardwareBridge::initialize(g_vm, thiz);
        
        HandlerRegistry registry;
        registry.intentProcessor = intent_handler;
        registry.execProcessor = exec_handler;
        registry.shutdownProcessor = []() {
            LOGI(TAG, "Cleaning up JNI Global Kernel components.");
        };

        static DefaultCapabilityManager cap_manager; 
        g_kernel = std::make_unique<RoninKernel>(registry, cap_manager);
        auto engine = std::make_unique<InferenceEngine>("hybrid_mode");
        engine->setLibPath(native_lib_path);
        engine->setBasePath(base_path);
        
        auto chatSkill = std::make_shared<ChatSkill>(engine.get(), g_ltm.get());
        chatSkill->setKernel(g_kernel.get()); // Pass kernel for system prompt
        g_intent_engine->registerSkill(1, chatSkill);

        g_llm_context.engine = engine.get();
        g_intent_engine->setInferenceEngine(std::move(engine));
        
        LOGI(TAG, "Ronin Kernel UI Core Active.");
    } else {
        LOGI(TAG, "Ronin Kernel Worker Node Active.");
    }
}

void native_setEngineInstance(JNIEnv *env, jobject thiz) {}

void native_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    if (g_llm_context.engine) {
        // Use loadModel instead of notifyModelLoaded
        g_llm_context.engine->loadModel(ConvertJStringToString(env, path));
    }
}

void native_stopLowPriorityTasks(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) g_intent_engine->stopLowPriorityTasks();
}

void native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {}
void native_setPriority(JNIEnv *env, jobject thiz, jint priority) {}

jstring native_checkFileAccess(JNIEnv *env, jobject thiz, jstring path) {
    std::string p = ConvertJStringToString(env, path);
    std::ifstream f(p);
    return env->NewStringUTF(f.good() ? "OK" : "DENIED");
}

jfloat native_getFreeRamGB(JNIEnv *env, jobject thiz) {
    return HardwareBridge::getFreeRamGB();
}

void native_reportOutcome(JNIEnv *env, jobject thiz, jint sourceId, jint targetId, jboolean success, jint risk) {
    if (g_graph_executor) {
        g_graph_executor->reportOutcome(
            static_cast<uint32_t>(sourceId),
            static_cast<uint32_t>(targetId),
            success == JNI_TRUE,
            static_cast<Ronin::Kernel::Reasoning::RiskLevel>(risk)
        );
    }
}

void native_setInferenceSilence(JNIEnv *env, jobject thiz, jboolean silent) {
    LOGI(TAG, "L3 Bridge: Setting silentInference to %d", silent);
    jclass cls = env->GetObjectClass(thiz);
    jfieldID field = env->GetFieldID(cls, "silentInference", "Z");
    if (field) {
        env->SetBooleanField(thiz, field, silent);
    } else {
        LOGE(TAG, "L3 Bridge ERROR: Could not find silentInference field!");
    }
}

jstring native_processInput(JNIEnv *env, jobject thiz, jstring input, jstring systemPrompt) {
    if (!g_intent_engine) return env->NewStringUTF("Error: Engine Not Ready.");
    
    std::string rawInput = ConvertJStringToString(env, input);
    
    // v10.2.10: Integrated Command Handler (/status, /skills, /model, /reset)
    std::string cmdOutput;
    if (g_intent_engine->handleCommand(rawInput, cmdOutput)) {
        if (rawInput == "/reset" && g_llm_context.engine) {
            g_llm_context.engine->purgeKVCache();
        }
        return env->NewStringUTF(cmdOutput.c_str());
    }

    std::string customSystem = ConvertJStringToString(env, systemPrompt);
    
    if (!customSystem.empty() && g_kernel) {
        g_kernel->setSuggestedSubject(customSystem);
    }
    
    auto intent = g_intent_engine->process(rawInput, "");
    std::string result;

    if (intent.category == IntentCategory::AGENT_PLAN && g_intent_engine->getPlanner()) {
        LOGI(TAG, "v7.7: Initializing Robust Agent Planning for input: %s", rawInput.c_str());
        
        // v7.6: Suppress tokens during internal planning
        native_setInferenceSilence(env, thiz, JNI_TRUE);
        auto plan = g_intent_engine->getPlanner()->createPlan(rawInput);
        native_setInferenceSilence(env, thiz, JNI_FALSE);
        
        if (plan.intent_name == "fallback_chat") {
            LOGW(TAG, "L1 Planner: Falling back to chat due to failure.");
            return env->NewStringUTF("Agent planning failed. Please try a different request.");
        }

        auto session = SessionManager::getInstance().createSession(plan.intent_name);
        session->setPlan(plan.plan_steps);
        for (const auto& [k, v] : plan.parameters) session->setParameter(k, v);

        bool is_safe = true;
        std::string i_lower = plan.intent_name;
        std::transform(i_lower.begin(), i_lower.end(), i_lower.begin(), ::tolower);

        // --- v10.2.11: Precise HITL Safety Check ---
        bool needs_sms_hitl = (i_lower.find("sms") != std::string::npos || i_lower.find("message") != std::string::npos || i_lower.find("ပို့") != std::string::npos);
        
        // Final guard: If it's a map request, don't trigger SMS HITL even if Gemma hallucinated 'MAP/SMS'
        if (i_lower.find("map") != std::string::npos && plan.plan_steps.size() > 0) {
            bool has_send_sms = false;
            for (const auto& step : plan.plan_steps) {
                std::string s_step = step;
                std::transform(s_step.begin(), s_step.end(), s_step.begin(), ::tolower);
                if (s_step.find("send_sms") != std::string::npos) { has_send_sms = true; break; }
            }
            if (!has_send_sms) needs_sms_hitl = false;
        }

        if (needs_sms_hitl) {
            
            session->setState(AgentState::ASK_CONFIRMATION);
            jclass cls = env->GetObjectClass(thiz);
            jmethodID hitlMethod = env->GetMethodID(cls, "requestHITLConfirmation", "(Ljava/lang/String;Ljava/lang/String;)Z");
            if (hitlMethod) {
                std::string recipient = "Someone";
                if (plan.parameters.count("recipient")) recipient = plan.parameters["recipient"];
                else if (plan.parameters.count("contact_name")) recipient = plan.parameters["contact_name"];
                
                std::string hitl_msg = "Do you want to send your location to " + recipient + " via SMS?";
                jstring jIntentName = env->NewStringUTF(plan.intent_name.c_str());
                jstring jMessage = env->NewStringUTF(hitl_msg.c_str());
                jboolean approved = env->CallBooleanMethod(thiz, hitlMethod, jIntentName, jMessage);
                env->DeleteLocalRef(jIntentName);
                env->DeleteLocalRef(jMessage);
                
                if (approved == JNI_FALSE) {
                    is_safe = false;
                    session->setState(AgentState::FAILED);
                    result = "Action cancelled by user.";
                }
            }
        }
        
        if (is_safe) {
            LOGI(TAG, "v7.7: Scheduling session %s. Intent: %s", session->getSessionId().c_str(), i_lower.c_str());
            AgentScheduler::getInstance().schedule(session, 5); 
            result = "Agent is executing the plan: " + plan.intent_name;
        }
    } else {
        result = g_intent_engine->executeSkill(intent.id, rawInput);
    }

    return env->NewStringUTF(result.c_str());
}

jboolean native_isLoaded(JNIEnv *env, jobject thiz) {
    return (g_llm_context.engine && g_llm_context.engine->isLoaded()) ? JNI_TRUE : JNI_FALSE;
}

void native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    if (g_intent_engine) g_intent_engine->notifyTrimMemory(level);
}

jstring native_getActiveModelPath(JNIEnv *env, jobject thiz) {
    if (g_llm_context.engine) return env->NewStringUTF(g_llm_context.engine->getModelPath().c_str());
    return env->NewStringUTF("");
}

void native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_intent_engine) g_intent_engine->updateLocation(lat, lon);
}

jboolean native_storeNote(JNIEnv *env, jobject thiz, jstring title, jstring content, jstring tags) {
    if (!g_ltm) return JNI_FALSE;
    return g_ltm->storeNote(ConvertJStringToString(env, title), ConvertJStringToString(env, content), ConvertJStringToString(env, tags)) ? JNI_TRUE : JNI_FALSE;
}

jboolean native_storeFact(JNIEnv *env, jobject thiz, jstring entity, jstring attr, jstring value) {
    if (!g_ltm) return JNI_FALSE;
    return g_ltm->storeFact(ConvertJStringToString(env, entity), ConvertJStringToString(env, attr), ConvertJStringToString(env, value)) ? JNI_TRUE : JNI_FALSE;
}

jstring native_lookupFact(JNIEnv *env, jobject thiz, jstring entity, jstring attr) {
    if (!g_ltm) return env->NewStringUTF("");
    std::string val = g_ltm->lookupFact(ConvertJStringToString(env, entity), ConvertJStringToString(env, attr));
    return env->NewStringUTF(val.c_str());
}

jstring native_lookupVault(JNIEnv *env, jobject thiz, jstring title) {
    if (!g_ltm) return env->NewStringUTF("");
    std::string val = g_ltm->lookupVault(ConvertJStringToString(env, title));
    return env->NewStringUTF(val.c_str());
}

jobjectArray native_searchNotes(JNIEnv *env, jobject thiz, jstring query) {
    if (!g_ltm) return nullptr;
    auto results = g_ltm->searchNotes(ConvertJStringToString(env, query));
    jobjectArray res = (jobjectArray)env->NewObjectArray(results.size(), env->FindClass("java/lang/String"), env->NewStringUTF(""));
    for (size_t i = 0; i < results.size(); ++i) env->SetObjectArrayElement(res, i, env->NewStringUTF(results[i].c_str()));
    return res;
}

jobjectArray native_searchEpisodes(JNIEnv *env, jobject thiz, jstring query) {
    if (!g_ltm) return nullptr;
    auto results = g_ltm->searchEpisodes(ConvertJStringToString(env, query));
    jobjectArray res = (jobjectArray)env->NewObjectArray(results.size(), env->FindClass("java/lang/String"), env->NewStringUTF(""));
    for (size_t i = 0; i < results.size(); ++i) env->SetObjectArrayElement(res, i, env->NewStringUTF(results[i].c_str()));
    return res;
}

jboolean native_storeVault(JNIEnv *env, jobject thiz, jstring title, jstring encrypted_blob) {
    if (!g_ltm) return JNI_FALSE;
    return g_ltm->storeVault(ConvertJStringToString(env, title), ConvertJStringToString(env, encrypted_blob)) ? JNI_TRUE : JNI_FALSE;
}

jboolean native_storePrediction(JNIEnv *env, jobject thiz, jstring goalId, jstring nodeId, jstring predicted, jstring actual, jfloat error) {
    if (!g_ltm) return JNI_FALSE;
    return g_ltm->storePrediction(ConvertJStringToString(env, goalId), ConvertJStringToString(env, nodeId), 
                                  ConvertJStringToString(env, predicted), ConvertJStringToString(env, actual), error) ? JNI_TRUE : JNI_FALSE;
}

void native_injectWorldState(JNIEnv *env, jobject thiz, jfloat battery, jfloat ram, jboolean gps, jboolean net, jboolean charging) {
    // Blueprint v1.3: World State is transient. We can log it or pass it to intent engine.
    if (g_intent_engine) {
        // g_intent_engine->updateWorldState(...) // Needs implementation in IntentEngine
    }
}

jboolean native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    HardwareBridge::reportSystemHealth(temp, used, total);
    return JNI_TRUE;
}

void native_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {}
void native_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {}

jint native_getLMKPressure(JNIEnv *env, jobject thiz) {
    return g_memory_manager ? g_memory_manager->getPressureScore() : 0;
}

jboolean native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring path) {
    if (g_intent_engine) {
        g_intent_engine->loadCapabilities(ConvertJStringToString(env, path));
        return JNI_TRUE;
    }
    return JNI_FALSE;
}
jboolean native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }
jboolean native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring path) { return JNI_TRUE; }

jboolean native_isValidModel(JNIEnv *env, jobject thiz, jstring path) {
    std::string p = ConvertJStringToString(env, path);
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return JNI_FALSE;
    char header[4];
    size_t read = fread(header, 1, 4, f);
    fclose(f);
    
    if (read == 4 && memcmp(header, "TFL3", 4) == 0) return JNI_TRUE;
    
    if (p.find(".litertlm") != std::string::npos || p.find(".bin") != std::string::npos) {
        return JNI_TRUE;
    }
    
    return JNI_FALSE;
}

jobjectArray native_getChatHistory(JNIEnv *env, jobject thiz, jint limit, jint offset) {
    if (!g_ltm) return nullptr;
    auto history = g_ltm->getHistory(limit, offset);
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(history.size() * 2, stringClass, nullptr);
    for (size_t i = 0; i < history.size(); ++i) {
        env->SetObjectArrayElement(result, i * 2, env->NewStringUTF(history[i].first.c_str()));
        env->SetObjectArrayElement(result, i * 2 + 1, env->NewStringUTF(history[i].second.c_str()));
    }
    return result;
}

void native_resetContext(JNIEnv *env, jobject thiz) {
    if (g_ltm) g_ltm->clearHistory();
    if (g_memory_manager) g_memory_manager->clearContext();
}

jboolean native_loadMyanmarDictionary(JNIEnv *env, jobject thiz, jstring path) {
    if (g_ltm) {
        return g_ltm->loadSegmenter(ConvertJStringToString(env, path)) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

void native_requestCancellation(JNIEnv *env, jobject thiz) {
    if (g_llm_context.engine) g_llm_context.engine->requestCancellation();
}

void native_shutdownKernel(JNIEnv *env, jobject thiz) {
    if (g_kernel) g_kernel->shutdown();
    HardwareBridge::release(env);
}

// --- JNI Registration ---

static JNINativeMethod g_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"setEngineInstanceNative", "()V", (void*)native_setEngineInstance},
    {"notifyModelLoadedNative", "(Ljava/lang/String;)V", (void*)native_notifyModelLoaded},
    {"stopLowPriorityTasksNative", "()V", (void*)native_stopLowPriorityTasks},
    {"setSafeModeNative", "(Z)V", (void*)native_setSafeMode},
    {"setPriorityNative", "(I)V", (void*)native_setPriority},
    {"checkFileAccessNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_checkFileAccess},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"processInputNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)native_processInput},
    {"isLoadedNative", "()Z", (void*)native_isLoaded},
    {"notifyTrimMemoryNative", "(I)V", (void*)native_notifyTrimMemory},
    {"getActiveModelPathNative", "()Ljava/lang/String;", (void*)native_getActiveModelPath},
    {"injectLocationNative", "(DD)V", (void*)native_injectLocation},
    {"updateSystemHealthNative", "(FFF)Z", (void*)native_updateSystemHealth},
    {"setOfflineModeNative", "(Z)V", (void*)native_setOfflineMode},
    {"setPrimaryCloudProviderNative", "(Ljava/lang/String;)V", (void*)native_setPrimaryCloudProvider},
    {"getLMKPressureNative", "()I", (void*)native_getLMKPressure},
    {"updateModelRegistryNative", "(Ljava/lang/String;)Z", (void*)native_updateModelRegistry},
    {"updateCloudProvidersNative", "(Ljava/lang/String;)Z", (void*)native_updateCloudProviders},
    {"scanSpecificPathNative", "(Ljava/lang/String;)Z", (void*)native_scanSpecificPath},
    {"isValidModelNative", "(Ljava/lang/String;)Z", (void*)native_isValidModel},
    {"getChatHistoryNative", "(II)[Ljava/lang/String;", (void*)native_getChatHistory},
    {"resetContextNativeJNI", "()V", (void*)native_resetContext},
    {"loadMyanmarDictionaryNative", "(Ljava/lang/String;)Z", (void*)native_loadMyanmarDictionary},
    {"reportOutcomeNative", "(IIZI)V", (void*)native_reportOutcome},
    {"requestCancellationNative", "()V", (void*)native_requestCancellation},
    {"setInferenceSilenceNative", "(Z)V", (void*)native_setInferenceSilence},
    {"storeNoteNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeNote},
    {"storeFactNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeFact},
    {"storeVaultNative", "(Ljava/lang/String;Ljava/lang/String;)Z", (void*)native_storeVault},
    {"lookupFactNative", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)native_lookupFact},
    {"lookupVaultNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_lookupVault},
    {"searchNotesNative", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)native_searchNotes},
    {"searchEpisodesNative", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)native_searchEpisodes},
    {"storePredictionNative", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;F)Z", (void*)native_storePrediction},
    {"injectWorldStateNative", "(FFZZZ)V", (void*)native_injectWorldState}
};

static JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    auto register_class = [&](const char* className, JNINativeMethod* methods, int count) {
        jclass cls = env->FindClass(className);
        if (cls) env->RegisterNatives(cls, methods, count);
    };

    register_class("com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods) / sizeof(g_methods[0]));
    register_class("com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods) / sizeof(g_worker_methods[0]));

    return JNI_VERSION_1_6;
}

#endif // __ANDROID__
