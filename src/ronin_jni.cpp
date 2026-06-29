#include "ronin_jni.h"
#include <jni.h>
#include <chrono>
#include <cstdio>
#include <cstring>
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

namespace Ronin::Kernel::JNI {

KernelRuntimeContext& runtimeContext() {
    static KernelRuntimeContext context;
    return context;
}

void KernelRuntimeContext::release(JNIEnv* env) {
    if (kernel) kernel->shutdown();
    Ronin::Kernel::Capability::HardwareBridge::release(env);
    if (instance) {
        env->DeleteGlobalRef(instance);
        instance = nullptr;
    }

    AgentScheduler::getInstance().setExecutor(nullptr);
    llm_context.engine = nullptr;
    kernel.reset();
    intent_engine.reset();
    memory_manager.reset();
    graph_executor.reset();
    cap_graph.reset();
    graph_storage.reset();
    resonance_analyzer.reset();
    ltm.reset();
    world_state = {};
}

} // namespace Ronin::Kernel::JNI

// --- Helper: exec_handler to bridge Kernel to JNI ---
static Result exec_handler(uint32_t nodeId, const CognitiveState& state) {
    if (runtimeContext().intent_engine) {
        std::string param = "";
        // Extract the most relevant parameter based on common naming from current_plan parameters
        if (state.current_plan.parameters.count("query")) param = state.current_plan.parameters.at("query");
        else if (state.current_plan.parameters.count("value")) param = state.current_plan.parameters.at("value");
        else if (state.current_plan.parameters.count("entity")) param = state.current_plan.parameters.at("entity");
        else if (state.current_plan.parameters.count("time")) param = state.current_plan.parameters.at("time");

        runtimeContext().intent_engine->executeSkill(nodeId, param);
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
        if (runtimeContext().instance) {
            env->DeleteGlobalRef(runtimeContext().instance);
            runtimeContext().instance = nullptr;
        }
        runtimeContext().instance = env->NewGlobalRef(thiz);
        runtimeContext().ltm = std::make_shared<LongTermMemory>(base_path + "/ronin_cognitive.db");
        Execution::FailureTelemetryBus::getInstance().setMemory(runtimeContext().ltm.get());
        Execution::ExecutionCheckpointStore::getInstance().initialize(runtimeContext().ltm->getDatabase());
        Execution::RuntimeHealingController::getInstance().initialize(&AgentScheduler::getInstance());
        runtimeContext().resonance_analyzer = std::shared_ptr<ResonanceAnalyzer>(&ResonanceAnalyzer::getInstance(), [](ResonanceAnalyzer*){});
        runtimeContext().graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
        runtimeContext().cap_graph = std::make_unique<CapabilityGraph>();
        runtimeContext().graph_storage->loadGraph(*runtimeContext().cap_graph);

        // v12.20: Ensure Core Nodes exist in graph
        if (!runtimeContext().cap_graph->getNodeByID("SENSOR")) runtimeContext().cap_graph->addNode(3, "SENSOR");
        if (!runtimeContext().cap_graph->getNodeByID("FILES")) runtimeContext().cap_graph->addNode(6, "FILES");
        if (!runtimeContext().cap_graph->getNodeByID("LOCATION")) runtimeContext().cap_graph->addNode(1, "LOCATION");
        if (!runtimeContext().cap_graph->getNodeByID("CALENDAR")) runtimeContext().cap_graph->addNode(12, "CALENDAR");

        runtimeContext().graph_executor = std::make_unique<GraphExecutor>(*runtimeContext().cap_graph, *runtimeContext().graph_storage, runtimeContext().ltm.get());
        AgentScheduler::getInstance().setExecutor(runtimeContext().graph_executor.get());
        runtimeContext().memory_manager = std::make_unique<MemoryManager>(2048);
        runtimeContext().memory_manager->setLongTermMemory(runtimeContext().ltm.get());
        runtimeContext().intent_engine = std::make_shared<IntentEngine>(runtimeContext().ltm.get());
        runtimeContext().intent_engine->setMemoryManager(runtimeContext().memory_manager.get());
        if (runtimeContext().graph_executor) runtimeContext().intent_engine->setBeliefState(&runtimeContext().graph_executor->getBeliefState());

        auto engine = std::make_unique<Model::InferenceEngine>("hybrid_mode");
        engine->setLibPath(native_lib_path);
        engine->setBasePath(base_path);
        runtimeContext().llm_context.engine = engine.get();
        runtimeContext().intent_engine->setInferenceEngine(std::move(engine));
        if (runtimeContext().graph_executor) runtimeContext().graph_executor->getReflectionEngine().setInferenceEngine(runtimeContext().llm_context.engine);

        HardwareBridge::initialize(runtimeContext().vm, runtimeContext().instance);
        HandlerRegistry registry;
        registry.intentProcessor = [](const Input &input) -> CognitiveIntent {
            if (runtimeContext().intent_engine) return runtimeContext().intent_engine->process(std::string(input.data, input.length), "");
            return {0, 0.0f, false, IntentCategory::UNKNOWN};
        };
        registry.execProcessor = exec_handler;
        registry.shutdownProcessor = nullptr;
        struct DummyCapManager : public CapabilityManager {
            bool canExecute(uint32_t nodeId) const override { return true; }
        };
        static DummyCapManager dummyCap;
        runtimeContext().kernel = std::make_shared<RoninKernel>(registry, dummyCap);
        runtimeContext().actor_supervisor = std::make_unique<Ronin::Kernel::Execution::ActorSupervisor>();
        runtimeContext().capability_compiler = std::make_unique<CapabilityCompiler>();
        runtimeContext().planner_rule_cache = std::make_unique<Reasoning::PlannerRuleCache>();
        runtimeContext().capability_policy_engine = std::make_unique<Ronin::Kernel::CapabilityPolicyEngine>();
        runtimeContext().shadow_testing_manager = std::make_unique<Ronin::Kernel::Optimization::ShadowTestingManager>();
        runtimeContext().ab_version_manager = std::make_unique<Ronin::Kernel::Optimization::ABVersionManager>();
        runtimeContext().safe_rollback_manager = std::make_unique<Ronin::Kernel::Optimization::SafeRollbackManager>(runtimeContext().shadow_testing_manager.get());
    }
}

