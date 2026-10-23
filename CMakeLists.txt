cmake_minimum_required(VERSION 3.25)

project(
    MatchingEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

# We explicitly choose C++20 so the codebase has a stable,
# well-defined language baseline across Windows and Linux.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------
# Matching engine library
# ------------------------------------------------------------

add_library(matching_engine
    src/order_book.cpp
)

target_include_directories(matching_engine
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

# Keep warnings enabled for the entire project.
if(MSVC)
    target_compile_options(matching_engine PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(matching_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )

    # Prevent GoogleTest from overriding our compiler/runtime settings.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    add_executable(order_tests
        tests/order_tests.cpp
        tests/order_book_tests.cpp
    )

    target_link_libraries(order_tests
        PRIVATE
            matching_engine
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(order_tests)
endif()

# ------------------------------------------------------------
# Benchmarks
# ------------------------------------------------------------

option(BUILD_BENCHMARKS "Build benchmarks" ON)

if(BUILD_BENCHMARKS)

    include(FetchContent)

    FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
    )

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(order_benchmark
        benchmarks/order_benchmark.cpp
        
    )
    add_executable(order_book_benchmark
        benchmarks/order_book_benchmark.cpp
    )

    add_executable(order_book_latency
        benchmarks/order_book_latency.cpp
    )

    target_link_libraries(order_benchmark
        PRIVATE
            matching_engine
            benchmark::benchmark
    )
    target_link_libraries(order_book_benchmark
        PRIVATE
            matching_engine
            benchmark::benchmark
    )
    target_link_libraries(order_book_latency
        PRIVATE
            matching_engine
            benchmark::benchmark
    )

endif()