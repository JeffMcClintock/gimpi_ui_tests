#pragma once
#include "Drawing.h"
#include <filesystem>
#include <memory>
#include <cstdint>

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
    gmpi::drawing::BitmapRenderTarget createCpuRenderTarget(gmpi::drawing::SizeU size, int32_t flags = 0);

#ifdef _WIN32
    // Creates a 128x20 sRGB gradient PNG where column x has grey value x (0..127).
    // Only writes the file if it does not already exist.
    static bool createSRGBGradientPng(const std::filesystem::path& path);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
