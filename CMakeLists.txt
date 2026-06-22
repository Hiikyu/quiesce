cmake_minimum_required(VERSION 4.1)

project(quiesce)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# If vcpkg is available via VCPKG_ROOT, use it automatically for package discovery.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file" FORCE)
endif()

option(QUIESCE_ENABLE_TESTS "Build tests for libquiesce" ON)
option(QUIESCE_TESTS_USE_TSAN "Build with ThreadSanitizer" OFF)
option(QUIESCE_TESTS_USE_ASAN "Build with AddressSanitizer + UBSan" OFF)
option(QUIESCE_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

if(QUIESCE_ENABLE_TESTS)
    enable_testing()
endif()

if(QUIESCE_ENABLE_COVERAGE AND (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang"))
    message(STATUS "Building with coverage instrumentation")
    add_compile_options(--coverage -g)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
endif()

add_subdirectory("src")