JNIEXPORT void JNICALL native_setEngineInstance(JNIEnv *env, jobject thiz) {}
JNIEXPORT void JNICALL native_stopLowPriorityTasks(JNIEnv *env, jobject thiz) { if (runtimeContext().intent_engine) runtimeContext().intent_engine->stopLowPriorityTasks(); }

// --- Forward Declarations ---
JNIEXPORT void JNICALL native_runNightlyReflection(JNIEnv *env, jobject thiz);

JNIEXPORT jstring JNICALL native_processInput(JNIEnv *env, jobject thiz, jstring jSessionId, jstring jExecId, jstring jCorrId, jstring input, jstring systemPrompt) {
    auto uec_result = JniExecutionGateway::getInstance().createAndValidateContext(env, jSessionId, jExecId, jCorrId);
    if (!uec_result.isOk()) {
        LOGE("RoninJNI", "Execution Context Validation Failed: %s", uec_result.error().c_str());
        nlohmann::json err_res;
        err_res["success"] = false;
        err_res["result"] = "Error: Execution Blocked by Governance Layer.";
        err_res["session_id"] = "";
        err_res["error"] = {
            {"code", "POLICY_DENIED"},
            {"message", uec_result.error()}
        };
        return env->NewStringUTF(err_res.dump().c_str());
    }
    auto exec_ctx = uec_result.value();

    if (!runtimeContext().intent_engine) {
        nlohmann::json err_res;
        err_res["success"] = false;
        err_res["result"] = "Error: Intent engine not initialized.";
        err_res["session_id"] = "";
        err_res["error"] = {
            {"code", "SERVICE_DISCONNECTED"},
            {"message", "Intent engine not initialized."}
        };
        return env->NewStringUTF(err_res.dump().c_str());
    }
    std::string rawInput = ConvertJStringToString(env, input);
    if (runtimeContext().graph_executor) runtimeContext().graph_executor->clearContext();
    std::string cmdOutput;
    if (runtimeContext().intent_engine->handleCommand(rawInput, cmdOutput)) {
        if (rawInput == "/reset" && runtimeContext().llm_context.engine) runtimeContext().llm_context.engine->purgeKVCache();
        if (rawInput == "/reflect") native_runNightlyReflection(env, thiz);
        nlohmann::json cmdRes;
        cmdRes["success"] = true;
        cmdRes["result"] = cmdOutput;
        return env->NewStringUTF(cmdRes.dump().c_str());
    }
    std::string customSystem = ConvertJStringToString(env, systemPrompt);
    if (!customSystem.empty() && runtimeContext().kernel) runtimeContext().kernel->setSuggestedSubject(customSystem);
    auto intent = runtimeContext().intent_engine->process(rawInput, "");
    std::string result = "";
    std::string sid = "";
    if (intent.category == IntentCategory::AGENT_PLAN && runtimeContext().intent_engine->getPlanner()) {
        HardwareBridge::setInferenceSilence(true);
        auto plan = runtimeContext().intent_engine->getPlanner()->createPlan(rawInput);
        HardwareBridge::setInferenceSilence(false);
        if (plan.intent_name == "fallback_chat") {
            if (runtimeContext().graph_executor) {
                runtimeContext().graph_executor->recordEpisode("PLANNING_FAILURE", "Agent failed to generate a valid plan for input: " + rawInput, "{}", false);
            }
            nlohmann::json err_res;
            err_res["success"] = false;
            err_res["result"] = "Error: Agent planning failed.";
            err_res["session_id"] = "";
            err_res["error"] = {
                {"code", "PLANNING_FAILED"},
                {"message", "Agent failed to generate a valid plan for input."}
            };
            return env->NewStringUTF(err_res.dump().c_str());
        }
        auto session = SessionManager::getInstance().createSession(plan.intent_name);
        sid = session->getSessionId();
        session->bindExecutionContext(exec_ctx); // v1.4 Bind UEC
        if (plan.parameters.find("corr_id") != plan.parameters.end() && exec_ctx) {
            exec_ctx->correlation_id = plan.parameters.at("corr_id");
        }
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

                    if (runtimeContext().graph_executor) {
                        // Apply high-risk penalty to the root node (0) for this intent selection
                        // This will decrease the probability of selecting this path/tool in similar contexts
                        runtimeContext().graph_executor->reportOutcome(0, intent.id, false, RiskLevel::HIGH);
                    }
                }
            }
        }
        if (is_safe) { AgentScheduler::getInstance().schedule(session, 5); result = "Executing plan: " + plan.intent_name; }
    } else { result = runtimeContext().intent_engine->executeSkill(intent.id, rawInput); }
    nlohmann::json fr;
    fr["session_id"] = sid;
    if (result == "Cancelled.") {
        fr["success"] = false;
        fr["result"] = "Error: Cancelled.";
        fr["error"] = {
            {"code", "CANCELLED"},
            {"message", "User rejected confirmation dialog."}
        };
    } else {
        fr["success"] = true;
        fr["result"] = result;
    }
    return env->NewStringUTF(fr.dump().c_str());
}

