cmake_minimum_required(VERSION 3.10)
project(ronin_kernel)

# Phase 5.5: Final Host Test Alignment
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    cmake_policy(SET CMP0135 NEW) # Requirement: Correct FetchContent timestamps
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization: Link-Time Optimization (LTO)
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

include(FetchContent)

# Requirement 1: Static SQLite linkage
FetchContent_Declare(
  sqlite3_src
  URL https://www.sqlite.org/2023/sqlite-amalgamation-3410200.zip
)
FetchContent_MakeAvailable(sqlite3_src)

add_library(sqlite3_lib STATIC ${sqlite3_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_src_SOURCE_DIR})
# Phase 5.6: Enable FTS5 for Keyword-based search logic
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_ENABLE_FTS5=1)

# Requirement 2: FlatBuffers Schema Generation
set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG v2.0.8
)
FetchContent_MakeAvailable(flatbuffers)

# Requirement 3: ONNX Runtime for Intent Routing
if(ANDROID)
    FetchContent_Declare(
      onnxruntime
      URL https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.17.1/onnxruntime-android-1.17.1.aar
    )
else()
    FetchContent_Declare(
      onnxruntime
      URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-1.17.1.tgz
    )
endif()
FetchContent_MakeAvailable(onnxruntime)

# Requirement 4: GoogleTest for Host Validation
if(NOT ANDROID)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Include Directories
include_directories(
    include
    ${sqlite3_src_SOURCE_DIR}
    ${flatbuffers_SOURCE_DIR}/include
    ${onnxruntime_SOURCE_DIR}/include
    generated
)

# Core Sources
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
        set(ORT_LIB_PATH ${onnxruntime_SOURCE_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so)
        add_library(onnxruntime_lib SHARED IMPORTED)
        set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})

        set(MP_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/jni_libs/${ANDROID_ABI}/libllm_inference_engine_jni.so)
        set(HAS_MEDIAPIPE FALSE)

        if(EXISTS ${MP_LIB_PATH})
            message(STATUS ">>> MediaPipe GenAI library detected: ${MP_LIB_PATH}")
            add_library(mediapipe_genai SHARED IMPORTED)
            set_target_properties(mediapipe_genai PROPERTIES IMPORTED_LOCATION ${MP_LIB_PATH})
            set(HAS_MEDIAPIPE TRUE)
        else()
            message(WARNING ">>> MediaPipe GenAI library NOT found. Using header-only stubs.")
        endif()

        add_library(ronin_kernel SHARED src/ronin_jni.cpp)

        set(RONIN_LIBS ronin_core sqlite3_lib onnxruntime_lib ${log-lib} ${android-lib})
        if(HAS_MEDIAPIPE)
            list(APPEND RONIN_LIBS mediapipe_genai)
        endif()

        target_link_libraries(ronin_kernel PUBLIC ${RONIN_LIBS})

        if(ANDROID_ABI STREQUAL "arm64-v8a")
            target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
        endif()
    endif()
else()
    # Host build (Linux x64)
    find_library(ORT_LIB_PATH_INTERNAL NAMES onnxruntime
                 PATHS ${onnxruntime_SOURCE_DIR}/lib 
                 NO_DEFAULT_PATH)
    set(ORT_LIB_PATH ${ORT_LIB_PATH_INTERNAL})

    add_library(onnxruntime_lib SHARED IMPORTED)
    set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})
    target_link_libraries(ronin_core PUBLIC onnxruntime_lib)

    add_library(ronin_kernel SHARED src/ronin_jni.cpp)
    target_link_libraries(ronin_kernel PUBLIC ronin_core sqlite3_lib onnxruntime_lib)
endif()

# Optional: Host-side Unit Tests
if(NOT ANDROID)
    enable_testing()
    
    # Requirement: Link to gtest_main directly
    add_executable(ronin_atomic_test tests/atomic_integrity_test.cpp)
    target_link_libraries(ronin_atomic_test ronin_core gtest_main)

    add_executable(ronin_precision_test tests/precision_test.cpp)
    target_link_libraries(ronin_precision_test ronin_core gtest_main)

    add_executable(ronin_memory_test tests/memory_stress_test.cpp)
    target_link_libraries(ronin_memory_test ronin_core gtest_main)

    add_executable(ronin_search_test tests/search_capability_test.cpp)
    target_link_libraries(ronin_search_test ronin_core gtest_main)
endif()
