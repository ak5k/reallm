include_guard(GLOBAL)

option(CLANG_TIDY_ENABLE "Enable clang-tidy integration for project targets" ON)
set(CLANG_TIDY_PATH "" CACHE FILEPATH "Absolute path to clang-tidy executable")

function(_resolve_clang_tidy out_var)
    set(_clang_tidy_executable "")

    if(CLANG_TIDY_PATH)
        set(_clang_tidy_executable "${CLANG_TIDY_PATH}")
    else()
        find_program(_brew_executable NAMES brew)
        if(_brew_executable)
            execute_process(
                COMMAND "${_brew_executable}" --prefix llvm
                OUTPUT_VARIABLE _brew_llvm_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _brew_result
            )
            if(
                _brew_result EQUAL 0
                AND EXISTS "${_brew_llvm_prefix}/bin/clang-tidy"
            )
                set(_clang_tidy_executable
                    "${_brew_llvm_prefix}/bin/clang-tidy"
                )
            endif()
        endif()

        if(NOT _clang_tidy_executable)
            find_program(_clang_tidy_executable NAMES clang-tidy)
        endif()

        if(NOT _clang_tidy_executable)
            find_program(
                _clang_tidy_executable
                NAMES clang-tidy
                HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
            )
        endif()
    endif()

    set(${out_var} "${_clang_tidy_executable}" PARENT_SCOPE)
endfunction()

function(enable_clang_tidy_for_targets)
    if(NOT CLANG_TIDY_ENABLE)
        return()
    endif()

    if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
        message(STATUS "Skipping clang-tidy for universal macOS builds")
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "Skipping clang-tidy when using MSVC compiler")
        return()
    endif()

    _resolve_clang_tidy(_clang_tidy_executable)
    if(NOT _clang_tidy_executable)
        message(STATUS "clang-tidy not found; skipping clang-tidy integration")
        return()
    endif()

    set(_clang_tidy_command "${_clang_tidy_executable}")
    if(WIN32)
        # MSVC release flags (for example /GL) are not understood by clang-tidy's
        # compile simulation and can be reported as unused command-line arguments.
        string(
            APPEND _clang_tidy_command
            ";--extra-arg=-Wno-unused-command-line-argument"
        )
    endif()

    foreach(target_name IN LISTS ARGN)
        if(TARGET ${target_name})
            set_target_properties(
                ${target_name}
                PROPERTIES CXX_CLANG_TIDY "${_clang_tidy_command}"
            )
        endif()
    endforeach()
endfunction()

if(PROJECT_NAME)
    cmake_language(
        DEFER
        CALL enable_clang_tidy_for_targets
        "${PROJECT_NAME}_lib"
        "${PROJECT_NAME}"
        "${PROJECT_NAME}_tests"
    )
endif()