JNIEXPORT jboolean JNICALL native_isLoaded(JNIEnv *env, jobject thiz) { return (runtimeContext().llm_context.engine && runtimeContext().llm_context.engine->isLoaded()) ? JNI_TRUE : JNI_FALSE; }
JNIEXPORT void JNICALL native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) { if (runtimeContext().intent_engine) runtimeContext().intent_engine->notifyTrimMemory(level); }
JNIEXPORT jstring JNICALL native_getActiveModelPath(JNIEnv *env, jobject thiz) { return env->NewStringUTF(runtimeContext().llm_context.engine ? runtimeContext().llm_context.engine->getModelPath().c_str() : ""); }
JNIEXPORT void JNICALL native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) { if (runtimeContext().intent_engine) runtimeContext().intent_engine->updateLocation(lat, lon); }
JNIEXPORT jboolean JNICALL native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) { HardwareBridge::reportSystemHealth(temp, used, total); return JNI_TRUE; }
JNIEXPORT void JNICALL native_cancelExecution(JNIEnv *env, jobject thiz, jstring execId) {
    std::string exec_id = ConvertJStringToString(env, execId);
    if (!exec_id.empty()) {
        Ronin::Kernel::JNI::JniExecutionGateway::getInstance().propagateCancellation(exec_id);
    }
}

JNIEXPORT void JNICALL native_requestCancellation(JNIEnv *env, jobject thiz) { if (runtimeContext().llm_context.engine) runtimeContext().llm_context.engine->requestCancellation(); }
JNIEXPORT void JNICALL native_setInferenceSilence(JNIEnv *env, jobject thiz, jboolean silent) { HardwareBridge::setInferenceSilence(silent == JNI_TRUE); }
JNIEXPORT void JNICALL native_injectWorldState(JNIEnv *env, jobject thiz, jfloat b, jfloat r, jboolean g, jboolean n, jboolean c, jint h) {
    runtimeContext().world_state.battery_percent = b;
    runtimeContext().world_state.ram_available_mb = r;
    runtimeContext().world_state.gps_available = (g == JNI_TRUE);
    runtimeContext().world_state.network_available = (n == JNI_TRUE);
    runtimeContext().world_state.charging = (c == JNI_TRUE);
    runtimeContext().world_state.hour_of_day = h;
    runtimeContext().world_state.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    if (runtimeContext().kernel) runtimeContext().kernel->updateWorldState(runtimeContext().world_state);
    Execution::AdaptiveBudgetController::getInstance().updateWorldState(runtimeContext().world_state);
}
JNIEXPORT jfloat JNICALL native_getFreeRamGB(JNIEnv *env, jobject thiz) { return HardwareBridge::getFreeRamGB(); }
JNIEXPORT void JNICALL native_shutdownKernel(JNIEnv *env, jobject thiz) {
    runtimeContext().release(env);
}
JNIEXPORT jint JNICALL native_getLMKPressure(JNIEnv *env, jobject thiz) { return runtimeContext().memory_manager ? runtimeContext().memory_manager->getPressureScore() : 0; }
JNIEXPORT jboolean JNICALL native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring p) { if (runtimeContext().intent_engine) { runtimeContext().intent_engine->loadCapabilities(ConvertJStringToString(env, p)); return JNI_TRUE; } return JNI_FALSE; }
JNIEXPORT jboolean JNICALL native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring j) { return JNI_TRUE; }
JNIEXPORT jboolean JNICALL native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring p) { return JNI_TRUE; }

