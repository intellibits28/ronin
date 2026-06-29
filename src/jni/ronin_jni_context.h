#pragma once

#include <jni.h>
#include <memory>

#include "capability_graph.h"
#include "dsp/resonance_analyzer.h"
#include "graph_executor.h"
#include "graph_storage.h"
#include "intent_engine.h"
#include "long_term_memory.h"
#include "memory_manager.h"
#include "models/inference_engine.h"
#include "ronin_kernel.hpp"
#include "ronin_types.hpp"
#include "planner_rule_cache.h"
#include "capability_compiler.h"

namespace Ronin::Kernel {
namespace JNI {

struct LLMContext {
    Model::InferenceEngine* engine = nullptr;
};

class KernelRuntimeContext {
public:
    std::shared_ptr<RoninKernel> kernel;
    std::shared_ptr<Intent::IntentEngine> intent_engine;
    std::shared_ptr<Memory::LongTermMemory> ltm;
    std::shared_ptr<DSP::ResonanceAnalyzer> resonance_analyzer;
    std::unique_ptr<Reasoning::GraphStorage> graph_storage;
    std::unique_ptr<Reasoning::CapabilityGraph> cap_graph;
    std::unique_ptr<Reasoning::GraphExecutor> graph_executor;
    std::unique_ptr<Memory::MemoryManager> memory_manager;
    LLMContext llm_context;
    WorldState world_state;
    std::unique_ptr<Reasoning::PlannerRuleCache> planner_rule_cache;
    std::unique_ptr<CapabilityCompiler> capability_compiler;
    std::unique_ptr<CapabilityPolicyEngine> capability_policy_engine;
    std::unique_ptr<Ronin::Kernel::Execution::ActorSupervisor> actor_supervisor;
std::unique_ptr<Ronin::Kernel::Optimization::ABVersionManager> ab_version_manager;
std::unique_ptr<Ronin::Kernel::Optimization::SafeRollbackManager> safe_rollback_manager;
    jobject instance = nullptr;
    JavaVM* vm = nullptr;

    bool initialized() const { return intent_engine != nullptr; }
    // Accessors for new components
    Reasoning::PlannerRuleCache* getPlannerCache() const { return planner_rule_cache.get(); }
    CapabilityCompiler* getCapabilityCompiler() const { return capability_compiler.get(); }
    CapabilityPolicyEngine* getPolicyEngine() const { return capability_policy_engine.get(); }
    Ronin::Kernel::Execution::ActorSupervisor* getActorSupervisor() const { return actor_supervisor.get(); }
    Ronin::Kernel::Optimization::ABVersionManager* getABVersionManager() const { return ab_version_manager.get(); }
    Ronin::Kernel::Optimization::SafeRollbackManager* getSafeRollbackManager() const { return safe_rollback_manager.get(); }
};

KernelRuntimeContext& runtimeContext();

} // namespace JNI
}

