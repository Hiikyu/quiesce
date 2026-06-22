file(GLOB_RECURSE libquiesce_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)
add_library(libquiesce STATIC ${libquiesce_sources})

target_include_directories(libquiesce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)

if(QUIESCE_TESTS_USE_TSAN)
    target_compile_options(libquiesce PRIVATE -fsanitize=thread -O1 -g)
    target_link_options(libquiesce PRIVATE -fsanitize=thread)
elseif(QUIESCE_TESTS_USE_ASAN)
    target_compile_options(libquiesce PRIVATE -fsanitize=address,undefined -O1 -g)
    target_link_options(libquiesce PRIVATE -fsanitize=address,undefined)
endif()

if(QUIESCE_ENABLE_TESTS)
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

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cc")

    add_executable(
        quiesce_test
        ${test_sources}
    )
    target_link_libraries(
        quiesce_test
        PRIVATE libquiesce
        PRIVATE GTest::gtest_main
    )

    if(QUIESCE_TESTS_USE_TSAN)
        target_compile_options(quiesce_test PRIVATE -fsanitize=thread -O1 -g)
        target_link_options(quiesce_test PRIVATE -fsanitize=thread)
    elseif(QUIESCE_TESTS_USE_ASAN)
        target_compile_options(quiesce_test PRIVATE -fsanitize=address,undefined -O1 -g)
        target_link_options(quiesce_test PRIVATE -fsanitize=address,undefined)
    else()
        target_compile_options(quiesce_test PRIVATE -O2 -g -Wall -Wextra)
    endif()

    include(GoogleTest)
    gtest_discover_tests(quiesce_test)
endif()
