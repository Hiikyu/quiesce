cmake_minimum_required(VERSION 4.1)

project(quiesce)

# If vcpkg is available via VCPKG_ROOT, use it automatically for package discovery.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file" FORCE)
endif()

option(QUIESCE_ENABLE_TESTS "Build tests for libquiesce" ON)

if(QUIESCE_ENABLE_TESTS)
    enable_testing()
endif()

add_subdirectory("src")
