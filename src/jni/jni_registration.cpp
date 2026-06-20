#include "ronin_jni_context.h"

using Ronin::Kernel::JNI::runtimeContext;

namespace {

JNINativeMethod g_methods[] = {
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

JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

void registerClassNatives(JNIEnv* env, const char* class_name, JNINativeMethod* methods, int method_count) {
    jclass cls = env->FindClass(class_name);
    if (cls) env->RegisterNatives(cls, methods, method_count);
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    runtimeContext().vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    registerClassNatives(env, "com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods) / sizeof(g_methods[0]));
    registerClassNatives(env, "com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods) / sizeof(g_worker_methods[0]));
    return JNI_VERSION_1_6;
}
