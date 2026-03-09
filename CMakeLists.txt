cmake_minimum_required(VERSION 3.18)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/core/include/wkp/_version.h" WKP_VERSION_HEADER)
string(REGEX MATCH "#define[ \t]+WKP_CORE_VERSION[ \t]+\"([^\"]+)\"" WKP_VERSION_MATCH "${WKP_VERSION_HEADER}")
if(NOT WKP_VERSION_MATCH)
    message(FATAL_ERROR "Failed to parse WKP_CORE_VERSION from core/include/wkp/_version.h")
endif()
set(WKP_CORE_VERSION "${CMAKE_MATCH_1}")

project(wkp_native VERSION ${WKP_CORE_VERSION} LANGUAGES CXX)

option(WKP_BUILD_TESTS "Build C++ tests" ON)
option(WKP_BUILD_BENCHMARKS "Build C++ benchmarks" ON)
option(WKP_BUILD_PYTHON_BINDINGS "Build Python nanobind module + generate stubs" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wkp_core_static STATIC
    core/src/core.cpp
)

target_include_directories(wkp_core_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

add_library(wkp_core_shared SHARED
    core/src/core.cpp
)

target_include_directories(wkp_core_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

set_target_properties(wkp_core_shared PROPERTIES OUTPUT_NAME wkp_core)
set_target_properties(wkp_core_shared PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(WIN32)
    set_target_properties(wkp_core_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(MSVC)
    target_compile_options(wkp_core_static PRIVATE /W4 /permissive-)
    target_compile_options(wkp_core_shared PRIVATE /W4 /permissive-)
else()
    target_compile_options(wkp_core_static PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(wkp_core_shared PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WKP_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
    )
    FetchContent_MakeAvailable(doctest)
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

    add_executable(wkp_cpp_test_vectors
        bindings/cpp/tests/test_vectors.cpp
    )
    target_include_directories(wkp_cpp_test_vectors PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include
    )
    target_link_libraries(wkp_cpp_test_vectors PRIVATE wkp_core_static doctest::doctest)
    doctest_discover_tests(wkp_cpp_test_vectors)

    add_executable(wkp_core_test_capi
        core/tests/test_c_api.cpp
    )
    target_link_libraries(wkp_core_test_capi PRIVATE wkp_core_static doctest::doctest)
    doctest_discover_tests(wkp_core_test_capi)

    add_executable(wkp_cpp_test_errors
        bindings/cpp/tests/test_errors.cpp
    )
    target_include_directories(wkp_cpp_test_errors PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include
    )
    target_link_libraries(wkp_cpp_test_errors PRIVATE wkp_core_static doctest::doctest)
    doctest_discover_tests(wkp_cpp_test_errors)

    add_executable(wkp_cpp_test_geometry
        bindings/cpp/tests/test_geometry_encoder.cpp
    )
    target_include_directories(wkp_cpp_test_geometry PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include
    )
    target_link_libraries(wkp_cpp_test_geometry PRIVATE wkp_core_static doctest::doctest)
    doctest_discover_tests(wkp_cpp_test_geometry)
endif()

if(WKP_BUILD_BENCHMARKS)
    add_executable(wkp_cpp_benchmark
        bindings/cpp/bench/benchmark_cpp.cpp
    )
    target_include_directories(wkp_cpp_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include
    )
    target_link_libraries(wkp_cpp_benchmark PRIVATE wkp_core_static)
endif()

if(WKP_BUILD_PYTHON_BINDINGS)
    include(FetchContent)
    FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind.git
        GIT_TAG v2.12.0
    )
    FetchContent_MakeAvailable(nanobind)

    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

    nanobind_add_module(wkp_python_core
        bindings/python/src/wkp/core_nb.cpp
        core/src/core.cpp
    )

    target_include_directories(wkp_python_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
    )

    set_target_properties(wkp_python_core PROPERTIES
        OUTPUT_NAME "_core"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp"
    )

    nanobind_add_stub(
        wkp_python_core_stub
        MODULE wkp._core
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp/_core.pyi
        PYTHON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src
        DEPENDS wkp_python_core
        MARKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/src/wkp/py.typed
    )
endif()
