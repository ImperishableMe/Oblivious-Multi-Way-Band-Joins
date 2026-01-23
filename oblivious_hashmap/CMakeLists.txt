cmake_minimum_required(VERSION 3.16)
project(ObliviousHashmap
    VERSION 1.0
    DESCRIPTION "Oblivious Hashmap - Extracted from H2O2RAM"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O1")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DPARALLEL -s -fopenmp")

# Check for AVX-512 support and add flags if available
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)
if(COMPILER_SUPPORTS_AVX512)
    message(STATUS "AVX-512 supported - enabling optimizations")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mavx -mavx512f -mavx512cd -mavx512vl -mavx512bw -mavx512dq -mbmi2 -march=native")
else()
    message(STATUS "AVX-512 not supported - using SSE2/AVX fallback")
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mavx2 -march=native")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -msse2 -march=native")
    endif()
endif()

add_compile_options(-Wno-deprecated-declarations)

# Apply flags based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Adding Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Adding Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Required dependencies
find_package(TBB REQUIRED)
if(TBB_FOUND)
    message(STATUS "TBB found")
else()
    message(FATAL_ERROR "TBB not found. Install with: sudo apt-get install libtbb-dev")
endif()

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found")
else()
    message(WARNING "OpenMP not found. Install with: sudo apt-get install libomp-dev")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(NLopt REQUIRED)
if(NLopt_FOUND)
    message(STATUS "NLopt found")
else()
    message(FATAL_ERROR "NLopt not found. Install with: sudo apt-get install libnlopt-dev")
endif()

# Include directories
include_directories(include)

# Library sources
file(GLOB LIB_SOURCES "src/*.cpp")
add_library(ObliviousHashmapLib ${LIB_SOURCES})
target_link_libraries(ObliviousHashmapLib
    TBB::tbb
    Threads::Threads
    crypto
    nlopt)

if(OpenMP_CXX_FOUND)
    target_link_libraries(ObliviousHashmapLib OpenMP::OpenMP_CXX)
endif()

# Example executable
add_executable(oblivious_hashmap_example examples/main.cpp)
target_link_libraries(oblivious_hashmap_example ObliviousHashmapLib)

# Tests (optional)
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "GTest found - building tests")
        enable_testing()
        include_directories(${GTEST_INCLUDE_DIRS})

        file(GLOB TEST_SOURCES "tests/*.cpp")
        add_executable(oblivious_hashmap_test ${TEST_SOURCES})
        target_link_libraries(oblivious_hashmap_test
            ObliviousHashmapLib
            GTest::gtest_main)

        include(GoogleTest)
        gtest_discover_tests(oblivious_hashmap_test)
    else()
        message(STATUS "GTest not found - skipping tests. Install with: sudo apt-get install libgtest-dev")
    endif()
endif()

# Installation
install(TARGETS ObliviousHashmapLib
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include/oblivious_hashmap)
