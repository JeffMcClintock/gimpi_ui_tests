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
#include <array>
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

    // Create a StrokeStyle via the render target's factory wrapper.
    StrokeStyle makeStrokeStyle(StrokeStyleProperties props,
                                std::span<const float> dashes = {})
    {
        return rt.getFactory().createStrokeStyle(props, dashes);
    }

    // Create a TextFormat via the low-level factory.
    // Defaults to Arial so results are consistent across Windows machines.
    TextFormat makeTextFormat(
        float height,
        const char*        family  = "Arial",
        FontWeight         weight  = FontWeight::Regular,
        FontStyle          style   = FontStyle::Normal,
        FontStretch        stretch = FontStretch::Normal)
    {
        TextFormat tf;
        dxFactory->createTextFormat(
            family, weight, style, stretch, height,
            static_cast<int32_t>(FontFlags::BodyHeight),
            AccessPtr::put(tf));
        return tf;
    }

    // Build an 8x8 checkerboard bitmap using a small render target, then
    // wrap it in a BitmapBrush tied to the main render target (rt).
    BitmapBrush makeCheckerboardBrush(Color color1 = Colors::Red,
                                      Color color2 = Colors::White)
    {
        constexpr uint32_t kPat = 8;
        gmpi::drawing::BitmapRenderTarget patRT;
        dxFactory->createCpuRenderTarget({kPat, kPat}, kRenderFlags, AccessPtr::put(patRT));
        patRT.beginDraw();
        patRT.clear(color1);
        auto b2 = patRT.createSolidColorBrush(color2);
        for (uint32_t y = 0; y < kPat; ++y)
            for (uint32_t x = y % 2; x < kPat; x += 2)
                patRT.fillRectangle({float(x), float(y), float(x+1), float(y+1)}, b2);
        patRT.endDraw();
        auto patternBitmap = patRT.getBitmap();
        return rt.createBitmapBrush(patternBitmap);
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

    // Core comparison helper — works with any already-ended BitmapRenderTarget.
    ::testing::AssertionResult checkBitmap(const std::string& testName,
                                           gmpi::drawing::BitmapRenderTarget& target,
                                           int tolerance = 0)
    {
        auto bitmap = target.getBitmap();

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
                    int pixelMaxDiff = 0;
                    int pixelDiff    = 0;
                    for (int c = 0; c < 4; ++c)
                    {
                        int d = std::abs(
                            static_cast<int>((a >> (c * 8)) & 0xFF) -
                            static_cast<int>((b >> (c * 8)) & 0xFF));
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
        }

        if (diffCount > 0)
        {
            savePng(actualPath, bitmap);

            const int    totalPixels = static_cast<int>(ourSize.width * ourSize.height);
            const double meanDiff    = static_cast<double>(totalDiff) / (diffCount * 4);

            const auto logPath = referenceDir() / (testName + "_diff.log");
            if (auto f = std::ofstream(logPath))
            {
                f << "Test:              " << testName << "\n"
                  << "Tolerance:         " << tolerance << " / 255\n"
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

    // Convenience wrapper for the standard fixture render target.
    // Call at the end of each test (after all drawing is done).
    // tolerance: max allowed per-channel difference (0 = pixel-exact).
    // Use tolerance >= 2 for text tests to handle ClearType sub-pixel variation.
    ::testing::AssertionResult checkResult(const std::string& testName, int tolerance = 0)
    {
        if (drawingActive)
        {
            rt.endDraw();
            drawingActive = false;
        }
        return checkBitmap(testName, rt, tolerance);
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

// ============================================================
// Text drawing tests
// ============================================================

// Single short string, left-aligned, black on white.
TEST_F(DrawingTest, DrawTextSimple)
{
    auto tf    = makeTextFormat(12.f);
    rt.drawTextU("Hello", tf, {2.f, 2.f, 62.f, 62.f}, rt.createSolidColorBrush(Colors::Black));
    EXPECT_TRUE(checkResult("drawTextSimple", 2));
}

// Text centred horizontally and vertically in the bitmap.
TEST_F(DrawingTest, DrawTextCentred)
{
    auto tf = makeTextFormat(12.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("Hi", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("drawTextCentred", 2));
}

// Bold weight text.
TEST_F(DrawingTest, DrawTextBold)
{
    auto tf    = makeTextFormat(12.f, "Arial", FontWeight::Bold);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("Bold", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("drawTextBold", 2));
}

// Larger font size to stress-test glyph outlines.
TEST_F(DrawingTest, DrawTextLarge)
{
    auto tf    = makeTextFormat(24.f);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("Ag", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("drawTextLarge", 2));
}

// Two lines of text — verifies line spacing.
TEST_F(DrawingTest, DrawTextMultiLine)
{
    auto tf = makeTextFormat(10.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("Line one\nLine two", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("drawTextMultiLine", 40)); // inconsistant smoothing, on same windows system
}

// Coloured text on a coloured background.
TEST_F(DrawingTest, DrawTextColoured)
{
    auto bgBrush   = rt.createSolidColorBrush(Colors::DarkBlue);
    auto textBrush = rt.createSolidColorBrush(Colors::Yellow);
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    auto tf = makeTextFormat(14.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    rt.drawTextU("Test", tf, {0.f, 0.f, 64.f, 64.f}, textBrush);
    EXPECT_TRUE(checkResult("drawTextColoured", 2));
}

// ============================================================
// Bitmap brush tests
// ============================================================

// Stroke a thick line with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushLine)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::Yellow);
    rt.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("bitmapBrushLine"));
}

// Fill a rectangle with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushFillRectangle)
{
    auto brush = makeCheckerboardBrush(Colors::DarkGreen, Colors::White);
    rt.fillRectangle({4.f, 4.f, 60.f, 60.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushFillRectangle"));
}

// Fill an ellipse with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushFillEllipse)
{
    auto brush = makeCheckerboardBrush(Colors::DarkRed, Colors::White);
    rt.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 28.f, 28.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushFillEllipse"));
}

// Draw text using a bitmap brush as the foreground.
TEST_F(DrawingTest, BitmapBrushText)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::Cyan);
    auto tf    = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    rt.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushText", 2));
}

// ============================================================
// Gradient brush on lines and text
// ============================================================

// Stroke a thick diagonal line with a horizontal linear gradient.
TEST_F(DrawingTest, LinearGradientLine)
{
    const std::array<Gradientstop, 2> stops = {{
        {0.f, Colors::Red},
        {1.f, Colors::Blue},
    }};
    auto brush = rt.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    rt.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("linearGradientLine"));
}

// Stroke a thick line with a radial gradient (useful for glowing effects).
TEST_F(DrawingTest, RadialGradientLine)
{
    auto brush = rt.createRadialGradientBrush(
        {32.f, 32.f}, 40.f, Colors::White, Colors::DarkBlue);
    rt.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("radialGradientLine"));
}

// Draw large text with a horizontal linear gradient foreground.
TEST_F(DrawingTest, LinearGradientText)
{
    const std::array<Gradientstop, 2> stops = {{
        {0.f, Colors::Red},
        {1.f, Colors::Blue},
    }};
    auto brush = rt.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    rt.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("linearGradientText", 2));
}

// Draw large text with a radial gradient foreground.
TEST_F(DrawingTest, RadialGradientText)
{
    auto brush = rt.createRadialGradientBrush(
        {32.f, 32.f}, 32.f, Colors::Yellow, Colors::DarkRed);
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    rt.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("radialGradientText", 2));
}

// ============================================================
// Transparent brush tests
// ============================================================

// Semi-transparent fill blended over a solid background.
TEST_F(DrawingTest, TransparentFill)
{
    auto bgBrush  = rt.createSolidColorBrush(Colors::DarkBlue);
    auto fgBrush  = rt.createSolidColorBrush(colorFromHex(0xFF4500u, 0.5f)); // OrangeRed 50%
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    rt.fillRectangle({8.f, 8.f, 56.f, 56.f}, fgBrush);
    EXPECT_TRUE(checkResult("transparentFill"));
}

// Two semi-transparent shapes overlapping — tests additive blending order.
TEST_F(DrawingTest, TransparentOverlap)
{
    auto red   = rt.createSolidColorBrush(colorFromHex(0xFF0000u, 0.6f));
    auto blue  = rt.createSolidColorBrush(colorFromHex(0x0000FFu, 0.6f));
    rt.fillEllipse(gmpi::drawing::Ellipse{{24.f, 32.f}, 20.f, 20.f}, red);
    rt.fillEllipse(gmpi::drawing::Ellipse{{40.f, 32.f}, 20.f, 20.f}, blue);
    EXPECT_TRUE(checkResult("transparentOverlap"));
}

// Semi-transparent stroke over a filled rectangle.
TEST_F(DrawingTest, TransparentStroke)
{
    auto fill   = rt.createSolidColorBrush(Colors::SteelBlue);
    auto stroke = rt.createSolidColorBrush(colorFromHex(0x000000u, 0.4f)); // 40% black
    rt.fillRectangle({4.f, 4.f, 60.f, 60.f}, fill);
    rt.drawRectangle({12.f, 12.f, 52.f, 52.f}, stroke, 6.0f);
    EXPECT_TRUE(checkResult("transparentStroke"));
}

// Semi-transparent text over a coloured background.
TEST_F(DrawingTest, TransparentText)
{
    auto bgBrush   = rt.createSolidColorBrush(Colors::Tomato);
    auto textBrush = rt.createSolidColorBrush(colorFromHex(0xFFFFFFu, 0.6f)); // 60% white
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    rt.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, textBrush);
    EXPECT_TRUE(checkResult("transparentText", 2));
}

// ============================================================
// Bitmap draw and stretch tests  (drawBitmap, not BitmapBrush)
// ============================================================

// Helper: build a small 16x16 RGBA test pattern (4 coloured quadrants).
// Returns the bitmap; uses the fixture's dxFactory and kRenderFlags.
// (defined as a lambda inside each test to keep captures clear, but the
//  logic is the same so we put it in the fixture as a method.)

// Draw a bitmap 1:1 at its natural size (pixel-exact, no interpolation).
TEST_F(DrawingTest, DrawBitmapNative)
{
    // Build a 16x16 four-quadrant pattern.
    gmpi::drawing::BitmapRenderTarget patRT;
    dxFactory->createCpuRenderTarget({16, 16}, kRenderFlags, AccessPtr::put(patRT));
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Draw centred, 1:1.
    rt.drawBitmap(bmp, {24.f, 24.f, 40.f, 40.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapNative"));
}

// Stretch the same 16x16 bitmap to fill most of the render target.
TEST_F(DrawingTest, DrawBitmapStretched)
{
    gmpi::drawing::BitmapRenderTarget patRT;
    dxFactory->createCpuRenderTarget({16, 16}, kRenderFlags, AccessPtr::put(patRT));
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Stretch to 56x56 with nearest-neighbour (pixel-exact, no blending).
    rt.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapStretched"));
}

// Stretch with bilinear interpolation — smooth edges between quadrants.
TEST_F(DrawingTest, DrawBitmapLinearInterp)
{
    gmpi::drawing::BitmapRenderTarget patRT;
    dxFactory->createCpuRenderTarget({16, 16}, kRenderFlags, AccessPtr::put(patRT));
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    rt.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::Linear);
    EXPECT_TRUE(checkResult("drawBitmapLinearInterp"));
}

// Draw only a sub-rectangle (top-right quadrant) of the source bitmap.
TEST_F(DrawingTest, DrawBitmapCropped)
{
    gmpi::drawing::BitmapRenderTarget patRT;
    dxFactory->createCpuRenderTarget({16, 16}, kRenderFlags, AccessPtr::put(patRT));
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Source: top-right quadrant (green). Destination: stretched to most of the bitmap.
    rt.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {8.f, 0.f, 16.f, 8.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapCropped"));
}

// Draw a bitmap at 50% opacity over a coloured background.
TEST_F(DrawingTest, DrawBitmapOpacity)
{
    gmpi::drawing::BitmapRenderTarget patRT;
    dxFactory->createCpuRenderTarget({16, 16}, kRenderFlags, AccessPtr::put(patRT));
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    auto bgBrush = rt.createSolidColorBrush(Colors::DarkSlateGray);
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    rt.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  0.5f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapOpacity"));
}

// ============================================================
// Clipping tests
// ============================================================

// Fill the whole target, clip to an inner rect, fill again — only the
// inner region should show the second colour.
TEST_F(DrawingTest, ClipBasic)
{
    auto bgBrush = rt.createSolidColorBrush(Colors::SteelBlue);
    auto fgBrush = rt.createSolidColorBrush(Colors::OrangeRed);
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    rt.pushAxisAlignedClip({16.f, 16.f, 48.f, 48.f});
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, fgBrush); // only 16..48 visible
    rt.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("clipBasic"));
}

// Two nested clips: only their intersection should receive the fill.
TEST_F(DrawingTest, ClipNested)
{
    auto bgBrush = rt.createSolidColorBrush(Colors::DarkSlateGray);
    auto fgBrush = rt.createSolidColorBrush(Colors::Yellow);
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    rt.pushAxisAlignedClip({8.f,  8.f,  56.f, 56.f});
    rt.pushAxisAlignedClip({20.f, 20.f, 64.f, 64.f}); // intersection: 20..56
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, fgBrush);
    rt.popAxisAlignedClip();
    rt.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("clipNested"));
}

