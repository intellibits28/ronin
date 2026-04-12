cmake_minimum_required(VERSION 3.22.1)
project(RoninKernel VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
include_directories(include)

# Kernel Source
add_library(ronin_kernel SHARED
    src/ronin_jni.cpp
    src/intent_engine.cpp
    src/checkpoint_engine.cpp
    src/thompson_sampler.cpp
    src/graph_executor.cpp
    src/graph_storage.cpp
    src/memory_manager.cpp
    src/long_term_memory.cpp
)

# Platform Libraries
if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    target_link_libraries(ronin_kernel ${log-lib} ${android-lib} sqlite3)
else()
    # Host build needs rt and sqlite3
    target_link_libraries(ronin_kernel rt sqlite3)
endif()

# GoogleTest Integration
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee9d7941fa904a088863f664326f183c.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Memory Stress Test
add_executable(ronin_memory_test tests/memory_stress_test.cpp src/memory_manager.cpp)
target_link_libraries(ronin_memory_test gtest_main)

# Precision Test
add_executable(ronin_precision_test tests/precision_test.cpp src/thompson_sampler.cpp)
target_link_libraries(ronin_precision_test gtest_main)

# Atomic Integrity Test
add_executable(ronin_atomic_test tests/atomic_integrity_test.cpp src/checkpoint_engine.cpp)
target_link_libraries(ronin_atomic_test gtest_main)

if(NOT ANDROID)
    target_link_libraries(ronin_memory_test rt)
    target_link_libraries(ronin_precision_test rt)
    target_link_libraries(ronin_atomic_test rt)
endif()
