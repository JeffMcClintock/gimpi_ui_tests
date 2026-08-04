cmake_minimum_required(VERSION 3.15)
project(gmpi_ui_tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Local override: set GMPI_UI_FOLDER_OVERRIDE to your local gmpi_ui clone
# to skip the GitHub fetch (e.g. -DGMPI_UI_FOLDER_OVERRIDE=C:/SE/gmpi_ui)
# ---------------------------------------------------------------------------
set(GMPI_UI_FOLDER_OVERRIDE "" CACHE PATH
    "Path to a local gmpi_ui repo, or blank to fetch from GitHub.")
set(GMPI_SDK_FOLDER_OVERRIDE "" CACHE PATH
    "Path to a local GMPI repo, or blank to fetch from GitHub.")

if("${GMPI_UI_FOLDER_OVERRIDE}" STREQUAL "")
    message(STATUS "Fetching gmpi_ui from GitHub")
    # SOURCE_SUBDIR points to a directory with no CMakeLists.txt so that
    # FetchContent does not attempt add_subdirectory on the repo root.
    # Requires CMake >= 3.28 for this to work silently.
    FetchContent_Declare(
        gmpi_ui
        GIT_REPOSITORY https://github.com/JeffMcClintock/gmpi_ui
        GIT_TAG        origin/main
        SOURCE_SUBDIR  helpers
    )
    FetchContent_MakeAvailable(gmpi_ui)
    set(GMPI_UI_SDK ${gmpi_ui_SOURCE_DIR})
else()
    message(STATUS "Using local gmpi_ui: ${GMPI_UI_FOLDER_OVERRIDE}")
    set(GMPI_UI_SDK ${GMPI_UI_FOLDER_OVERRIDE})
endif()

if("${GMPI_SDK_FOLDER_OVERRIDE}" STREQUAL "")
    message(STATUS "Fetching GMPI from GitHub")
    FetchContent_Declare(
        gmpi
        GIT_REPOSITORY https://github.com/JeffMcClintock/GMPI
        GIT_TAG        origin/main
        SOURCE_SUBDIR  Core
    )
    FetchContent_MakeAvailable(gmpi)
    set(GMPI_SDK ${gmpi_SOURCE_DIR})
else()
    message(STATUS "Using local GMPI: ${GMPI_SDK_FOLDER_OVERRIDE}")
    set(GMPI_SDK ${GMPI_SDK_FOLDER_OVERRIDE})
endif()

# ---------------------------------------------------------------------------
# HarfBuzz — shaping plus glyph outlines plus colour-glyph access, which is
# what CJK and emoji need. Built the way JUCE builds it: one amalgamation
# source file, no HarfBuzz build system involved. A shipping product vendors
# these sources; fetching them here keeps the test build reproducible.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz
    GIT_TAG        14.2.1
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  do-not-configure   # we compile the amalgamation ourselves
)
FetchContent_MakeAvailable(harfbuzz)

add_library(gmpi_harfbuzz STATIC ${harfbuzz_SOURCE_DIR}/src/harfbuzz.cc)
target_include_directories(gmpi_harfbuzz PUBLIC ${harfbuzz_SOURCE_DIR}/src)
target_compile_definitions(gmpi_harfbuzz PUBLIC HB_NO_MT)
set_target_properties(gmpi_harfbuzz PROPERTIES CXX_STANDARD 17)
if(MSVC)
    target_compile_options(gmpi_harfbuzz PRIVATE /wd4244 /wd4267 /wd4996 /bigobj)
endif()

# ---------------------------------------------------------------------------
# GoogleTest
# ---------------------------------------------------------------------------
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
# Which renderer the tests use. OFF (default) puts macOS on the shared CPU
# backend, so the Direct2D reference images describe the expected result on
# every platform; ON restores the Cocoa backend there. Windows always renders
# natively — it is the anchor that re-proves the references. See
# tests/DrawingTestContext.h for the full reasoning.
option(GMPI_UI_TESTS_NATIVE_BACKEND
    "Render the drawing tests through each platform's native backend (Cocoa on macOS) instead of the shared CPU backend"
    OFF)