// ============================================================
// Transform tests
// ============================================================

// Scale transform: a small rectangle appears stretched.
TEST_F(DrawingTest, TransformScale)
{
    auto brush = rt.createSolidColorBrush(Colors::Tomato);
    rt.setTransform(makeScale(2.f, 1.5f));
    rt.fillRectangle({8.f, 8.f, 24.f, 24.f}, brush);
    rt.setTransform(Matrix3x2{}); // identity
    EXPECT_TRUE(checkResult("transformScale"));
}

// Translation: draw at origin, transform shifts it.
TEST_F(DrawingTest, TransformTranslate)
{
    auto brush = rt.createSolidColorBrush(Colors::DodgerBlue);
    rt.setTransform(makeTranslation(20.f, 16.f));
    rt.fillRectangle({0.f, 0.f, 20.f, 20.f}, brush);
    rt.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("transformTranslate"));
}

// 45-degree rotation around the bitmap centre.
TEST_F(DrawingTest, TransformRotate)
{
    constexpr float kPi = 3.14159265f;
    auto brush = rt.createSolidColorBrush(Colors::Orchid);
    rt.setTransform(makeRotation(kPi / 4.f, {32.f, 32.f}));
    rt.fillRectangle({20.f, 20.f, 44.f, 44.f}, brush);
    rt.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("transformRotate"));
}

