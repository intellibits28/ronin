#include "capability_policy_engine.h"
#include "ronin_log.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>

#if defined(__ANDROID__) && defined(RONIN_ANDROID_APK_BUILD)
#include "jni/ronin_jni_context.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#endif

#define TAG "CapabilityPolicyEngine"

using json = nlohmann::json;
using namespace Ronin::Kernel;

CapabilityPolicyEngine::CapabilityPolicyEngine() {
    loadManifest();
}

void CapabilityPolicyEngine::loadManifest() {
#if defined(__ANDROID__) && defined(RONIN_ANDROID_APK_BUILD)
    if (!JNI::runtimeContext().vm || !JNI::runtimeContext().instance) {
        LOGW(TAG, "CapabilityPolicyEngine: JVM or instance not available – empty policy.");
        return;
    }
    JNIEnv* env = nullptr;
    JNI::runtimeContext().vm->AttachCurrentThread(&env, nullptr);
    jclass instCls = env->GetObjectClass(JNI::runtimeContext().instance);
    if (!instCls) { LOGE(TAG, "CapabilityPolicyEngine: cannot find class of instance"); return; }
    jmethodID getAssetMgr = env->GetMethodID(instCls, "getAssets", "()Landroid/content/res/AssetManager;");
    if (!getAssetMgr) { LOGE(TAG, "CapabilityPolicyEngine: cannot find getAssets method"); return; }
    jobject assetMgrObj = env->CallObjectMethod(JNI::runtimeContext().instance, getAssetMgr);
    if (!assetMgrObj) { LOGW(TAG, "CapabilityPolicyEngine: AssetManager is null"); return; }
    AAssetManager* mgr = AAssetManager_fromJava(env, assetMgrObj);
    if (!mgr) { LOGE(TAG, "CapabilityPolicyEngine: AAssetManager_fromJava failed"); return; }
    // The manifest is placed in the APK assets folder.
    const char* manifestPath = "policy_manifest.json";
    AAsset* asset = AAssetManager_open(mgr, manifestPath, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGW(TAG, "CapabilityPolicyEngine: %s not found in assets, trying capabilities.json", manifestPath);
        manifestPath = "capabilities.json";
        asset = AAssetManager_open(mgr, manifestPath, AASSET_MODE_BUFFER);
    }
    if (!asset) {
        LOGE(TAG, "CapabilityPolicyEngine: neither policy_manifest.json nor capabilities.json found in assets");
        return;
    }
    size_t size = AAsset_getLength(asset);
    std::vector<char> buffer(size);
    AAsset_read(asset, buffer.data(), size);
    AAsset_close(asset);
    // Parse JSON (non‑throwing variant).
    json j = json::parse(buffer.data(), buffer.data() + size, nullptr, false);
    if (j.is_discarded()) {
        LOGE(TAG, "CapabilityPolicyEngine: failed to parse manifest JSON.");
        return;
    }
    // Populate policy map from object map or array.
    if (j.is_object()) {
        for (auto& [key, val] : j.items()) {
            PolicyEntry entry;
            entry.allowed = val.value("allowed", true);
            entry.battery_min = val.value("battery_min", 0);
            entry.requires_network = val.value("requires_network", false);
            entry.requires_human = val.value("requires_human", false);
            if (val.contains("allowed_versions")) {
                entry.allowed_versions = val["allowed_versions"].get<std::vector<int>>();
            }
            m_policyMap.emplace(key, std::move(entry));
        }
    } else if (j.is_array()) {
        for (auto& item : j) {
            std::string name = item.value("name", "");
            if (name.empty()) continue;
            PolicyEntry entry;
            entry.allowed = true;
            entry.battery_min = 5;
            entry.requires_network = false;
            entry.requires_human = false;
            entry.allowed_versions = {1};
            m_policyMap.emplace(name, std::move(entry));
        }
    }
    LOGI(TAG, "CapabilityPolicyEngine: loaded %zu policy entries", m_policyMap.size());
#else
    LOGI(TAG, "CapabilityPolicyEngine: host build detected, using default empty policy.");
#endif
}

