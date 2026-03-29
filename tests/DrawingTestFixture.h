// Shared test fixture for GMPI-UI pixel-comparison tests.
// Each test renders to a CPU-readable offscreen bitmap and compares
// pixel-for-pixel against a reference PNG stored in reference_images/.
//
// WORKFLOW
// --------
//  1. First run: no reference images exist, so each test FAILS and writes its
//     output bitmap to reference_images/<testName>.png.
//  2. Inspect the generated images and verify they look correct.
//  3. Re-run: tests compare against the saved references and PASS.
//  4. On future regression: the test FAILs and writes
//     reference_images/<testName>_actual.png alongside a diff log.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "Drawing.h"
#include "helpers/SavePng.h"
#include "DrawingTestContext.h"

using namespace gmpi::drawing;
using namespace gmpi::drawing::Colors;


// ============================================================
// DrawingTest fixture
// ============================================================
class DrawingTest : public ::testing::Test
{
protected:
    static constexpr uint32_t kWidth  = 64;
    static constexpr uint32_t kHeight = 64;

    // No format flag → bitmap is 64bppPRGBAHalf (8 bytes/pixel) on Windows,
    // 128bpp float on macOS. Matches the float-precision swapchain used on screen,
    // giving accurate colour round-trips with no 8-bit linear quantisation.
    static constexpr int32_t kRenderFlags = 0;

    DrawingTestContext drawingContext;
    gmpi::drawing::BitmapRenderTarget g;
    bool drawingActive = false;

    static std::filesystem::path referenceDir()
    {
        return REFERENCE_IMAGES_DIR;
    }

    void SetUp() override
    {
        g = drawingContext.createCpuRenderTarget({ kWidth, kHeight }, kRenderFlags);
        g.beginDraw();
        drawingActive = true;
        g.clear(Colors::White);
    }

    // Create a StrokeStyle via the render target's factory wrapper.
    StrokeStyle makeStrokeStyle(StrokeStyleProperties props,
                                std::span<const float> dashes = {})
    {
        return g.getFactory().createStrokeStyle(props, dashes);
    }

    // Create a TextFormat via the wrapper factory.
    // Defaults to Arial so results are consistent across Windows machines.
    TextFormat makeTextFormat(
        float height,
        const char*        family  = "Arial",
        FontWeight         weight  = FontWeight::Regular,
        FontStyle          style   = FontStyle::Normal,
        FontStretch        stretch = FontStretch::Normal)
    {
        std::string_view familySv{family};
        return drawingContext.factory().createTextFormat(height, {&familySv, 1}, weight, style, stretch);
    }

    // Build an 8x8 checkerboard bitmap using a small render target, then
    // wrap it in a BitmapBrush tied to the main render target (rt).
    BitmapBrush makeCheckerboardBrush(Color color1 = Colors::Red,
                                      Color color2 = Colors::White)
    {
        constexpr uint32_t kPat = 8;
        auto patRT = drawingContext.factory().createCpuRenderTarget({kPat, kPat}, kRenderFlags);

        patRT.beginDraw();
        patRT.clear(color1);
        auto b2 = patRT.createSolidColorBrush(color2);
        for (uint32_t y = 0; y < kPat; ++y)
            for (uint32_t x = y % 2; x < kPat; x += 2)
                patRT.fillRectangle({float(x), float(y), float(x+1), float(y+1)}, b2);
        patRT.endDraw();
        auto patternBitmap = patRT.getBitmap();
        return g.createBitmapBrush(patternBitmap);
    }

    void TearDown() override
    {
        if (drawingActive)
            g.endDraw();
        // Release render target before the factory (DrawingTestContext dtor) runs.
        g = {};
    }