// Draw with a transform then reset — both shapes should be visible.
TEST_F(DrawingTest, TransformReset)
{
    auto red  = rt.createSolidColorBrush(Colors::Red);
    auto blue = rt.createSolidColorBrush(Colors::Blue);
    rt.setTransform(makeTranslation(0.f, 32.f));
    rt.fillRectangle({0.f, 0.f, 32.f, 32.f}, red);  // lands at y=32..64
    rt.setTransform(Matrix3x2{});
    rt.fillRectangle({32.f, 0.f, 64.f, 32.f}, blue); // stays at x=32..64, y=0..32
    EXPECT_TRUE(checkResult("transformReset"));
}

// ============================================================
// Stroke style tests
// ============================================================

// Dashed line.
TEST_F(DrawingTest, StrokeStyleDash)
{
    StrokeStyleProperties props;
    props.dashStyle = DashStyle::Dash;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::DarkRed);
    rt.drawLine({8.f, 32.f}, {56.f, 32.f}, brush, 4.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleDash"));
}

// Dotted line with round caps.
TEST_F(DrawingTest, StrokeStyleDot)
{
    StrokeStyleProperties props;
    props.dashStyle = DashStyle::Dot;
    props.lineCap   = CapStyle::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
    rt.drawLine({8.f, 32.f}, {56.f, 32.f}, brush, 4.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleDot"));
}

// Round caps extend beyond the line endpoints.
TEST_F(DrawingTest, StrokeStyleRoundCap)
{
    StrokeStyleProperties props;
    props.lineCap = CapStyle::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::DarkSlateBlue);
    rt.drawLine({16.f, 32.f}, {48.f, 32.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleRoundCap"));
}

// Square caps extend squarely beyond the endpoints.
TEST_F(DrawingTest, StrokeStyleSquareCap)
{
    StrokeStyleProperties props;
    props.lineCap = CapStyle::Square;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::Sienna);
    rt.drawLine({16.f, 32.f}, {48.f, 32.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleSquareCap"));
}

// Bevel join on a stroked rectangle.
TEST_F(DrawingTest, StrokeStyleBevelJoin)
{
    StrokeStyleProperties props;
    props.lineJoin = LineJoin::Bevel;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::DarkOliveGreen);
    rt.drawRectangle({12.f, 12.f, 52.f, 52.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleBevelJoin"));
}

// Round join on a stroked rectangle.
TEST_F(DrawingTest, StrokeStyleRoundJoin)
{
    StrokeStyleProperties props;
    props.lineJoin = LineJoin::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = rt.createSolidColorBrush(Colors::DarkMagenta);
    rt.drawRectangle({12.f, 12.f, 52.f, 52.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleRoundJoin"));
}

// ============================================================
// Degenerate / boundary cases
// ============================================================

// An empty string should not affect the bitmap (remains all-white).
TEST_F(DrawingTest, EmptyStringText)
{
    auto tf    = makeTextFormat(12.f);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("emptyStringText", 2));
}

// A fully transparent brush (alpha=0) should render nothing.
TEST_F(DrawingTest, FullyTransparentBrush)
{
    auto brush = rt.createSolidColorBrush(colorFromHex(0xFF0000u, 0.0f)); // red, alpha=0
    rt.fillRectangle({8.f, 8.f, 56.f, 56.f}, brush);
    EXPECT_TRUE(checkResult("fullyTransparentBrush"));
}

// A rect larger than the render target is clamped to its bounds.
TEST_F(DrawingTest, ShapeAtBoundary)
{
    auto brush = rt.createSolidColorBrush(Colors::DarkOrchid);
    rt.fillRectangle({-8.f, -8.f, 72.f, 72.f}, brush);
    EXPECT_TRUE(checkResult("shapeAtBoundary"));
}

// Zero-width stroke — platform-defined; at minimum must not crash.
TEST_F(DrawingTest, ZeroWidthStroke)
{
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawRectangle({8.f, 8.f, 56.f, 56.f}, brush, 0.0f);
    EXPECT_TRUE(checkResult("zeroWidthStroke"));
}

// ============================================================
// Text clipping and wrapping
// ============================================================

// Text clipped by pushAxisAlignedClip — top half of glyphs only.
TEST_F(DrawingTest, TextClippedByClipRect)
{
    auto tf    = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    // Only the top half of the bitmap is visible.
    rt.pushAxisAlignedClip({0.f, 0.f, 64.f, 32.f});
    rt.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    rt.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("textClippedByClipRect", 2));
}

// Text clipped by the layout rect — right edge of a long string is cut off.
TEST_F(DrawingTest, TextClippedByLayoutRect)
{
    auto tf    = makeTextFormat(14.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    // Layout rect is narrow — text overflows and is clipped on the right.
    rt.drawTextU("ClipMe Right Edge", tf, {2.f, 22.f, 40.f, 42.f}, brush);
    EXPECT_TRUE(checkResult("textClippedByLayoutRect", 34));
}

// Word-wrap ON: a long string breaks across multiple lines within the layout rect.
TEST_F(DrawingTest, TextWrapOn)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("textWrapOn", 34));
}

// Word-wrap OFF: same long string runs in one line and is clipped on the right.
TEST_F(DrawingTest, TextWrapOff)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("textWrapOff", 2));
}

// Text clipped at the bottom of the layout rect — last line is cut off.
TEST_F(DrawingTest, TextClippedAtBottom)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    // Layout rect is only tall enough for 2 of the 4 lines.
    rt.drawTextU("Line one\nLine two\nLine three\nLine four",
                 tf, {2.f, 2.f, 62.f, 28.f}, brush);
    EXPECT_TRUE(checkResult("textClippedAtBottom", 10));
}

