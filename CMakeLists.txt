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
target_include_directories(ronin_core PUBLIC include ${sqlite3_src_SOURCE_DIR} ${onnxruntime_SOURCE_DIR}/include)
target_link_libraries(ronin_core PUBLIC sqlite3_lib)

# --- JNI Implementation ---
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    
    # MediaPipe LLM Inference JNI (Expected from AAR or jniLibs)
    find_library(MP_JNI_LIB llm_inference_engine_jni)

    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_include_directories(ronin_kernel PRIVATE include)
    
    target_link_libraries(ronin_kernel 
        PUBLIC 
        ronin_core 
        onnxruntime_lib 
        ${log-lib} 
        ${android-lib}
    )
    
    if(MP_JNI_LIB)
        target_link_libraries(ronin_kernel PUBLIC ${MP_JNI_LIB})
        message(STATUS ">>> Found MediaPipe JNI: ${MP_JNI_LIB}")
    else()
        message(WARNING ">>> MediaPipe JNI NOT found in linker path. Runtime dlopen fallback may be needed.")
    endif()

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
    endif()
endif()
