cmake_minimum_required(VERSION 3.24...4.2)
project(spsc LANGUAGES CXX)

add_library(spsc INTERFACE)
add_library(spsc::spsc ALIAS spsc)

target_include_directories(spsc INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(spsc INTERFACE cxx_std_20)

option(SPSC_BUILD_TESTS "Build spsc tests" ${PROJECT_IS_TOP_LEVEL})
option(SPSC_BUILD_BENCHMARKS "Build spsc benchmarks" OFF)

set(SPSC_SANITIZERS "" CACHE STRING
        "Sanitizers for tests, e.g. \"address;undefined\" or \"thread\"")

if ("thread" IN_LIST SPSC_SANITIZERS AND "address" IN_LIST SPSC_SANITIZERS)
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
endif ()

if (SPSC_BUILD_TESTS OR SPSC_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    find_package(Threads REQUIRED)
endif ()

if (SPSC_BUILD_TESTS)
    enable_testing()

    add_executable(spsc_tests tests/spsc_test.cpp tests/lifetime_test.cpp tests/stress_test.cpp)
    set_target_properties(spsc_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(spsc_tests PRIVATE spsc::spsc GTest::gtest_main Threads::Threads)
    target_compile_options(spsc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

    if (SPSC_SANITIZERS)
        list(JOIN SPSC_SANITIZERS "," _spsc_sanitizers)
        set(_spsc_sanitizer_flags -fsanitize=${_spsc_sanitizers} -fno-omit-frame-pointer)
        if ("undefined" IN_LIST SPSC_SANITIZERS)
            list(APPEND _spsc_sanitizer_flags -fno-sanitize-recover=undefined)
        endif ()
        target_compile_options(spsc_tests PRIVATE ${_spsc_sanitizer_flags})
        target_link_options(spsc_tests PRIVATE ${_spsc_sanitizer_flags})

        if ("thread" IN_LIST SPSC_SANITIZERS)
            find_program(SPSC_SETARCH setarch)
            if (SPSC_SETARCH)
                if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
                    set(_spsc_launcher_property TEST_LAUNCHER)
                else ()
                    set(_spsc_launcher_property CROSSCOMPILING_EMULATOR)
                endif ()
                set_target_properties(spsc_tests PROPERTIES
                        ${_spsc_launcher_property} "${SPSC_SETARCH};${CMAKE_SYSTEM_PROCESSOR};-R")
            endif ()
        endif ()
    endif ()

    include(GoogleTest)
    gtest_discover_tests(spsc_tests)
endif ()

if (SPSC_BUILD_BENCHMARKS)
    if (NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
        message(WARNING "Benchmarks are built with CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'; use Release for meaningful numbers")
    endif ()

    add_executable(spsc_benchmarks tests/benchmark_test.cpp)
    set_target_properties(spsc_benchmarks PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(spsc_benchmarks PRIVATE spsc::spsc GTest::gtest_main Threads::Threads)
    target_compile_options(spsc_benchmarks PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif ()