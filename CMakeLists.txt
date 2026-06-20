cmake_minimum_required(VERSION 4.1)

project(quiesce)

# If vcpkg is available via VCPKG_ROOT, use it automatically for package discovery.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file" FORCE)
endif()

option(QUIESCE_ENABLE_TESTS "Build tests for libquiesce" ON)
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
