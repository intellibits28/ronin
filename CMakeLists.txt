cmake_minimum_required(VERSION 3.22.1)
project(ronin_kernel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Requirement: SQLite ---
FetchContent_Declare(
  sqlite3_src
  URL https://www.sqlite.org/2023/sqlite-amalgamation-3410200.zip
)
FetchContent_MakeAvailable(sqlite3_src)
add_library(sqlite3_lib STATIC ${sqlite3_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_src_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_ENABLE_FTS5=1)

# --- Requirement: FlatBuffers ---
set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG v2.0.8
)
FetchContent_MakeAvailable(flatbuffers)

# FlatBuffers Schema Generation
set(FBS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/checkpoint_schema.fbs)
set(FBS_OUT ${CMAKE_CURRENT_BINARY_DIR}/checkpoint_schema_generated.h)

# Logic to find the correct flatc (Host vs Target)
if(ANDROID)
    # On Android cross-build, we MUST use a host flatc
    find_program(FLATC_EXE flatc)
    if(NOT FLATC_EXE)
        message(FATAL_ERROR "flatc not found in PATH. Required for Android cross-build.")
    endif()
else()
    # On host build, we can use the target build by FetchContent if available
    if(TARGET flatc)
        set(FLATC_EXE flatc)
    else()
        find_program(FLATC_EXE flatc)
    endif()
endif()

if(NOT FLATC_EXE)
    set(FLATC_EXE flatbuffers::flatc)
endif()

add_custom_command(
  OUTPUT ${FBS_OUT}
  COMMAND ${FLATC_EXE} --cpp --gen-object-api -o ${CMAKE_CURRENT_BINARY_DIR} ${FBS_SRC}
  DEPENDS ${FBS_SRC}
  COMMENT "Generating FlatBuffers header from ${FBS_SRC}"
)
add_custom_target(generate_fbs DEPENDS ${FBS_OUT})

# --- Requirement: ONNX Runtime ---
if(ANDROID)
    FetchContent_Declare(
      onnxruntime
      URL https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.17.1/onnxruntime-android-1.17.1.aar
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(ORT_LIB_PATH ${onnxruntime_SOURCE_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so)
    add_library(onnxruntime_lib SHARED IMPORTED)
    set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})
else()
    FetchContent_Declare(
      onnxruntime
      URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-1.17.1.tgz
    )
    FetchContent_MakeAvailable(onnxruntime)
    set(ORT_LIB_PATH ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so)
    add_library(onnxruntime_lib SHARED IMPORTED)
    set_target_properties(onnxruntime_lib PROPERTIES IMPORTED_LOCATION ${ORT_LIB_PATH})
endif()

# --- Core Ronin Logic ---
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
add_dependencies(ronin_core generate_fbs)
target_include_directories(ronin_core PUBLIC 
    include 
    ${sqlite3_src_SOURCE_DIR} 
    ${onnxruntime_SOURCE_DIR}/include
    ${flatbuffers_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(ronin_core PUBLIC sqlite3_lib)

# --- JNI Implementation ---
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    
    # MediaPipe LLM Inference JNI (Dynamic Resolution Phase 5.9)
    # We no longer link this at build-time to avoid stub/JNI symbol conflicts.
    # Implementation is resolved via dlopen/dlsym in inference_engine.cpp.

    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_include_directories(ronin_kernel PRIVATE include)
    
    target_link_libraries(ronin_kernel 
        PUBLIC 
        ronin_core 
        onnxruntime_lib 
        ${log-lib} 
        ${android-lib}
    )

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
    endif()
else()
    # Host build (Linux x64)
    target_link_libraries(ronin_core PUBLIC onnxruntime_lib)

    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_link_libraries(ronin_kernel PUBLIC ronin_core sqlite3_lib onnxruntime_lib)
endif()

# Optional: Host-side Unit Tests
if(NOT ANDROID)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)
    
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
