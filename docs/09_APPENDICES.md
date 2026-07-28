# 9. Appendices

## Glossary
*   **F₀ (Fundamental Frequency)**: The lowest resonant frequency of a vibrating object.
*   **PSD (Power Spectral Density)**: A measure of a signal's power content versus frequency.
*   **LiteRT**: The optimized runtime environment for executing machine learning models on edge devices (formerly TFLite).
*   **JNI (Java Native Interface)**: The bridge allowing Kotlin code running in the JVM to execute native C/C++ libraries.

## Developer Setup
To build the Ronin Kernel:
1.  Ensure you have CMake 3.22+ and Ninja installed.
2.  For Android, ensure Gradle 8.0+ and the Android NDK are configured.
3.  Run `cmake -S . -B build_host -DCMAKE_BUILD_TYPE=Debug` for host testing.
4.  Run `cd android && gradle assembleDebug` for the APK build.

## Troubleshooting
*   **No F0 Detected**: Ensure the device is placed firmly on a vibrating surface. Hand-holding the device absorbs high-frequency vibrations.
*   **AI Review Timeout**: Verify the API key in the Developer Settings HUD and check network connectivity.
*   **JNI UnsatisfiedLinkError**: The C++ shared library (`libronin_core.so`) failed to compile or load. Check Gradle NDK build logs.
