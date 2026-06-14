# Repository Guidelines

## Project Structure & Module Organization

Ronin is a C++20 native kernel with Android/Kotlin integration. Core native sources live in `src/`, with public headers in `include/`; keep mirrored subdirectories such as `src/capabilities/` and `include/capabilities/` aligned. Android app code is under `android/app/src/main/kotlin/com/ronin/kernel/`, AIDL interfaces are in `android/app/src/main/aidl/`, and app assets are in `android/app/src/main/assets/`. Host-side C++ tests are in `tests/`; Android unit tests are in `android/app/src/test/`. Architecture notes and product specs belong in `docs/`.

## Build, Test, and Development Commands

- `cmake -S . -B build_host -DCMAKE_BUILD_TYPE=Debug`: configure a host build and fetch CMake dependencies.
- `cmake --build build_host`: build the native kernel and host test binaries.
- `cd build_host && ctest --output-on-failure`: run registered host tests.
- `cd build_host && ./ronin_atomic_test && ./ronin_integration_test && ./ronin_segmenter_test`: run the main native checks directly.
- `cd android && gradle assembleDebug`: build the Android debug APK when a local Gradle installation is available.
- `cd android && gradle test`: run Android/JVM unit tests.

## Coding Style & Naming Conventions

Use C++20 for native code and Kotlin/JVM 17 for Android. Follow the existing style: four-space indentation in CMake/Kotlin blocks, snake_case for C++ files and functions where already used, and PascalCase for Kotlin classes such as `NativeEngine` and `InferenceService`. Keep JNI boundary code in `src/ronin_jni.cpp`, `src/jni_utils.cpp`, and related Android classes. Prefer small, focused headers in `include/` and update `CMakeLists.txt` whenever adding native sources.

## Testing Guidelines

Native tests use GoogleTest where applicable and are named by behavior or subsystem, for example `atomic_integrity_test.cpp` and `segmenter_stress_test.cpp`. Add new host tests under `tests/` and register new executables in `CMakeLists.txt`. Android tests should live under `android/app/src/test/kotlin/` and follow `*Test.kt` naming. Run host tests before native changes and Android tests before Kotlin, AIDL, or Gradle changes.

## Commit & Pull Request Guidelines

Recent history uses concise Conventional Commit-style prefixes such as `feat(agent): ...`, `fix(agent): ...`, and `chore(ci): ...`; follow that pattern and include the affected area in parentheses when useful. Pull requests should describe the behavior change, list tests run, link related issues or docs, and include screenshots or logcat snippets for UI, Android service, or runtime diagnostics changes.

## Security & Configuration Tips

Do not commit API keys, local model files, generated logs, or device-specific secrets. Keep capability manifests in `assets/capabilities.json` and `android/app/src/main/assets/capabilities.json` synchronized when behavior changes.