PolicyDecision CapabilityPolicyEngine::evaluate(const std::string &capabilityId,
                                               int batteryLevel,
                                               bool isOnline,
                                               bool privacyAllowed,
                                               int version,
                                               std::optional<ReasonCode> &reason) const {
    auto it = m_policyMap.find(capabilityId);
    if (it == m_policyMap.end()) {
#if !(defined(__ANDROID__) && defined(RONIN_ANDROID_APK_BUILD))
        reason = ReasonCode::NONE;
        return PolicyDecision::ALLOW;
#else
        reason = ReasonCode::UNKNOWN_CAPABILITY;
        return PolicyDecision::DENY;
#endif
    }
    const PolicyEntry &e = it->second;
    if (!e.allowed) {
        reason = ReasonCode::UNKNOWN_CAPABILITY;
        return PolicyDecision::DENY;
    }
    if (batteryLevel < e.battery_min) {
        reason = ReasonCode::BATTERY_LOW;
        return PolicyDecision::DENY;
    }
    if (e.requires_network && !isOnline) {
        reason = ReasonCode::OFFLINE;
        return PolicyDecision::DENY;
    }
    if (e.requires_human) {
        reason = ReasonCode::HITL_REQUIRED;
        return PolicyDecision::DEFER;
    }
    if (!privacyAllowed) {
        reason = ReasonCode::PRIVACY_BLOCKED;
        return PolicyDecision::DENY;
    }
    if (!e.allowed_versions.empty() &&
        std::find(e.allowed_versions.begin(), e.allowed_versions.end(), version) == e.allowed_versions.end()) {
        reason = ReasonCode::VERSION_MISMATCH;
        return PolicyDecision::DENY;
    }
    reason = ReasonCode::NONE;
    return PolicyDecision::ALLOW;
}

void CapabilityPolicyEngine::resetMcpMonitor() {
    std::lock_guard<std::mutex> lock(m_mcpMutex);
    m_mcpRing.fill(McpCallRecord{});
    m_mcpHead = 0;
    m_mcpCount = 0;
    m_ewmaIntervalMs = 2000.0f;
}

