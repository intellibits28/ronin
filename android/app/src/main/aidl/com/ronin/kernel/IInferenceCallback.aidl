package com.ronin.kernel;

/**
 * Interface for receiving real-time inference tokens across processes.
 */
interface IInferenceCallback {
    /**
     * Called when a new token fragment is generated.
     */
    void onToken(String fragment, boolean isFinal);

    /**
     * Called if an error occurs during inference.
     */
    void onError(String message);
}
