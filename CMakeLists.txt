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
if(APPLE)
    add_executable(gmpi_ui_tests
        tests/drawing_tests.cpp
        tests/text_tests.cpp
        tests/DrawingTestContext.mm
        ${GMPI_UI_SDK}/backends/DirectXGfx.cpp
    )
else()
    add_executable(gmpi_ui_tests
        tests/drawing_tests.cpp
        tests/text_tests.cpp
        tests/DrawingTestContext.cpp
        ${GMPI_UI_SDK}/backends/DirectXGfx.cpp
    )
endif()

target_include_directories(gmpi_ui_tests PRIVATE
    ${GMPI_UI_SDK}
)

# Pass the absolute path to reference images as a compile-time string.
# Use forward slashes so it works as a C string literal on Windows too.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/reference_images" REF_IMAGES_PATH)
target_compile_definitions(gmpi_ui_tests PRIVATE
    REFERENCE_IMAGES_DIR="${REF_IMAGES_PATH}"
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
        "-framework Cocoa"
        "-framework CoreGraphics"
        "-framework ImageIO"
        "-framework CoreText"
    )
else()
    target_link_libraries(gmpi_ui_tests PRIVATE
        gtest_main
        d2d1
        dwrite
        WindowsCodecs
        d3d11
        dxgi
    )
endif()

enable_testing()
include(GoogleTest)
gtest_discover_tests(gmpi_ui_tests)
