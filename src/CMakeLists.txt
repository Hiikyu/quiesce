file(GLOB libquiesce_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")
add_library(libquiesce STATIC ${libquiesce_sources})

# Associates headers explicitly and manages propagation automatically
target_sources(libquiesce 
    PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include  # Absolute path to the include folder
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/maths.h
)

target_include_directories(libquiesce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if (QUIESCE_ENABLE_TESTS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    file(GLOB test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cc")

    add_executable(
        maths_test
        ${test_sources}
    )
    target_link_libraries(
        maths_test
        PRIVATE libquiesce
        PRIVATE GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(maths_test)
endif()