if(APPLE)
    # BOTH are compiled; the macro decides which one has a body, so flipping it
    # needs no CMake change. CoreText and ImageIO supply the CPU backend's font
    # discovery and image decoding, and are linked below either way.
    set(DRAWING_TEST_CONTEXT_SRC
        tests/DrawingTestContext.mm
        tests/DrawingTestContext_cpu.cpp)
elseif(WIN32)
    set(DRAWING_TEST_CONTEXT_SRC tests/DrawingTestContext.cpp)
else()
    # Linux: render through gmpi_ui's pure-software backend (backends/CpuGfx.h).
    # No GPU, no display server and no JUCE — just fontconfig for font discovery
    # and libpng for image codecs, both of which are present on any desktop
    # distro and installable headless.
    set(DRAWING_TEST_CONTEXT_SRC tests/DrawingTestContext_cpu.cpp)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    pkg_check_modules(LIBPNG REQUIRED IMPORTED_TARGET libpng)
endif()

add_executable(gmpi_ui_tests
    tests/drawing_tests.cpp
    tests/text_tests.cpp
    tests/layout_tests.cpp
    tests/cpu_backend_tests.cpp
    tests/cpu_vs_d2d_tests.cpp
    tests/cpu_text_tests.cpp
    tests/drawing_math_tests.cpp
    tests/cpu_present_tests.cpp
    ${DRAWING_TEST_CONTEXT_SRC}
    ${GMPI_UI_SDK}/backends/DirectXGfx.cpp
    ${GMPI_UI_SDK}/experimental/grid.cpp
    ${GMPI_UI_SDK}/experimental/primatives.cpp
    ${GMPI_UI_SDK}/experimental/ui_facades.cpp
)

target_include_directories(gmpi_ui_tests PRIVATE
    ${GMPI_UI_SDK}
    ${GMPI_SDK}
)

# Add headers so they appear in the Xcode project navigator and are searchable.
file(GLOB GMPI_UI_HEADERS
    ${GMPI_UI_SDK}/*.h
    ${GMPI_UI_SDK}/backends/*.h
    ${GMPI_UI_SDK}/helpers/*.h
    ${GMPI_UI_SDK}/experimental/*.h
)
target_sources(gmpi_ui_tests PRIVATE
    tests/DrawingTestFixture.h
    tests/DrawingTestContext.h
    ${GMPI_UI_HEADERS}
)

# Pass the absolute path to reference images as a compile-time string.
# Use forward slashes so it works as a C string literal on Windows too.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/reference_images" REF_IMAGES_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fonts" FONTS_PATH)
target_compile_definitions(gmpi_ui_tests PRIVATE
    REFERENCE_IMAGES_DIR="${REF_IMAGES_PATH}"
    FONTS_DIR="${FONTS_PATH}"
    GMPI_UI_TESTS_NATIVE_BACKEND=$<BOOL:${GMPI_UI_TESTS_NATIVE_BACKEND}>
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(gmpi_ui_tests PRIVATE /W3 /WX-)
endif()

# ---------------------------------------------------------------------------
# Platform-specific configuration
# ---------------------------------------------------------------------------
if(APPLE)
    target_link_libraries(gmpi_ui_tests PRIVATE
        gtest_main
        gmpi_harfbuzz   # tests/cpu_text_tests.cpp shapes with it directly
        "-framework Cocoa"
        "-framework CoreGraphics"
        "-framework ImageIO"
        "-framework CoreText"
    )
elseif(WIN32)
    target_link_libraries(gmpi_ui_tests PRIVATE
        gtest_main
        gmpi_harfbuzz
        d2d1
        dwrite
        WindowsCodecs
        d3d11
        dxgi
    )
else()
    target_link_libraries(gmpi_ui_tests PRIVATE
        gtest_main
        gmpi_harfbuzz
        PkgConfig::FONTCONFIG
        PkgConfig::LIBPNG
    )
endif()

enable_testing()
include(GoogleTest)
gtest_discover_tests(gmpi_ui_tests)
