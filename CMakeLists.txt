cmake_minimum_required(VERSION 3.22)

# C/C++ consumers of the Rust core: builds the C ABI (bindings/c) through Corrosion, then the
# C ABI tests, the C++ binding tests and the C++ benchmark against it.

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml" WKP_CARGO_TOML)
string(REGEX MATCH "\\[workspace\\.package\\][^[]*version = \"([^\"]+)\"" WKP_VERSION_MATCH "${WKP_CARGO_TOML}")
if(NOT WKP_VERSION_MATCH)
    message(FATAL_ERROR "Failed to parse [workspace.package] version from Cargo.toml")
endif()
set(WKP_VERSION "${CMAKE_MATCH_1}")

project(wkp VERSION ${WKP_VERSION} LANGUAGES C CXX)

option(WKP_BUILD_TESTS "Build C/C++ tests" ON)
option(WKP_BUILD_BENCHMARKS "Build C++ benchmarks" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.1
)
FetchContent_MakeAvailable(Corrosion)

# Target `wkp`: the Rust static library plus whatever system libraries it needs.
corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    CRATES wkp-capi
    CRATE_TYPES staticlib
)
target_include_directories(wkp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/include)

if(WKP_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.3
    )
    FetchContent_MakeAvailable(doctest)
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

    function(wkp_add_test name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include)
        target_compile_definitions(${name} PRIVATE WKP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${name} PRIVATE wkp doctest::doctest)
        doctest_discover_tests(${name})
    endfunction()

    wkp_add_test(wkp_c_test_api bindings/c/tests/test_c_api.cpp)
    wkp_add_test(wkp_c_test_errors bindings/c/tests/test_c_api_errors.cpp)
    wkp_add_test(wkp_c_test_geometry bindings/c/tests/test_c_api_geometry.cpp)
    wkp_add_test(wkp_cpp_test_vectors bindings/cpp/tests/test_vectors.cpp)
    wkp_add_test(wkp_cpp_test_integration bindings/cpp/tests/test_integration_txt.cpp)
endif()

if(WKP_BUILD_BENCHMARKS)
    add_executable(wkp_cpp_benchmark bindings/cpp/bench/benchmark_cpp.cpp)
    target_include_directories(wkp_cpp_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp/include)
    target_link_libraries(wkp_cpp_benchmark PRIVATE wkp)
endif()
