cmake_minimum_required(VERSION 3.22.1)
project(ronin_kernel)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Global Configuration for Dependencies (CLI Critical Audit Directive)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TUNIT OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TRAIN OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TRAINING OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_NFKC_COMPILE OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Requirement: SQLite ---
FetchContent_Declare(
  sqlite3_src
  URL "https://www.sqlite.org/2023/sqlite-amalgamation-3410200.zip"
)
FetchContent_MakeAvailable(sqlite3_src)
add_library(sqlite3_lib STATIC ${sqlite3_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_src_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_ENABLE_FTS5=1)

# --- Requirement: FlatBuffers ---
if(ANDROID)
    set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
else()
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG v23.5.26
)
FetchContent_MakeAvailable(flatbuffers)

# FlatBuffers Schema Generation
set(FBS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/checkpoint_schema.fbs)
set(FBS_OUT ${CMAKE_CURRENT_BINARY_DIR}/checkpoint_schema_generated.h)

# Use target flatc if available (host build) or find it in PATH (cross-build)
if(TARGET flatc)
    set(FLATC_EXE flatc)
else()
    find_program(FLATC_EXE flatc REQUIRED)
endif()

add_custom_command(
  OUTPUT ${FBS_OUT}
  COMMAND ${FLATC_EXE} --cpp --gen-object-api -o ${CMAKE_CURRENT_BINARY_DIR} ${FBS_SRC}
  DEPENDS ${FBS_SRC}
  COMMENT "Generating FlatBuffers header from ${FBS_SRC}"
)
add_custom_target(generate_fbs DEPENDS ${FBS_OUT})


# --- Requirement: SentencePiece ---
FetchContent_Declare(
  sentencepiece
  GIT_REPOSITORY https://github.com/google/sentencepiece.git
  GIT_TAG v0.1.99
)
FetchContent_GetProperties(sentencepiece)
if(NOT sentencepiece_POPULATED)
    FetchContent_Populate(sentencepiece)
    # Use EXCLUDE_FROM_ALL to prevent building training/tools by default
    add_subdirectory(${sentencepiece_SOURCE_DIR} ${sentencepiece_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# --- Requirement: LiteRT (TensorFlow Lite) ---
FetchContent_Declare(
  tensorflow_lite
  URL https://github.com/tensorflow/tensorflow/archive/v2.14.0.zip
)
# Headers are always needed for host tests and as fallback for Android
FetchContent_GetProperties(tensorflow_lite)
if(NOT tensorflow_lite_POPULATED)
    FetchContent_Populate(tensorflow_lite)
endif()

if(ANDROID)
    message(STATUS "Android build detected. Searching for TensorFlow Lite binary...")
    
    # Standard Prefab search
    find_package(tensorflowlite CONFIG)
    if(NOT tensorflowlite_FOUND)
        find_package(tensorflow-lite CONFIG)
    endif()
    
    # Aggressive library discovery
    # AGP extracts prefab libraries to a subdirectory of the CXX build dir
    find_library(TFLITE_LIB 
        NAMES tensorflowlite tensorflowlite_jni tensorflow-lite
        HINTS 
            ${CMAKE_FIND_ROOT_PATH}
            "${CMAKE_CURRENT_BINARY_DIR}/../prefab/arm64-v8a/prefab/lib"
        PATH_SUFFIXES 
            "tensorflowlite" 
            "tensorflow-lite" 
            "libs/android.${ANDROID_ABI}" 
            "jni/${ANDROID_ABI}"
    )
    
    if(TFLITE_LIB)
        message(STATUS "SUCCESS: TensorFlow Lite binary located at: ${TFLITE_LIB}")
    elseif(tensorflowlite_FOUND OR tensorflow-lite_FOUND)
        message(STATUS "INFO: TensorFlow Lite Prefab target found.")
    else()
        message(WARNING "CRITICAL: TensorFlow Lite binary not found. Build will likely fail.")
    endif()
endif()

# --- Core Ronin Logic ---
set(RONIN_CORE_SOURCES
    src/intent_engine.cpp
    src/models/inference_engine.cpp
    src/models/hydration_manager.cpp
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
add_dependencies(ronin_core generate_fbs)

if(ANDROID)
    target_include_directories(ronin_core PUBLIC 
        include 
        ${sqlite3_src_SOURCE_DIR} 
        ${tensorflow_lite_SOURCE_DIR}
        ${sentencepiece_SOURCE_DIR}/src
        ${flatbuffers_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    # Priority Linkage Chain
    if(TARGET tensorflow-lite::tensorflow-lite)
        target_link_libraries(ronin_core PUBLIC sqlite3_lib sentencepiece-static tensorflow-lite::tensorflow-lite)
    elseif(TARGET tensorflowlite::tensorflowlite)
        target_link_libraries(ronin_core PUBLIC sqlite3_lib sentencepiece-static tensorflowlite::tensorflowlite)
    elseif(TFLITE_LIB)
        target_link_libraries(ronin_core PUBLIC sqlite3_lib sentencepiece-static ${TFLITE_LIB})
    else()
        target_link_libraries(ronin_core PUBLIC sqlite3_lib sentencepiece-static)
    endif()
else()
    target_include_directories(ronin_core PUBLIC 
        include 
        ${sqlite3_src_SOURCE_DIR} 
        ${tensorflow_lite_SOURCE_DIR}
        ${sentencepiece_SOURCE_DIR}/src
        ${flatbuffers_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_link_libraries(ronin_core PUBLIC sqlite3_lib sentencepiece-static)
endif()

# --- JNI Implementation ---
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    
    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_include_directories(ronin_kernel PRIVATE include)
    
    target_link_libraries(ronin_kernel 
        PUBLIC 
        ronin_core 
        ${log-lib} 
        ${android-lib}
    )

    # Propagate TFLite linkage to Shared Library
    if(TARGET tensorflow-lite::tensorflow-lite)
        target_link_libraries(ronin_kernel PRIVATE tensorflow-lite::tensorflow-lite)
    elseif(TARGET tensorflowlite::tensorflowlite)
        target_link_libraries(ronin_kernel PRIVATE tensorflowlite::tensorflowlite)
    elseif(TFLITE_LIB)
        target_link_libraries(ronin_kernel PRIVATE ${TFLITE_LIB})
    endif()

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
    endif()
else()
    # Host build (Linux x64)
    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_link_libraries(ronin_kernel PUBLIC ronin_core sqlite3_lib)
endif()

# Optional: Host-side Unit Tests
if(NOT ANDROID)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
    )
    FetchContent_MakeAvailable(googletest)
    
    enable_testing()
    
    add_executable(ronin_atomic_test tests/atomic_integrity_test.cpp)
    target_link_libraries(ronin_atomic_test ronin_core gtest_main)

    add_executable(ronin_precision_test tests/precision_test.cpp)
    target_link_libraries(ronin_precision_test ronin_core gtest_main)

    add_executable(ronin_memory_test tests/memory_stress_test.cpp)
    target_link_libraries(ronin_memory_test ronin_core gtest_main)

    add_executable(ronin_search_test tests/search_capability_test.cpp)
    target_link_libraries(ronin_search_test ronin_core gtest_main)

    add_executable(ronin_inference_test tests/inference_unit_test.cpp)
    target_link_libraries(ronin_inference_test ronin_core gtest_main)
endif()
