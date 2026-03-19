// GMPI-UI Drawing Tests
// Each test renders to a 64x64 CPU-readable offscreen bitmap and compares
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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "backends/DirectXGfx.h"
#include "Drawing.h"

using namespace gmpi::drawing;
using namespace gmpi::drawing::Colors;

// ============================================================
// savePng  –  save a GMPI Bitmap to a PNG file using WIC
// ============================================================
// The bitmap must have been created with the EightBitPixels flag so its
// locked pixels are 32bpp PBGRA, which WIC's PNG encoder accepts directly.
static bool savePng(const std::filesystem::path& path, gmpi::drawing::Bitmap& bitmap)
{
    std::filesystem::create_directories(path.parent_path());

    auto pixels = bitmap.lockPixels(BitmapLockFlags::Read);
    if (!pixels)
        return false;

    uint8_t* data    = pixels.getAddress();
    int32_t  bpr     = pixels.getBytesPerRow();
    SizeU    size    = bitmap.getSize();

    // Create a short-lived WIC factory for encoding only.
    IWICImagingFactory* rawWic{};
    CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&rawWic));
    gmpi::directx::ComPtr<IWICImagingFactory> wic(rawWic);
    if (!wic) return false;

    IWICStream* rawStream{};
    wic->CreateStream(&rawStream);
    gmpi::directx::ComPtr<IWICStream> stream(rawStream);
    if (!stream) return false;
    stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);

    IWICBitmapEncoder* rawEncoder{};
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &rawEncoder);
    gmpi::directx::ComPtr<IWICBitmapEncoder> encoder(rawEncoder);
    if (!encoder) return false;
    encoder->Initialize(stream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* rawFrame{};
    IPropertyBag2* props{};
    encoder->CreateNewFrame(&rawFrame, &props);
    gmpi::directx::ComPtr<IWICBitmapFrameEncode> frame(rawFrame);
    if (props) props->Release();
    if (!frame) return false;

    frame->Initialize(nullptr);
    frame->SetSize(size.width, size.height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(size.height, bpr, bpr * size.height, data);
    frame->Commit();
    encoder->Commit();

    return true;
}

// ============================================================
// DrawingTest fixture
// ============================================================
class DrawingTest : public ::testing::Test
{
protected:
    static constexpr uint32_t kWidth  = 64;
    static constexpr uint32_t kHeight = 64;

    // Use EightBitPixels so the WIC bitmap backing the render target is
    // 32bpp PBGRA – the format that lockPixels / getPixel understand.
    static constexpr int32_t kRenderFlags =
        static_cast<int32_t>(BitmapRenderTargetFlags::EightBitPixels);

    std::unique_ptr<gmpi::directx::Factory> dxFactory;
    gmpi::drawing::BitmapRenderTarget rt;
    bool drawingActive = false;

    static std::filesystem::path referenceDir()
    {
        return REFERENCE_IMAGES_DIR;
    }

    void SetUp() override
    {
        CoInitialize(nullptr);
        dxFactory = std::make_unique<gmpi::directx::Factory>();
        dxFactory->createCpuRenderTarget({kWidth, kHeight}, kRenderFlags, AccessPtr::put(rt));
        rt.beginDraw();
        drawingActive = true;
        rt.clear(Colors::White);
    }

    void TearDown() override
    {
        if (drawingActive)
            rt.endDraw();
        // Release rt before factory to avoid dangling pointer.
        rt = gmpi::drawing::BitmapRenderTarget{};
        dxFactory.reset();
        CoUninitialize();
    }

    // Call at the end of each test (after all drawing is done).
    // Compares the rendered bitmap to the reference PNG.
    // On mismatch or missing reference, writes output to disk and fails.
    ::testing::AssertionResult checkResult(const std::string& testName)
    {
        if (drawingActive)
        {
            rt.endDraw();
            drawingActive = false;
        }

        auto bitmap = rt.getBitmap();

        const auto refPath    = referenceDir() / (testName + ".png");
        const auto actualPath = referenceDir() / (testName + "_actual.png");

        // Try to load the reference image.
        gmpi::drawing::Bitmap refBitmap;
        {
            const auto pathStr = refPath.string();
            dxFactory->loadImageU(pathStr.c_str(), AccessPtr::put(refBitmap));
        }

        if (!refBitmap)
        {
            // No reference yet – save the rendered output as the candidate
            // reference so the user can verify it and re-run.
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
        auto ourPixels = bitmap.lockPixels(BitmapLockFlags::Read);
        auto refPixels = refBitmap.lockPixels(BitmapLockFlags::Read);

        int     diffCount   = 0;
        int     maxChanDiff = 0;
        int64_t totalDiff   = 0;

        for (uint32_t y = 0; y < ourSize.height; ++y)
        {
            for (uint32_t x = 0; x < ourSize.width; ++x)
            {
                const uint32_t a = ourPixels.getPixel(x, y);
                const uint32_t b = refPixels.getPixel(x, y);
                if (a != b)
                {
                    ++diffCount;
                    for (int c = 0; c < 4; ++c)
                    {
                        int d = std::abs(
                            static_cast<int>((a >> (c * 8)) & 0xFF) -
                            static_cast<int>((b >> (c * 8)) & 0xFF));
                        maxChanDiff = std::max(maxChanDiff, d);
                        totalDiff  += d;
                    }
                }
            }
        }

        if (diffCount > 0)
        {
            savePng(actualPath, bitmap);

            const int    totalPixels = static_cast<int>(ourSize.width * ourSize.height);
            const double meanDiff    = static_cast<double>(totalDiff) / (diffCount * 4);

            // Write a text log alongside the actual image.
            const auto logPath = referenceDir() / (testName + "_diff.log");
            if (auto f = std::ofstream(logPath))
            {
                f << "Test:              " << testName << "\n"
                  << "Differing pixels:  " << diffCount << " / " << totalPixels
                  << " (" << 100.0 * diffCount / totalPixels << "%)\n"
                  << "Max channel diff:  " << maxChanDiff << " / 255\n"
                  << "Mean channel diff: " << meanDiff    << " / 255\n"
                  << "Actual image:      " << actualPath.string() << "\n"
                  << "Reference image:   " << refPath.string()    << "\n";
            }

            std::ostringstream msg;
            msg << "Pixel mismatch: " << diffCount << "/" << totalPixels
                << " pixels differ."
                << " Max channel diff: " << maxChanDiff << "/255."
                << " Mean diff: " << meanDiff << "/255."
                << "\nActual image: " << actualPath.string()
                << "\nDiff log:     " << logPath.string();
            return ::testing::AssertionFailure() << msg.str();
        }

        return ::testing::AssertionSuccess();
    }
};

// ============================================================
// Tests
// ============================================================

// Clear the entire render target with a flat colour.
TEST_F(DrawingTest, Clear)
{
    rt.clear(Colors::CornflowerBlue);
    EXPECT_TRUE(checkResult("clear"));
}

// Fill a rectangle leaving an 8-pixel border.
TEST_F(DrawingTest, FillRectangle)
{
    auto brush = rt.createSolidColorBrush(Colors::SteelBlue);
    rt.fillRectangle({8.f, 8.f, 56.f, 56.f}, brush);
    EXPECT_TRUE(checkResult("fillRectangle"));
}

// Stroke a rectangle (no fill) with a 2-pixel line.
TEST_F(DrawingTest, DrawRectangle)
{
    auto brush = rt.createSolidColorBrush(Colors::DarkRed);
    rt.drawRectangle({8.f, 8.f, 56.f, 56.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawRectangle"));
}

// Draw a diagonal line across the bitmap.
TEST_F(DrawingTest, DrawLine)
{
    auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
    rt.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawLine"));
}

// Fill a circle centred in the bitmap.
TEST_F(DrawingTest, FillEllipse)
{
    auto brush = rt.createSolidColorBrush(Colors::Orchid);
    rt.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, brush);
    EXPECT_TRUE(checkResult("fillEllipse"));
}

// Stroke a circle (no fill) with a 2-pixel line.
TEST_F(DrawingTest, DrawEllipse)
{
    auto brush = rt.createSolidColorBrush(Colors::DarkSlateGray);
    rt.drawEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawEllipse"));
}

// Fill a rounded rectangle.
TEST_F(DrawingTest, FillRoundedRectangle)
{
    auto brush = rt.createSolidColorBrush(Colors::Tomato);
    rt.fillRoundedRectangle(RoundedRect{{8.f, 8.f, 56.f, 56.f}, 8.f, 8.f}, brush);
    EXPECT_TRUE(checkResult("fillRoundedRectangle"));
}

// Stroke a rounded rectangle with a 2-pixel line.
TEST_F(DrawingTest, DrawRoundedRectangle)
{
    auto brush = rt.createSolidColorBrush(Colors::MidnightBlue);
    rt.drawRoundedRectangle(RoundedRect{{8.f, 8.f, 56.f, 56.f}, 8.f, 8.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawRoundedRectangle"));
}

// Fill a rectangle with a horizontal linear gradient (red → blue).
TEST_F(DrawingTest, LinearGradientFill)
{
    const Gradientstop stops[] = {
        {0.0f, Colors::Red},
        {1.0f, Colors::Blue},
    };
    auto brush = rt.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("linearGradientFill"));
}

// Fill a rectangle with a radial gradient (yellow centre → dark edge).
TEST_F(DrawingTest, RadialGradientFill)
{
    auto brush = rt.createRadialGradientBrush(
        {32.f, 32.f}, 28.f, Colors::Yellow, Colors::DarkBlue);
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("radialGradientFill"));
}
