FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
    SYSTEM
)

set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

file(
    GLOB_RECURSE _test_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*_test.cpp"
)
if(_test_sources)
    add_executable(${PROJECT_NAME}_tests ${_test_sources})
    target_link_libraries(
        ${PROJECT_NAME}_tests
        PRIVATE ${PROJECT_NAME}_lib GTest::gtest_main
    )
    gtest_discover_tests(${PROJECT_NAME}_tests DISCOVERY_MODE PRE_TEST)
endif()
