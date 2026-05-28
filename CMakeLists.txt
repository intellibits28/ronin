cmake_minimum_required(VERSION 3.22.1)
project(ronin_kernel)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Global Configuration for Dependencies (Single Model Hardening)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Requirement: SQLite (FTS5 Enabled) ---
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

# --- Core Ronin Logic (Single Model Architecture) ---
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
    src/capabilities/hardware_nodes.cpp
    src/capabilities/hardware_bridge.cpp
    src/capabilities/chat_skill.cpp
    src/myanmar_segmenter.cpp
)

add_library(ronin_core STATIC ${RONIN_CORE_SOURCES})
add_dependencies(ronin_core generate_fbs)

target_include_directories(ronin_core PUBLIC 
    include 
    ${sqlite3_src_SOURCE_DIR} 
    ${flatbuffers_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(ronin_core PUBLIC sqlite3_lib)

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

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
    endif()
else()
    add_library(ronin_kernel SHARED src/ronin_jni.cpp src/jni_utils.cpp)
    target_link_libraries(ronin_kernel PUBLIC ronin_core sqlite3_lib)
endif()

# Host-side Unit Tests
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

    add_executable(ronin_integration_test tests/phase_1_3_integration_test.cpp)
    target_link_libraries(ronin_integration_test ronin_core gtest_main)
endif()
