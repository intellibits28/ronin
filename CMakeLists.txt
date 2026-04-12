cmake_minimum_required(VERSION 3.22.1)
project(RoninKernel VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
include_directories(${CMAKE_SOURCE_DIR}/generated)
include_directories(include)

# Core Logic Source (Portable)
set(RONIN_CORE_SOURCES
    src/intent_engine.cpp
    src/checkpoint_engine.cpp
    src/thompson_sampler.cpp
    src/graph_executor.cpp
    src/graph_storage.cpp
    src/memory_manager.cpp
    src/long_term_memory.cpp
    src/capability_graph.cpp
)

add_library(ronin_core STATIC ${RONIN_CORE_SOURCES})

# Platform Specific Builds
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    
    # JNI Bridge Library
    add_library(ronin_kernel SHARED src/ronin_jni.cpp)
    target_link_libraries(ronin_kernel ronin_core ${log-lib} ${android-lib} sqlite3)
    
    # Enable ARMv8.2-A Dot Product extension
    target_compile_options(ronin_core PRIVATE -march=armv8.2-a+dotprod)
else()
    # Host build needs rt and sqlite3 for the core
    target_link_libraries(ronin_core rt sqlite3)
endif()

# GoogleTest Integration
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Memory Stress Test
add_executable(ronin_memory_test tests/memory_stress_test.cpp)
target_link_libraries(ronin_memory_test gtest_main ronin_core)

# Precision Test
add_executable(ronin_precision_test tests/precision_test.cpp)
target_link_libraries(ronin_precision_test gtest_main ronin_core)

# Atomic Integrity Test
add_executable(ronin_atomic_test tests/atomic_integrity_test.cpp)
target_link_libraries(ronin_atomic_test gtest_main ronin_core)

if(NOT ANDROID)
    target_link_libraries(ronin_memory_test rt)
    target_link_libraries(ronin_precision_test rt)
    target_link_libraries(ronin_atomic_test rt)
endif()