JNIEXPORT jboolean JNICALL native_isValidModel(JNIEnv *env, jobject thiz, jstring p) {
    std::string path = ConvertJStringToString(env, p);
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return JNI_FALSE;
    char h[4]; size_t r = fread(h, 1, 4, f); fclose(f);
    return (r == 4 && memcmp(h, "TFL3", 4) == 0) || path.find(".litertlm") != std::string::npos;
}

JNIEXPORT void JNICALL native_resetContext(JNIEnv *env, jobject thiz) { if (runtimeContext().kernel) runtimeContext().kernel->clearSuggestedSubject(); }
JNIEXPORT void JNICALL native_reportOutcome(JNIEnv *env, jobject thiz, jint s, jint t, jboolean success, jint r) { if (runtimeContext().graph_executor) runtimeContext().graph_executor->reportOutcome(s, t, success == JNI_TRUE, static_cast<RiskLevel>(r)); }

JNIEXPORT void JNICALL native_reportSemanticFailure(JNIEnv *env, jobject thiz, jstring execId, jstring nodeId, jint failureType, jstring details) {
    std::string eid = ConvertJStringToString(env, execId);
    std::string nid = ConvertJStringToString(env, nodeId);
    std::string det = ConvertJStringToString(env, details);
    Execution::FailureTelemetryBus::getInstance().logFailure(eid, nid, static_cast<FailureType>(failureType), det);
}

JNIEXPORT void JNICALL native_runNightlyReflection(JNIEnv *env, jobject thiz) {
    LOGI("RoninJNI", ">>> INITIATING NIGHTLY REFLECTION CYCLE [Phase 2] <<<");
    if (runtimeContext().graph_executor) {
        runtimeContext().graph_executor->getReflectionEngine().reflectOnRecentTasks();
    }
}

JNIEXPORT void JNICALL native_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    std::string modelPath = ConvertJStringToString(env, path);
    LOGI(TAG, "C++ Kernel Notified: Hybrid Model Ready at %s", modelPath.c_str());

    if (runtimeContext().llm_context.engine) {
        runtimeContext().llm_context.engine->loadModel(modelPath);
    }

    if (runtimeContext().intent_engine) {
        runtimeContext().intent_engine->setPriority(Ronin::Kernel::Capability::SkillPriority::HIGH);
    }
}
JNIEXPORT void JNICALL native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {
    if (enabled) {
        LOGE(TAG, "Entering Strict SafeMode.");
        AgentScheduler::getInstance().purgeQueue();
        Ronin::Kernel::JNI::JniExecutionGateway::getInstance().triggerSafeMode();
        if (runtimeContext().ltm) runtimeContext().ltm->setReadOnly(true);
        if (runtimeContext().kernel) runtimeContext().kernel->enterSafeMode();
    } else {
        if (runtimeContext().ltm) runtimeContext().ltm->setReadOnly(false);
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
    if (!runtimeContext().resonance_analyzer) return JNI_FALSE;
    if (!jx || !jy || !jz) return JNI_FALSE;
    int len = env->GetArrayLength(jx);
    if (env->GetArrayLength(jy) != len || env->GetArrayLength(jz) != len) return JNI_FALSE;
    std::vector<float> x(len), y(len), z(len);
    env->GetFloatArrayRegion(jx, 0, len, x.data());
    env->GetFloatArrayRegion(jy, 0, len, y.data());
    env->GetFloatArrayRegion(jz, 0, len, z.data());
    runtimeContext().resonance_analyzer->pushSamples(x, y, z);
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL native_getSensorAnalysis(JNIEnv *env, jobject thiz, jstring jtype) {
    if (!runtimeContext().resonance_analyzer) return env->NewStringUTF("{ \"error\": \"DSP_NOT_READY\" }");
    std::string type = ConvertJStringToString(env, jtype);
    return env->NewStringUTF(runtimeContext().resonance_analyzer->getAnalysisJson(type).c_str());
}

} // extern "C"
