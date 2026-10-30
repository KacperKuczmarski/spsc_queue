cmake_minimum_required(VERSION 3.24...4.2)
project(spsc LANGUAGES CXX)

add_library(spsc INTERFACE)
add_library(spsc::spsc ALIAS spsc)

target_include_directories(spsc INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_compile_features(spsc INTERFACE cxx_std_20)

option(SPSC_BUILD_TESTS "Build spsc tests" ${PROJECT_IS_TOP_LEVEL})
set(SPSC_SANITIZERS "" CACHE STRING
        "Sanitizers for tests, e.g. \"address;undefined\" or \"thread\"")

if ("thread" IN_LIST SPSC_SANITIZERS AND "address" IN_LIST SPSC_SANITIZERS)
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
endif ()

if (SPSC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    find_package(Threads REQUIRED)

    add_executable(spsc_tests tests/spsc_test.cpp tests/lifetime_test.cpp tests/stress_test.cpp)
    target_link_libraries(spsc_tests PRIVATE spsc::spsc GTest::gtest_main Threads::Threads)

    if (SPSC_SANITIZERS)
        list(JOIN SPSC_SANITIZERS "," _spsc_sanitizers)
        set(_spsc_sanitizer_flags -fsanitize=${_spsc_sanitizers} -fno-omit-frame-pointer)
        if ("undefined" IN_LIST SPSC_SANITIZERS)
            list(APPEND _spsc_sanitizer_flags -fno-sanitize-recover=undefined)
        endif ()
        target_compile_options(spsc_tests PRIVATE ${_spsc_sanitizer_flags})
        target_link_options(spsc_tests PRIVATE ${_spsc_sanitizer_flags})

        # Older TSan runtimes abort with "unexpected memory mapping" on kernels
        # with high vm.mmap_rnd_bits; running without ASLR avoids it.
        if ("thread" IN_LIST SPSC_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(SPSC_SETARCH setarch)
            if (SPSC_SETARCH)
                set_target_properties(spsc_tests PROPERTIES
                        CROSSCOMPILING_EMULATOR "${SPSC_SETARCH};${CMAKE_SYSTEM_PROCESSOR};-R")
            endif ()
        endif ()
    endif ()

    include(GoogleTest)
    gtest_discover_tests(spsc_tests)
endif ()