extern "C" {

JNIEXPORT void JNICALL native_initializeKernel(JNIEnv* env, jobject thiz, jstring filesDir, jstring libDir, jboolean isWorker);
JNIEXPORT void JNICALL native_setEngineInstance(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_stopLowPriorityTasks(JNIEnv* env, jobject thiz);
JNIEXPORT jstring JNICALL native_processInput(JNIEnv* env, jobject thiz, jstring jSessionId, jstring jExecId, jstring jCorrId, jstring input, jstring systemPrompt);
JNIEXPORT jboolean JNICALL native_isLoaded(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_notifyTrimMemory(JNIEnv* env, jobject thiz, jint level);
JNIEXPORT jstring JNICALL native_getActiveModelPath(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_injectLocation(JNIEnv* env, jobject thiz, jdouble lat, jdouble lon);
JNIEXPORT jboolean JNICALL native_updateSystemHealth(JNIEnv* env, jobject thiz, jfloat temp, jfloat used, jfloat total);
JNIEXPORT void JNICALL native_cancelExecution(JNIEnv* env, jobject thiz, jstring execId);
JNIEXPORT void JNICALL native_requestCancellation(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_setInferenceSilence(JNIEnv* env, jobject thiz, jboolean silent);
JNIEXPORT void JNICALL native_injectWorldState(JNIEnv* env, jobject thiz, jfloat b, jfloat r, jboolean g, jboolean n, jboolean c, jint h);
JNIEXPORT jfloat JNICALL native_getFreeRamGB(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_shutdownKernel(JNIEnv* env, jobject thiz);
JNIEXPORT jint JNICALL native_getLMKPressure(JNIEnv* env, jobject thiz);
JNIEXPORT jboolean JNICALL native_updateModelRegistry(JNIEnv* env, jobject thiz, jstring p);
JNIEXPORT jboolean JNICALL native_updateCloudProviders(JNIEnv* env, jobject thiz, jstring j);
JNIEXPORT jboolean JNICALL native_scanSpecificPath(JNIEnv* env, jobject thiz, jstring p);
JNIEXPORT jboolean JNICALL native_isValidModel(JNIEnv* env, jobject thiz, jstring p);
JNIEXPORT void JNICALL native_resetContext(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_reportOutcome(JNIEnv* env, jobject thiz, jint s, jint t, jboolean success, jint r);
JNIEXPORT void JNICALL native_reportSemanticFailure(JNIEnv* env, jobject thiz, jstring execId, jstring nodeId, jint failureType, jstring details);
JNIEXPORT void JNICALL native_runNightlyReflection(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL native_notifyModelLoaded(JNIEnv* env, jobject thiz, jstring path);
JNIEXPORT void JNICALL native_setSafeMode(JNIEnv* env, jobject thiz, jboolean enabled);
JNIEXPORT void JNICALL native_setPriority(JNIEnv* env, jobject thiz, jint priority);
JNIEXPORT jstring JNICALL native_checkFileAccess(JNIEnv* env, jobject thiz, jstring path);
JNIEXPORT void JNICALL native_setOfflineMode(JNIEnv* env, jobject thiz, jboolean offline);
JNIEXPORT void JNICALL native_setPrimaryCloudProvider(JNIEnv* env, jobject thiz, jstring provider);
JNIEXPORT void JNICALL native_submitCapabilityResponse(JNIEnv* env, jobject thiz, jstring request_id, jboolean success, jstring payload);
JNIEXPORT jboolean JNICALL native_pushSensorSamples(JNIEnv* env, jobject thiz, jfloatArray jx, jfloatArray jy, jfloatArray jz, jstring jtype);
JNIEXPORT jstring JNICALL native_getSensorAnalysis(JNIEnv* env, jobject thiz, jstring jtype);
JNIEXPORT jboolean JNICALL native_storeNote(JNIEnv* env, jobject thiz, jstring t, jstring c, jstring tg);
JNIEXPORT jboolean JNICALL native_storeFact(JNIEnv* env, jobject thiz, jstring e, jstring a, jstring v);
JNIEXPORT jboolean JNICALL native_storeAuditLog(JNIEnv* env, jobject thiz, jstring a, jstring d);
JNIEXPORT jstring JNICALL native_lookupFact(JNIEnv* env, jobject thiz, jstring e, jstring a);
JNIEXPORT jstring JNICALL native_lookupVault(JNIEnv* env, jobject thiz, jstring t);
JNIEXPORT jobjectArray JNICALL native_searchNotes(JNIEnv* env, jobject thiz, jstring q);
JNIEXPORT jobjectArray JNICALL native_searchEpisodes(JNIEnv* env, jobject thiz, jstring q);
JNIEXPORT jboolean JNICALL native_storeVault(JNIEnv* env, jobject thiz, jstring t, jstring b);
JNIEXPORT jboolean JNICALL native_storePrediction(JNIEnv* env, jobject thiz, jstring g, jstring n, jstring p, jstring a, jfloat e);
JNIEXPORT void JNICALL native_applyHumanFeedback(JNIEnv* env, jobject thiz, jstring s, jboolean h);
JNIEXPORT jboolean JNICALL native_updateBelief(JNIEnv* env, jobject thiz, jstring key, jstring value, jfloat confidence);
JNIEXPORT jobjectArray JNICALL native_getChatHistory(JNIEnv* env, jobject thiz, jint l, jint o);
JNIEXPORT jboolean JNICALL native_loadMyanmarDictionary(JNIEnv* env, jobject thiz, jstring p);
JNIEXPORT void JNICALL native_indexFiles(JNIEnv* env, jobject thiz, jobjectArray paths, jobjectArray names, jlongArray dates);
JNIEXPORT jobjectArray JNICALL native_searchFiles(JNIEnv* env, jobject thiz, jstring q);

}