    // Core comparison helper — works with any already-ended BitmapRenderTarget.
    // tolerance:       per-channel difference allowed before a pixel counts as differing.
    // maxMeanDiff:     maximum allowed mean per-channel diff across DIFFERING pixels only
    //                  (image-size independent: same rendering quality → same score regardless of bitmap dimensions).
    // maxDiffPercent:  maximum allowed percentage of differing pixels (0–100). Default 100 = disabled.
    //                  Use this to additionally cap how many pixels may differ, independent of severity.
    ::testing::AssertionResult checkBitmap(const std::string& testName,
                                           gmpi::drawing::BitmapRenderTarget& target,
                                           int    tolerance      = 0,
                                           double maxMeanDiff    = 15.0,
                                           double maxDiffPercent = 100.0)
    {
        auto bitmap = target.getBitmap();

        const auto refPath    = referenceDir() / (testName + ".png");
        const auto actualPath = referenceDir() / (testName + "_actual.png");
        const auto logPath    = referenceDir() / (testName + "_diff.log");

        // Remove stale artifacts from previous failures.
        std::filesystem::remove(actualPath);
        std::filesystem::remove(logPath);

        // Try to load the reference image.
        auto refBitmap = drawingContext.factory().loadImageU(refPath.string());

        if (!refBitmap)
        {
            savePng(refPath, bitmap);
            return ::testing::AssertionFailure()
                << "[MISSING REFERENCE] Wrote candidate image to:\n  "
                << refPath.string()
                << "\nVerify it looks correct then re-run the tests.";
        }

        // Compare sizes.
        const auto ourSize = bitmap.getSize();
        const auto refSize = refBitmap.getSize();
        if (ourSize.width != refSize.width || ourSize.height != refSize.height)
        {
            savePng(actualPath, bitmap);
            return ::testing::AssertionFailure()
                << "Size mismatch: rendered " << ourSize.width << "x" << ourSize.height
                << ", reference " << refSize.width << "x" << refSize.height
                << ". Actual image written to: " << actualPath.string();
        }

        // Compare pixels.
        // Rendered bitmap may be 64bppPRGBAHalf (8 bytes/pixel, float linear) or
        // 32bppPBGRA (4 bytes/pixel, 8-bit linear).  Reference bitmap is always
        // 32bppPBGRA sRGB (as loaded from PNG).  We convert the rendered pixel to
        // sRGB BGRA uint8_t before comparing so both sides are in the same space.
        auto ourPixels = bitmap.lockPixels(BitmapLockFlags::Read);
        auto refPixels = refBitmap.lockPixels(BitmapLockFlags::Read);

        const uint8_t* ourAddr = ourPixels.getAddress();
        const int32_t  ourBpr  = ourPixels.getBytesPerRow();
        const int32_t  ourBpp  = ourBpr / static_cast<int32_t>(ourSize.width);
        const uint8_t* refAddr = refPixels.getAddress();
        const int32_t  refBpr  = refPixels.getBytesPerRow();

        int     diffCount   = 0;
        int     maxChanDiff = 0;
        int64_t totalDiff   = 0;

        for (uint32_t y = 0; y < ourSize.height; ++y)
        {
            for (uint32_t x = 0; x < ourSize.width; ++x)
            {
                // Convert rendered pixel → sRGB BGRA uint8_t[4].
                uint8_t rendered[4];
#ifdef _WIN32
                if (ourBpp == 8)
                {
                    // 64bppPRGBAHalf: linear premultiplied RGBA half-float.
                    const uint16_t* h = reinterpret_cast<const uint16_t*>(
                        ourAddr + y * ourBpr + x * 8);
                    float fr = gmpi::drawing::detail::halfToFloat(h[0]);
                    float fg = gmpi::drawing::detail::halfToFloat(h[1]);
                    float fb = gmpi::drawing::detail::halfToFloat(h[2]);
                    float fa = gmpi::drawing::detail::halfToFloat(h[3]);

                    uint8_t a = static_cast<uint8_t>(std::clamp(fa * 255.0f + 0.5f, 0.0f, 255.0f));
                    if (fa > 0.0f)
                    {
                        fr = std::clamp(fr / fa, 0.0f, 1.0f);
                        fg = std::clamp(fg / fa, 0.0f, 1.0f);
                        fb = std::clamp(fb / fa, 0.0f, 1.0f);
                    }
                    else { fr = fg = fb = 0.0f; }

                    rendered[0] = detail::linearToSRGB_f(fb); // B
                    rendered[1] = detail::linearToSRGB_f(fg); // G
                    rendered[2] = detail::linearToSRGB_f(fr); // R
                    rendered[3] = a;
                }
                else
                {
                    // 32bppPBGRA: linear premultiplied BGRA 8-bit.
                    const uint8_t* p = ourAddr + y * ourBpr + x * 4;
                    uint8_t a = p[3];
                    if (a == 0)
                    {
                        rendered[0] = rendered[1] = rendered[2] = rendered[3] = 0;
                    }
                    else
                    {
                        float fa = a / 255.0f;
                        rendered[2] = detail::linearToSRGB(static_cast<uint8_t>(std::clamp(p[2] / fa + 0.5f, 0.0f, 255.0f)));
                        rendered[1] = detail::linearToSRGB(static_cast<uint8_t>(std::clamp(p[1] / fa + 0.5f, 0.0f, 255.0f)));
                        rendered[0] = detail::linearToSRGB(static_cast<uint8_t>(std::clamp(p[0] / fa + 0.5f, 0.0f, 255.0f)));
                        rendered[3] = a;
                    }
                }
#else
                if (ourBpp == 16)
                {
                    // 128bpp float: premultiplied RGBA 32-bit float, linear sRGB.
                    const float* f = reinterpret_cast<const float*>(
                        ourAddr + y * ourBpr + x * 16);
                    float fr = f[0];
                    float fg = f[1];
                    float fb = f[2];
                    float fa = f[3];

                    uint8_t a = static_cast<uint8_t>(std::clamp(fa * 255.0f + 0.5f, 0.0f, 255.0f));
                    if (fa > 0.0f)
                    {
                        fr = std::clamp(fr / fa, 0.0f, 1.0f);
                        fg = std::clamp(fg / fa, 0.0f, 1.0f);
                        fb = std::clamp(fb / fa, 0.0f, 1.0f);
                    }
                    else { fr = fg = fb = 0.0f; }

                    rendered[0] = detail::linearToSRGB_f(fr); // R
                    rendered[1] = detail::linearToSRGB_f(fg); // G
                    rendered[2] = detail::linearToSRGB_f(fb); // B
                    rendered[3] = a;
                }
                else
                {
                    // 32bpp: sRGB RGBA directly (SRGBPixels or loaded PNG).
                    const uint8_t* p = ourAddr + y * ourBpr + x * 4;
                    rendered[0] = p[0]; rendered[1] = p[1];
                    rendered[2] = p[2]; rendered[3] = p[3];
                }
#endif
                // Reference: sRGB BGRA from loaded PNG (always 32bppPBGRA).
                const uint8_t* ref = refAddr + y * refBpr + x * 4;

                int pixelMaxDiff = 0;
                int pixelDiff    = 0;
                for (int c = 0; c < 4; ++c)
                {
                    int d = std::abs(static_cast<int>(rendered[c]) - static_cast<int>(ref[c]));
                    pixelMaxDiff = std::max(pixelMaxDiff, d);
                    pixelDiff   += d;
                }
                if (pixelMaxDiff > tolerance)
                {
                    ++diffCount;
                    maxChanDiff = std::max(maxChanDiff, pixelMaxDiff);
                    totalDiff  += pixelDiff;
                }
            }
        }

        const int    totalPixels  = static_cast<int>(ourSize.width * ourSize.height);
        const double diffPercent  = 100.0 * diffCount / totalPixels;
        const double meanDiffAll  = static_cast<double>(totalDiff) / (totalPixels * 4);
        const double meanDiffBad  = diffCount > 0 ? static_cast<double>(totalDiff) / (diffCount * 4) : 0.0;

        // Pass if both independent image-size-independent checks are satisfied:
        //   1. meanDiffBad: quality check — how bad are the differing pixels (unaffected by image dimensions).
        //   2. diffPercent: quantity check — what fraction of pixels differ at all.
        const bool qualityOk  = (meanDiffBad  <= maxMeanDiff);
        const bool quantityOk = (diffPercent  <= maxDiffPercent);
        if (qualityOk && quantityOk)
            return ::testing::AssertionSuccess();

        // Write the diff log and actual image for failed tests.
        if (diffCount > 0)
        {
            savePng(actualPath, bitmap);

            if (auto f = std::ofstream(logPath))
            {
                f << "Test:               " << testName << "\n"
                  << "Tolerance:          " << tolerance << " / 255\n"
                  << "maxMeanDiff:        " << maxMeanDiff << " / 255  (limit on mean diff of differing pixels)\n"
                  << "maxDiffPercent:     " << maxDiffPercent << " %  (limit on fraction of differing pixels)\n"
                  << "Differing pixels:   " << diffCount << " / " << totalPixels
                  << " (" << diffPercent << "%)"
                  << (quantityOk ? "" : "  <-- EXCEEDS LIMIT") << "\n"
                  << "Max channel diff:   " << maxChanDiff << " / 255\n"
                  << "Mean diff (bad px): " << meanDiffBad << " / 255"
                  << (qualityOk ? "" : "  <-- EXCEEDS LIMIT") << "\n"
                  << "Mean diff (all px): " << meanDiffAll << " / 255\n"
                  << "Actual image:       " << actualPath.string() << "\n"
                  << "Reference image:    " << refPath.string()    << "\n";
            }
        }

        std::ostringstream msg;
        msg << "Pixel mismatch: " << diffCount << "/" << totalPixels
            << " (" << diffPercent << "%) pixels differ (limit: " << maxDiffPercent << "%)."
            << " Mean diff (bad px): " << meanDiffBad << "/255 (limit: " << maxMeanDiff << ")."
            << "\nActual image: " << actualPath.string()
            << "\nDiff log:     " << logPath.string();
        return ::testing::AssertionFailure() << msg.str();
    }

    // Convenience wrapper for the standard fixture render target.
    // Call at the end of each test (after all drawing is done).
    // tolerance:      max allowed per-channel difference (0 = pixel-exact).
    //                 Use tolerance >= 2 for text tests to handle ClearType sub-pixel variation.
    // maxMeanDiff:    max mean per-channel diff of DIFFERING pixels (image-size independent).
    // maxDiffPercent: max % of pixels allowed to differ. Default 100 = no limit.
    ::testing::AssertionResult checkResult(const std::string& testName, int tolerance = 0, double maxMeanDiff = 15.0, double maxDiffPercent = 100.0)
    {
        if (drawingActive)
        {
            g.endDraw();
            drawingActive = false;
        }
        return checkBitmap(testName, g, tolerance, maxMeanDiff, maxDiffPercent);
    }
};
