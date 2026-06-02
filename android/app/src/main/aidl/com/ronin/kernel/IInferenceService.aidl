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
    boolean isLowPerformanceMode();
    String summarizeAndReset();
}
