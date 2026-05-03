# Phase 4.0: ProGuard/R8 Rules for Ronin Kernel
# Prevents obfuscation of JNI methods and NativeEngine fields

-keep class com.ronin.kernel.NativeEngine {
    native <methods>;
    <fields>;
}

# Keep the HardwareBridge callbacks
-keepclassmembers class com.ronin.kernel.NativeEngine {
    void pushKernelMessage(java.lang.String);
    java.lang.String getSecureApiKey(java.lang.String);
    boolean triggerHardwareAction(int, boolean);
    java.lang.String requestHardwareData(int);
    java.lang.String performCloudInference(java.lang.String, java.lang.String);
    void updateSystemTiers(float, float, float);
    java.lang.String runNeuralReasoning(java.lang.String);
}

# Preserve Native function names (JNI)
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}
