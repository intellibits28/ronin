package com.ronin.kernel;

import com.ronin.kernel.IInferenceCallback;

interface IInferenceService {
    boolean loadModel(String modelPath);
    String runReasoning(String input);
    boolean isHydrated();
    String getActiveModelPath();
    void notifyTrimMemory(int level);
    void setSafeMode(boolean enabled);
    void streamReasoning(String input, IInferenceCallback callback);
    void resetConversation();
    void updateSamplingParams(float temperature, int topK, float topP);
    void updateGenerationConfig(float temperature, int topK, float topP, int maxTokens);
    boolean isLowPerformanceMode();
    String summarizeAndReset();
}