McpAnomalyEvaluation CapabilityPolicyEngine::evaluateMcpToolCall(
    const std::string &toolName,
    const std::string &targetPath,
    const std::string &activeIntent,
    uint64_t timestampMs,
    float surpriseThreshold) {
    McpAnomalyEvaluation out;

    // Static high-risk tools always require confirmation
    if (toolName == "write_file" || toolName == "run_command" || toolName == "execute_shell" || toolName == "delete_file") {
        out.decision = PolicyDecision::DEFER;
        out.reason = ReasonCode::HITL_REQUIRED;
        out.contextual_surprise = 1.0f;
        out.requires_hitl_confirm = true;
        return out;
    }

    if (timestampMs == 0) {
        timestampMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::lock_guard<std::mutex> lock(m_mcpMutex);

    // 1. Burst Rate Surprise: compare short-window call rate against EWMA interval
    float burstSurprise = 0.0f;
    if (m_mcpCount > 0) {
        size_t prevIdx = (m_mcpHead + MCP_RING_CAPACITY - 1) % MCP_RING_CAPACITY;
        uint64_t prevTs = m_mcpRing[prevIdx].timestamp_ms;
        uint64_t deltaMs = (timestampMs >= prevTs) ? (timestampMs - prevTs) : 0;

        // Count calls within the last 2000 ms window
        size_t recentCalls = 0;
        for (size_t i = 0; i < m_mcpCount; ++i) {
            size_t idx = (m_mcpHead + MCP_RING_CAPACITY - 1 - i) % MCP_RING_CAPACITY;
            if (timestampMs >= m_mcpRing[idx].timestamp_ms && (timestampMs - m_mcpRing[idx].timestamp_ms) <= 2000) {
                recentCalls++;
            }
        }

        float expectedInterval = std::max(100.0f, m_ewmaIntervalMs);
        float instantRatio = expectedInterval / std::max(10.0f, static_cast<float>(deltaMs));
        float burstRatio = static_cast<float>(recentCalls) / 4.0f; // >4 calls in 2s is anomalous
        burstSurprise = std::clamp(0.45f * std::tanh((instantRatio - 1.5f) * 0.5f) + 0.55f * std::clamp(burstRatio, 0.0f, 1.0f), 0.0f, 1.0f);

        // Update EWMA interval (alpha = 0.25)
        m_ewmaIntervalMs = 0.75f * m_ewmaIntervalMs + 0.25f * static_cast<float>(std::min<uint64_t>(deltaMs, 5000));
    }

    // Record current call into the 32-slot Ring Buffer
    uint32_t pathHash = 2166136261u;
    for (unsigned char c : targetPath) {
        pathHash ^= c;
        pathHash *= 16777619u;
    }
    m_mcpRing[m_mcpHead] = McpCallRecord{timestampMs, pathHash};
    m_mcpHead = (m_mcpHead + 1) % MCP_RING_CAPACITY;
    if (m_mcpCount < MCP_RING_CAPACITY) {
        m_mcpCount++;
    }

    // 2. Path Entropy / Sensitivity Surprise
    std::string lowerPath = targetPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) { return std::tolower(c); });
    float pathEntropy = 0.0f;
    if (lowerPath.find("..") != std::string::npos) pathEntropy += 0.65f;
    if (lowerPath.find(".ssh") != std::string::npos || lowerPath.find(".env") != std::string::npos ||
        lowerPath.find("id_rsa") != std::string::npos || lowerPath.find("ronin_cognitive") != std::string::npos) {
        pathEntropy += 0.75f;
    }
    if (lowerPath.find("/sdcard/dcim") != std::string::npos || lowerPath.find("/sdcard/whatsapp") != std::string::npos ||
        lowerPath.find("/data/data/") != std::string::npos || lowerPath.find("/proc/") != std::string::npos) {
        pathEntropy += 0.55f;
    }
    pathEntropy = std::clamp(pathEntropy, 0.0f, 1.0f);

    // 3. Intent-Path Divergence
    float intentDivergence = 0.0f;
    if (!activeIntent.empty() && !lowerPath.empty()) {
        std::string lowerIntent = activeIntent;
        std::transform(lowerIntent.begin(), lowerIntent.end(), lowerIntent.begin(), [](unsigned char c) { return std::tolower(c); });
        // If user intent is casual chat/weather/greeting while tool is probing filesystem paths
        if ((lowerIntent.find("weather") != std::string::npos || lowerIntent.find("hello") != std::string::npos ||
             lowerIntent.find("joke") != std::string::npos) &&
            (toolName == "read_file" || toolName == "list_files")) {
            intentDivergence = 0.60f;
        }
    }

    // Combined Contextual Surprise F_mcp
    float contextualSurprise = std::clamp(
        0.45f * burstSurprise + 0.45f * pathEntropy + 0.30f * intentDivergence,
        0.0f, 1.0f);
    // Also allow either extreme path sensitivity or extreme burst to trigger threshold
    contextualSurprise = std::max({contextualSurprise, pathEntropy * 0.95f, burstSurprise * 0.90f});

    out.burst_surprise = burstSurprise;
    out.path_entropy = pathEntropy;
    out.intent_divergence = intentDivergence;
    out.contextual_surprise = contextualSurprise;

    if (contextualSurprise > surpriseThreshold) {
        out.decision = PolicyDecision::DEFER;
        out.reason = ReasonCode::ANOMALY_SURPRISE_HIGH;
        out.requires_hitl_confirm = true;
    } else {
        out.decision = PolicyDecision::ALLOW;
        out.reason = ReasonCode::NONE;
        out.requires_hitl_confirm = false;
    }
    return out;
}


