#include "capability_policy_engine.h"
#include "ronin_log.h"
#include <fstream>
#include <vector>
#include <algorithm>

#ifdef __ANDROID__
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
#ifdef __ANDROID__
    // Obtain AssetManager from the stored Java VM / instance in RuntimeContext.
    if (!JNI::runtimeContext().vm) {
        LOGW(TAG, "CapabilityPolicyEngine: JVM not available – empty policy.");
        return;
    }
    JNIEnv* env = nullptr;
    JNI::runtimeContext().vm->AttachCurrentThread(&env, nullptr);
    // Get AssetManager from the Android Context stored as "instance".
    jclass ctxCls = env->FindClass("android/content/Context");
    if (!ctxCls) { LOGE(TAG, "CapabilityPolicyEngine: cannot find Context class"); return; }
    jmethodID getAssetMgr = env->GetMethodID(ctxCls, "getAssets", "()Landroid/content/res/AssetManager;");
    if (!getAssetMgr) { LOGE(TAG, "CapabilityPolicyEngine: cannot find getAssets method"); return; }
    jobject assetMgrObj = env->CallObjectMethod(JNI::runtimeContext().instance, getAssetMgr);
    if (!assetMgrObj) { LOGW(TAG, "CapabilityPolicyEngine: AssetManager is null"); return; }
    AAssetManager* mgr = AAssetManager_fromJava(env, assetMgrObj);
    if (!mgr) { LOGE(TAG, "CapabilityPolicyEngine: AAssetManager_fromJava failed"); return; }
    // The manifest is placed in the APK assets folder.
    const char* manifestPath = "policy_manifest.json";
    AAsset* asset = AAssetManager_open(mgr, manifestPath, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGW(TAG, "CapabilityPolicyEngine: %s not found in assets", manifestPath);
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
    // Populate policy map.
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
        reason = ReasonCode::UNKNOWN_CAPABILITY;
        return PolicyDecision::DENY;
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