// ============================================================
// Bitmap brush origin / offset
// ============================================================

// Fill rect aligned to the pattern grid (rect origin == world origin == tile boundary).
// The 8x8 checkerboard tiles should appear perfectly aligned with the rect corners.
TEST_F(DrawingTest, BitmapBrushOriginAligned)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::LightGray);
    // Rect starts at (0,0) — world origin — so tile boundaries coincide with rect edges.
    rt.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushOriginAligned"));
}

// Fill rect offset from the pattern grid: the brush origin stays at world (0,0),
// so the tile pattern appears shifted by (4,4) relative to the rect's top-left corner.
TEST_F(DrawingTest, BitmapBrushOriginOffset)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::LightGray);
    // Rect starts at (4,4): 4 pixels into the 8-pixel tile, so the corner pixel
    // comes from the middle of a tile rather than a tile boundary.
    rt.fillRectangle({4.f, 4.f, 60.f, 60.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushOriginOffset"));
}

// Demonstrate that setTransform shifts the pattern origin together with geometry:
// the brush samples world-space coordinates, so translating the render target
// shifts both the rect AND the pattern by the same amount — the pattern is
// phase-shifted relative to where an un-transformed rect would land.
TEST_F(DrawingTest, BitmapBrushOriginWithTransform)
{
    auto brush = makeCheckerboardBrush(Colors::Crimson, Colors::LightGray);
    // With a (4,4) translation, drawing rect (0,0)-(56,56) is equivalent to
    // drawing at (4,4)-(60,60) in world space — the pattern tiles from world (0,0)
    // so the visible corner of the rect is 4 pixels into the first tile.
    rt.setTransform(makeTranslation(4.f, 4.f));
    rt.fillRectangle({0.f, 0.f, 56.f, 56.f}, brush);
    rt.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("bitmapBrushOriginWithTransform"));
}

