#pragma once
#include "Drawing.h"
#include <filesystem>
#include <memory>
#include <cstdint>

// ---------------------------------------------------------------------------
// Which renderer the drawing tests run through.
//
// The reference images are Direct2D output, so Windows renders natively and
// is the anchor: it re-proves the references still describe what Direct2D
// draws today. Everywhere else the tests use the SHARED CPU backend, which
// reproduces Direct2D's own rasterisation (gmpi_ui backends/CpuGfx.h plus
// helpers/CpuTextEngine.h) and so is measured against the same references
// rather than against a per-platform set. One set of references, one expected
// result, on every platform - which is the point.
//
// Set GMPI_UI_TESTS_NATIVE_BACKEND=1 (here, or -DGMPI_UI_TESTS_NATIVE_BACKEND=ON
// at configure time) to put macOS back on the Cocoa/CoreText backend. That is
// the pre-2026-08 behaviour, and it fails ~55 of the image tests, because
// CoreText's glyph rasterisation is not Direct2D's - keep it for diagnosing
// the Cocoa backend itself, not for a green run.
// ---------------------------------------------------------------------------
#ifndef GMPI_UI_TESTS_NATIVE_BACKEND
#define GMPI_UI_TESTS_NATIVE_BACKEND 0
#endif

// Resolved once, so the implementation files cannot disagree about which of
// them is the live one.
#if defined(_WIN32)
    #define GMPI_UI_TESTS_BACKEND_CPU 0                        // always Direct2D: it defines the references
#elif defined(__APPLE__)
    #define GMPI_UI_TESTS_BACKEND_CPU (!GMPI_UI_TESTS_NATIVE_BACKEND)
#else
    #define GMPI_UI_TESTS_BACKEND_CPU 1                        // no native backend on Linux
#endif

// Owns a platform-specific drawing backend and exposes only the
// gmpi::drawing API surface.  No backend headers (DirectXGfx.h,
// CocoaGfx.h) or platform namespaces leak into the caller.
class DrawingTestContext
{
public:
    DrawingTestContext();
    ~DrawingTestContext();

    // Non-copyable, moveable.
    DrawingTestContext(const DrawingTestContext&)            = delete;
    DrawingTestContext& operator=(const DrawingTestContext&) = delete;
    DrawingTestContext(DrawingTestContext&&)                 = default;
    DrawingTestContext& operator=(DrawingTestContext&&)      = default;

    gmpi::drawing::Factory& factory();

    // Creates a CPU-readable offscreen render target of the given pixel size.
    // flags are passed through to the backend (e.g. BitmapRenderTargetFlags).
    gmpi::drawing::BitmapRenderTarget createCpuRenderTarget(gmpi::drawing::SizeU size, int32_t flags = 0, float dpi = 96.0f);

#ifdef _WIN32
    // Creates a 128x20 sRGB gradient PNG where column x has grey value x (0..127).
    // Only writes the file if it does not already exist.
    static bool createSRGBGradientPng(const std::filesystem::path& path);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
