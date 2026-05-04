package com.ronin.kernel;

interface IInferenceService {
    /**
     * Hydrates the local reasoning spine with the specified model.
     */
    boolean loadModel(String modelPath);

    /**
     * Executes neural reasoning on the given input string.
     */
    String runReasoning(String input);

    /**
     * Checks if the inference engine is currently hydrated.
     */
    boolean isHydrated();

    /**
     * Gets the path of the currently active model.
     */
    String getActiveModelPath();

    /**
     * Notifies the inference process of memory pressure.
     */
    void notifyTrimMemory(int level);

    /**
     * Toggles Safe Mode (Low Precision/Throttled) for thermal protection.
     */
    void setSafeMode(boolean enabled);
}
