cmake_minimum_required(VERSION 3.22.1)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(RoninKernel VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
include_directories(${CMAKE_SOURCE_DIR}/generated)
include_directories(include)

# External Dependencies Integration
include(FetchContent)

# 1. Fetch GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# 2. Fetch SQLite Amalgamation
FetchContent_Declare(
  sqlite3_src
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
)

# 3. Fetch FlatBuffers (For C++ headers)
FetchContent_Declare(
  flatbuffers
  URL https://github.com/google/flatbuffers/archive/refs/tags/v2.0.8.zip
)

# 4. Fetch ONNX Runtime (C++ Library)
if(ANDROID)
    FetchContent_Declare(
      onnxruntime
      URL https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.17.1/onnxruntime-android-1.17.1.aar
    )
else()
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
        FetchContent_Declare(
          onnxruntime
          URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-aarch64-1.17.1.tgz
        )
    else()
        FetchContent_Declare(
          onnxruntime
          URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-1.17.1.tgz
        )
    endif()
endif()

# Disable FlatBuffers internal builds/tests to prevent Exec format errors during Android cross-compiling
set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest sqlite3_src flatbuffers onnxruntime)

# Define SQLite library
add_library(sqlite3_lib STATIC ${sqlite3_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_src_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5=1)

# Ensure FlatBuffers and ORT headers are accessible
include_directories(${flatbuffers_SOURCE_DIR}/include)
if(ANDROID)
    include_directories(${onnxruntime_SOURCE_DIR}/headers)
else()
    include_directories(${onnxruntime_SOURCE_DIR}/include)
endif()

# Core Logic Source (Portable)
set(RONIN_CORE_SOURCES
    src/intent_engine.cpp
    src/models/inference_engine.cpp
    src/checkpoint_engine.cpp
    src/checkpoint_manager.cpp
    src/lora_engine.cpp
    src/thompson_sampler.cpp
    src/graph_executor.cpp
    src/graph_storage.cpp
    src/memory_manager.cpp
    src/long_term_memory.cpp
    src/capability_graph.cpp
    src/ronin_kernel.cpp
    src/capabilities/file_scanner.cpp
    src/capabilities/file_search_node.cpp
    src/capabilities/neural_embedding_node.cpp
    src/capabilities/hardware_nodes.cpp
    src/capabilities/hardware_bridge.cpp
)

add_library(ronin_core STATIC ${RONIN_CORE_SOURCES})
target_link_libraries(ronin_core PUBLIC sqlite3_lib)

# Platform Specific Builds
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)

    if(log-lib AND android-lib)
        # Locate ORT shared library in AAR
        set(ORT_LIB_PATH ${onnxruntime_SOURCE_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so)
        add_library(onnxruntime_lib SHARED IMPORTED)
        set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})

        # Phase 5.3: MediaPipe Linkage for LiteRT-LM
        # We expect libmediapipe_tasks_genai_jni.so to be provided via AAR/Native libs
        find_library(MEDIAPIPE_GENAI_LIB NAMES mediapipe_tasks_genai_jni)
        if(MEDIAPIPE_GENAI_LIB)
            add_library(mediapipe_genai SHARED IMPORTED)
            set_target_properties(mediapipe_genai PROPERTIES IMPORTED_LOCATION ${MEDIAPIPE_GENAI_LIB})
            target_link_libraries(ronin_core PUBLIC mediapipe_genai)
        endif()

        # Link system libraries to core so tests inherit them
        target_link_libraries(ronin_core PUBLIC ${log-lib} ${android-lib} onnxruntime_lib)

        # JNI Bridge Library (Requirement 4)
        add_library(ronin_kernel SHARED src/ronin_jni.cpp)
        target_link_libraries(ronin_kernel PUBLIC ronin_core sqlite3_lib onnxruntime_lib ${log-lib})

        # Enable ARMv8.2-A Dot Product extension only for arm64-v8a
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
        endif()
    else()
        # Fallback for Android environments without NDK libs (like Termux)
        find_library(ORT_LIB_PATH_INTERNAL NAMES onnxruntime
                     PATHS ${onnxruntime_SOURCE_DIR}/lib 
                           ${onnxruntime_SOURCE_DIR}/jni/arm64-v8a 
                           ${onnxruntime_SOURCE_DIR}/jni/x86_64
                     NO_DEFAULT_PATH)
        set(ORT_LIB_PATH ${ORT_LIB_PATH_INTERNAL})

        add_library(onnxruntime_lib SHARED IMPORTED)
        set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})
        target_link_libraries(ronin_core PUBLIC rt onnxruntime_lib)

        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
            target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
        endif()
    endif()
else()
    # Host build needs rt and ORT
    set(ORT_LIB_PATH "ORT_LIB_PATH-NOTFOUND")
    find_library(ORT_LIB_PATH_INTERNAL NAMES onnxruntime
                 PATHS ${onnxruntime_SOURCE_DIR}/lib 
                       ${onnxruntime_SOURCE_DIR}/jni/arm64-v8a 
                       ${onnxruntime_SOURCE_DIR}/jni/x86_64
                 NO_DEFAULT_PATH)
    set(ORT_LIB_PATH ${ORT_LIB_PATH_INTERNAL})
    message(STATUS ">>> Phase 4.0: ORT_LIB_PATH is ${ORT_LIB_PATH}")

    add_library(onnxruntime_lib SHARED IMPORTED)
    set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})
    target_link_libraries(ronin_core PUBLIC rt onnxruntime_lib)

    # Enable ARMv8.2-A Dot Product extension for host ARM64
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
    endif()
endif()

enable_testing()

# Memory Stress Test
add_executable(ronin_memory_test tests/memory_stress_test.cpp)
target_link_libraries(ronin_memory_test gtest_main ronin_core onnxruntime_lib)

# Precision Test
add_executable(ronin_precision_test tests/precision_test.cpp)
target_link_libraries(ronin_precision_test gtest_main ronin_core onnxruntime_lib)

# Atomic Integrity Test
add_executable(ronin_atomic_test tests/atomic_integrity_test.cpp)
target_link_libraries(ronin_atomic_test gtest_main ronin_core onnxruntime_lib)

# Search Capability Test
add_executable(ronin_search_test tests/search_capability_test.cpp)
target_link_libraries(ronin_search_test gtest_main ronin_core onnxruntime_lib)

if(NOT ANDROID)
    target_link_libraries(ronin_memory_test rt)
    target_link_libraries(ronin_precision_test rt)
    target_link_libraries(ronin_atomic_test rt)
    target_link_libraries(ronin_search_test rt)
endif()