// ============================================================
// Polygon fill rules
// ============================================================
//
// The classic tests for FillMode::Alternate (even-odd) vs FillMode::Winding.
//
// Helper: build a pentagram (5-pointed star) as a self-intersecting closed path.
// The outer tips are at radius r from centre (cx,cy), drawn by connecting every
// other vertex of a regular pentagon.  The inner pentagon is visited twice, so
// its winding number is 2 under the winding rule (filled) but 2 mod 2 = 0 under
// the even-odd rule (hole).
//
// Helper: make a square contour (4 vertices, Y-down screen coords).
//   CW  in screen space: TL→TR→BR→BL
//   CCW in screen space: TL→BL→BR→TR

namespace {

PathGeometry makePentagram(gmpi::drawing::BitmapRenderTarget& rt, FillMode fm)
{
    constexpr float cx = 32.f, cy = 32.f, r = 28.f;
    constexpr float kPi = 3.14159265f;
    auto pt = [&](float deg) -> Point {
        const float rad = deg * kPi / 180.f;
        return {cx + r * std::cos(rad), cy + r * std::sin(rad)};
    };

    auto geom = rt.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(fm);
    // Skip every other vertex: 0°-indexed from top (-90°), step 144°
    sink.beginFigure(pt(-90.f), FigureBegin::Filled);
    sink.addLine(pt(-90.f + 144.f));
    sink.addLine(pt(-90.f + 288.f));
    sink.addLine(pt(-90.f + 432.f));
    sink.addLine(pt(-90.f + 576.f));
    sink.endFigure(FigureEnd::Closed);
    sink.close();
    return geom;
}

// Square contour helper.
// clockwise=true  → TL→TR→BR→BL  (positive winding in screen/Y-down space)
// clockwise=false → TL→BL→BR→TR  (negative winding)
void addSquareFigure(GeometrySink& sink, Rect r, bool clockwise)
{
    if (clockwise)
    {
        sink.beginFigure({r.left,  r.top},    FigureBegin::Filled);
        sink.addLine    ({r.right, r.top});
        sink.addLine    ({r.right, r.bottom});
        sink.addLine    ({r.left,  r.bottom});
    }
    else
    {
        sink.beginFigure({r.left,  r.top},    FigureBegin::Filled);
        sink.addLine    ({r.left,  r.bottom});
        sink.addLine    ({r.right, r.bottom});
        sink.addLine    ({r.right, r.top});
    }
    sink.endFigure(FigureEnd::Closed);
}

} // anonymous namespace

