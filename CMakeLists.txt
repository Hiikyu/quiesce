cmake_minimum_required(VERSION 4.1)

project(quiesce)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# If vcpkg is available via VCPKG_ROOT, use it automatically for package discovery.
## Configure vcpkg toolchain location
# Preferred default: an out/vcpkg_installed folder inside the repository output tree.
# Users can still override by setting the VCPKG_ROOT environment variable or
# passing -DCMAKE_TOOLCHAIN_FILE explicitly to cmake.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file" FORCE)
    else()
        # Default to an out/vcpkg_installed sibling of typical out/build directories
        set(default_vcpkg_dir "${CMAKE_SOURCE_DIR}/out/vcpkg_installed")
        # Expose as ENV so helper scripts and tools that read VCPKG_ROOT pick it up
        set(ENV{VCPKG_ROOT} "${default_vcpkg_dir}")
        set(CMAKE_TOOLCHAIN_FILE "${default_vcpkg_dir}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file" FORCE)
    endif()
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
