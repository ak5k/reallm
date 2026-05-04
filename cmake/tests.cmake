FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
    SYSTEM
)

set(INSTALL_GTEST OFF)
if(WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)
if(WIN32)
    foreach(_t IN ITEMS gtest gtest_main gmock gmock_main)
        if(TARGET ${_t})
            set_property(
                TARGET ${_t}
                PROPERTY
                    MSVC_RUNTIME_LIBRARY
                        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
            )
        endif()
    endforeach()
endif()

enable_testing()
include(GoogleTest)

file(
    GLOB_RECURSE _test_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*_test.cpp"
)
if(_test_sources)
    add_executable(${PROJECT_NAME}_tests ${_test_sources})
    if(WIN32)
        set_property(
            TARGET ${PROJECT_NAME}_tests
            PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
    endif()
    target_link_libraries(
        ${PROJECT_NAME}_tests
        PRIVATE ${PROJECT_NAME}_lib GTest::gtest_main
    )
    gtest_discover_tests(${PROJECT_NAME}_tests DISCOVERY_MODE PRE_TEST)
endif()