// ---- Stars ----

// Pentagram with Alternate (even-odd) fill: the inner pentagon is a hole because
// a ray from there crosses the path boundary an even number of times (2).
TEST_F(DrawingTest, FillModeAlternateStar)
{
    auto geom  = makePentagram(rt, FillMode::Alternate);
    auto brush = rt.createSolidColorBrush(Colors::SteelBlue);
    rt.fillGeometry(geom, brush);
    auto outline = rt.createSolidColorBrush(Colors::DarkSlateGray);
    rt.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeAlternateStar"));
}

// Same pentagram with Winding fill: every region has non-zero winding number so
// the entire star — including the inner pentagon — is filled solid.
TEST_F(DrawingTest, FillModeWindingStar)
{
    auto geom  = makePentagram(rt, FillMode::Winding);
    auto brush = rt.createSolidColorBrush(Colors::Tomato);
    rt.fillGeometry(geom, brush);
    auto outline = rt.createSolidColorBrush(Colors::DarkRed);
    rt.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingStar"));
}

// ---- Nested squares ----

// Alternate rule: inner square always becomes a hole regardless of direction
// (a point inside crosses 2 contour boundaries → even → not filled).
TEST_F(DrawingTest, FillModeAlternateNestedSquares)
{
    auto geom = rt.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Alternate);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);  // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, true);  // inner CW — still a hole
    sink.close();

    auto brush   = rt.createSolidColorBrush(Colors::CornflowerBlue);
    auto outline = rt.createSolidColorBrush(Colors::DarkBlue);
    rt.fillGeometry(geom, brush);
    rt.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeAlternateNestedSquares"));
}

// Winding rule, both squares CW: inner winding = 2 (outer CW + inner CW adds),
// so the inner square IS filled (both regions have non-zero winding).
TEST_F(DrawingTest, FillModeWindingNestedSameDir)
{
    auto geom = rt.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Winding);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);   // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, true);   // inner CW  → winding=2, filled
    sink.close();

    auto brush   = rt.createSolidColorBrush(Colors::DarkOrange);
    auto outline = rt.createSolidColorBrush(Colors::Sienna);
    rt.fillGeometry(geom, brush);
    rt.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingNestedSameDir"));
}

// Winding rule, outer CW + inner CCW: the inner winding cancels to 0,
// so the inner square becomes a hole — same visual as the Alternate case.
TEST_F(DrawingTest, FillModeWindingNestedOppositeDir)
{
    auto geom = rt.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Winding);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);   // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, false);  // inner CCW → winding=0, hole
    sink.close();

    auto brush   = rt.createSolidColorBrush(Colors::MediumPurple);
    auto outline = rt.createSolidColorBrush(Colors::DarkMagenta);
    rt.fillGeometry(geom, brush);
    rt.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingNestedOppositeDir"));
}

// ============================================================
// Font metrics visualisation  (256 × 120 render target)
// ============================================================
//
// Draws "Hfgx" in large text and overlays coloured horizontal rules at each
// metric returned by TextFormat::getFontMetrics():
//
//   Blue   — ascender line   (top of em box, ascent above baseline)
//   Green  — cap-height line (top of capital letters like H)
//   Orange — x-height line   (top of lowercase letters like x)
//   Red    — baseline
//   Purple — descender line  (bottom of em box, descent below baseline)
//
// The underline and strikethrough positions are drawn as thin dashed cyan
// and magenta lines respectively so they can also be verified.
TEST_F(DrawingTest, FontMetricsVisual)
{
    constexpr uint32_t kW = 256, kH = 120;

    // Create a dedicated render target for this test.
    gmpi::drawing::BitmapRenderTarget bigRT;
    dxFactory->createCpuRenderTarget({kW, kH}, kRenderFlags, AccessPtr::put(bigRT));
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    // ---- font & metrics ----
    constexpr float kFontHeight = 60.f;   // body height (ascent + descent)
    constexpr float kBaselineY  = 86.f;   // y we want the text baseline to land on

    TextFormat tf;
    dxFactory->createTextFormat(
        "Arial", FontWeight::Regular, FontStyle::Normal, FontStretch::Normal,
        kFontHeight, static_cast<int32_t>(FontFlags::BodyHeight), AccessPtr::put(tf));
    tf.setWordWrapping(WordWrapping::NoWrap);

    FontMetrics fm = tf.getFontMetrics();

    const float ascenderY    = kBaselineY - fm.ascent;
    const float capY         = kBaselineY - fm.capHeight;
    const float xhY          = kBaselineY - fm.xHeight;
    const float descenderY   = kBaselineY + fm.descent;
    const float underlineY   = kBaselineY - fm.underlinePosition;
    const float strikeY      = kBaselineY - fm.strikethroughPosition;

    // ---- metric lines (drawn first, behind the glyphs) ----
    auto brushAsc    = bigRT.createSolidColorBrush(colorFromHex(0x1565C0u));        // blue
    auto brushCap    = bigRT.createSolidColorBrush(colorFromHex(0x2E7D32u));        // green
    auto brushXH     = bigRT.createSolidColorBrush(colorFromHex(0xE65100u));        // orange
    auto brushBase   = bigRT.createSolidColorBrush(Colors::Red);
    auto brushDesc   = bigRT.createSolidColorBrush(colorFromHex(0x6A1B9Au));        // purple
    auto brushUL     = bigRT.createSolidColorBrush(colorFromHex(0x00838Fu, 0.7f));  // cyan
    auto brushST     = bigRT.createSolidColorBrush(colorFromHex(0xAD1457u, 0.7f));  // magenta

    const float W = static_cast<float>(kW);

    bigRT.drawLine({0.f, ascenderY},  {W, ascenderY},  brushAsc,  1.f);
    bigRT.drawLine({0.f, capY},       {W, capY},        brushCap,  1.f);
    bigRT.drawLine({0.f, xhY},        {W, xhY},         brushXH,   1.f);
    bigRT.drawLine({0.f, kBaselineY}, {W, kBaselineY},  brushBase, 1.f);
    bigRT.drawLine({0.f, descenderY}, {W, descenderY},  brushDesc, 1.f);

    // Underline and strikethrough as thicker dashed strokes.
    StrokeStyleProperties dashProps;
    dashProps.dashStyle = DashStyle::Dash;
    auto dashStyle = bigRT.getFactory().createStrokeStyle(dashProps);
    bigRT.drawLine({0.f, underlineY}, {W, underlineY}, brushUL, 2.f, dashStyle);
    bigRT.drawLine({0.f, strikeY},    {W, strikeY},    brushST, 2.f, dashStyle);

    // ---- glyphs: black text drawn on top of the lines ----
    auto textBrush = bigRT.createSolidColorBrush(Colors::Black);
    // Position layout rect so text baseline lands exactly at kBaselineY.
    const Rect layoutRect{4.f, ascenderY, W, descenderY};
    bigRT.drawTextU("Hfgx", tf, layoutRect, textBrush);

    // ---- labels (small Arial, right-aligned, matching line colour) ----
    TextFormat labelTF;
    dxFactory->createTextFormat(
        "Arial", FontWeight::Regular, FontStyle::Normal, FontStretch::Normal,
        9.f, static_cast<int32_t>(FontFlags::BodyHeight), AccessPtr::put(labelTF));
    labelTF.setTextAlignment(TextAlignment::Trailing);
    FontMetrics labelFm = labelTF.getFontMetrics();
    const float labelW = 80.f;
    const float labelX = W - labelW;
    auto labelY = [&](float lineY) { return lineY - labelFm.capHeight - 1.f; };

    bigRT.drawTextU("ascender",    labelTF, {labelX, labelY(ascenderY),  W, ascenderY},  brushAsc);
    bigRT.drawTextU("cap-height",  labelTF, {labelX, labelY(capY),       W, capY},       brushCap);
    bigRT.drawTextU("x-height",    labelTF, {labelX, labelY(xhY),        W, xhY},        brushXH);
    bigRT.drawTextU("baseline",    labelTF, {labelX, labelY(kBaselineY), W, kBaselineY}, brushBase);
    bigRT.drawTextU("descender",   labelTF, {labelX, descenderY + 1.f,   W, descenderY + labelFm.ascent + 1.f}, brushDesc);
    bigRT.drawTextU("underline",   labelTF, {labelX, labelY(underlineY), W, underlineY}, brushUL);
    bigRT.drawTextU("strikethrough",labelTF,{labelX, labelY(strikeY),    W, strikeY},    brushST);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("fontMetricsVisual", bigRT, 12));
}
