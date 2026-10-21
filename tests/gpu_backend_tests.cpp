// Tests specific to the GPU backend (backends/GpuGfx.h): the seams between
// GPU surfaces and the CPU-side helpers that lock, modify and re-upload them.
// Compiled only with -DGMPI_UI_TESTS_GPU_BACKEND=ON.

#include "DrawingTestContext.h"

#if GMPI_UI_TESTS_BACKEND_GPU

#include "DrawingTestFixture.h"
#include "helpers/BitmapMask.h"
#include "backends/GpuGfx.h"

#include <gtest/gtest.h>

#include <cstdio>

using namespace gmpi::drawing;

namespace
{

// Premultiplied linear fp16 RGBA at (x, y) of a locked target.
std::array<float, 4> pixelAt(BitmapPixels& px, uint32_t x, uint32_t y)
{
    EXPECT_EQ(px.getBytesPerPixel(), 8);
    const auto* h = reinterpret_cast<const uint16_t*>(px.getAddress() + y * px.getBytesPerRow() + x * 8);
    return { detail::halfToFloat(h[0]), detail::halfToFloat(h[1]), detail::halfToFloat(h[2]), detail::halfToFloat(h[3]) };
}

} // namespace

TEST_F(DrawingTest, GpuLockReadsBackWhatWasDrawn)
{
    auto brush = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 1.0f });
    g.fillRectangle(Rect{ 8.0f, 8.0f, 24.0f, 24.0f }, brush);
    g.endDraw();
    drawingActive = false;

    auto bitmap = g.getBitmap();
    auto px = bitmap.lockPixels(BitmapLockFlags::Read);
    const auto inside = pixelAt(px, 16, 16);
    const auto outside = pixelAt(px, 40, 40);
    EXPECT_NEAR(inside[0], 0.0f, 0.01f);
    EXPECT_NEAR(inside[2], 1.0f, 0.01f);
    EXPECT_NEAR(inside[3], 1.0f, 0.01f);
    EXPECT_NEAR(outside[0], 1.0f, 0.01f); // the white clear
    EXPECT_NEAR(outside[3], 1.0f, 0.01f);
}

// A ReadWrite lock's modifications must land on the GPU: a second lock, and
// a drawBitmap of the target, see them.
TEST_F(DrawingTest, GpuWriteLockRoundTrips)
{
    g.endDraw();
    drawingActive = false;

    auto bitmap = g.getBitmap();
    {
        auto px = bitmap.lockPixels(BitmapLockFlags::ReadWrite);
        uint8_t* data = px.getAddress();
        const int32_t bpr = px.getBytesPerRow();
        // Paint rows 0..15 opaque green, premultiplied.
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                auto* h = reinterpret_cast<uint16_t*>(data + y * bpr + x * 8);
                h[0] = detail::floatToHalf(0.0f);
                h[1] = detail::floatToHalf(1.0f);
                h[2] = detail::floatToHalf(0.0f);
                h[3] = detail::floatToHalf(1.0f);
            }
    }

    {
        auto px = bitmap.lockPixels(BitmapLockFlags::Read);
        const auto painted = pixelAt(px, 10, 5);
        const auto untouched = pixelAt(px, 10, 40);
        EXPECT_NEAR(painted[1], 1.0f, 0.01f);
        EXPECT_NEAR(painted[0], 0.0f, 0.01f);
        EXPECT_NEAR(untouched[0], 1.0f, 0.01f);
        EXPECT_NEAR(untouched[1], 1.0f, 0.01f);
    }

    // Drawing the target into another target carries the modification too.
    auto other = drawingContext.createCpuRenderTarget({ kWidth, kHeight }, kRenderFlags);
    other.beginDraw();
    other.clear(Colors::Black);
    other.drawBitmap(bitmap, Rect{ 0.0f, 0.0f, float(kWidth), float(kHeight) },
                     Rect{ 0.0f, 0.0f, float(kWidth), float(kHeight) });
    other.endDraw();
    auto px = other.getBitmap().lockPixels(BitmapLockFlags::Read);
    const auto copied = pixelAt(px, 10, 5);
    EXPECT_NEAR(copied[1], 1.0f, 0.01f);
    EXPECT_NEAR(copied[0], 0.0f, 0.01f);
}

// The exact sequence of EightBitMaskBitmapToOutput up to the mask: a
// CpuReadable colour target masked in place by an 8-bit mask target.
TEST_F(DrawingTest, GpuApplyMaskRoundTrips)
{
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask | (int32_t)BitmapRenderTargetFlags::CpuReadable;
    constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;
    auto factory = g.getFactory();

    auto maskRT = factory.createCpuRenderTarget({ kWidth, kHeight }, kMask);
    maskRT.beginDraw();
    maskRT.clear(Color{ 0.f, 0.f, 0.f, 0.f });
    auto maskBrush = maskRT.createSolidColorBrush(Colors::White);
    maskRT.fillEllipse(gmpi::drawing::Ellipse{ { 32.f, 32.f }, 24.f, 24.f }, maskBrush);
    maskRT.endDraw();

    auto colorRT = factory.createCpuRenderTarget({ kWidth, kHeight }, kColorCpu);
    colorRT.beginDraw();
    colorRT.clear(Colors::DodgerBlue);
    colorRT.endDraw();

    auto maskBmp = maskRT.getBitmap();
    auto colorBmp = colorRT.getBitmap();
    applyMask(colorBmp, maskBmp);

    // Transparent pixels written back must stay transparent: the composite
    // must not keep showing last frame's blue through them.
    auto px = colorBmp.lockPixels(BitmapLockFlags::Read);
    const auto corner = pixelAt(px, 0, 0);
    const auto centre = pixelAt(px, 32, 32);
    EXPECT_NEAR(corner[3], 0.0f, 0.01f) << "outside the ellipse the mask is 0";
    EXPECT_NEAR(centre[3], 1.0f, 0.01f);
    EXPECT_NEAR(centre[2], 1.0f, 0.01f) << "DodgerBlue keeps its blue";
}
#endif

#if GMPI_UI_TESTS_BACKEND_GPU
// A target keeps its pixels across beginDraw/endDraw pairs and never
// composites a shape twice, even when it is read back mid-frame.
TEST_F(DrawingTest, GpuRetainsPixelsAndDrawsEachShapeOnce)
{
    auto half = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.5f });
    g.fillRectangle(Rect{ 0.0f, 0.0f, 32.0f, 32.0f }, half);

    // Mid-frame read-back (what CachedBlur/applyMask do).
    auto bitmap = g.getBitmap();
    float greyBefore;
    {
        auto px = bitmap.lockPixels(BitmapLockFlags::Read);
        greyBefore = pixelAt(px, 16, 16)[0];
    }

    auto blue = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 1.0f });
    g.fillRectangle(Rect{ 40.0f, 40.0f, 60.0f, 60.0f }, blue);
    g.endDraw();
    drawingActive = false;

    // A second frame that draws nothing over the grey.
    g.beginDraw();
    g.fillRectangle(Rect{ 40.0f, 0.0f, 60.0f, 20.0f }, blue);
    g.endDraw();

    auto px = bitmap.lockPixels(BitmapLockFlags::Read);
    const auto grey = pixelAt(px, 16, 16);
    const auto b1 = pixelAt(px, 50, 50);
    const auto b2 = pixelAt(px, 50, 10);
    EXPECT_NEAR(greyBefore, 0.5f, 0.01f) << "50% black over white";
    EXPECT_NEAR(grey[0], 0.5f, 0.01f) << "not darkened by later frames or read-backs";
    EXPECT_NEAR(b1[2], 1.0f, 0.01f) << "first frame's shape kept";
    EXPECT_NEAR(b2[2], 1.0f, 0.01f) << "second frame's shape drawn";
}
#endif

#if GMPI_UI_TESTS_BACKEND_GPU
// Diagnostic: runs after the drawing tests in this process and reports which
// stage loses a channel once the "blue channel zeroed" state has set in.
TEST_F(DrawingTest, GpuChannelProbe)
{
    auto probe = [&](const char* what, gmpi::drawing::Bitmap bitmap) {
        auto px = bitmap.lockPixels(BitmapLockFlags::Read);
        const auto p = pixelAt(px, 32, 32);
        std::printf("[probe] %-28s r %.3f g %.3f b %.3f a %.3f\n", what, p[0], p[1], p[2], p[3]);
        return p;
    };

    // (a) the fixture's white clear, as is
    g.endDraw();
    drawingActive = false;
    const auto a = probe("clear white", g.getBitmap());

    // (b) a solid white fill on a fresh target (GPU path)
    auto t1 = drawingContext.createCpuRenderTarget({ kWidth, kHeight }, kRenderFlags);
    t1.beginDraw();
    t1.clear(Colors::Black);
    t1.fillRectangle(Rect{ 0, 0, 64, 64 }, t1.createSolidColorBrush(Colors::White));
    t1.endDraw();
    const auto b = probe("gpu fill white", t1.getBitmap());

    // (c) a white image through the fallback upload
    auto t2 = drawingContext.createCpuRenderTarget({ kWidth, kHeight }, kRenderFlags);
    auto src = drawingContext.factory().createCpuRenderTarget({ 8, 8 }, (int32_t)BitmapRenderTargetFlags::CpuReadable | (int32_t)BitmapRenderTargetFlags::SRGBPixels);
    src.beginDraw();
    src.clear(Colors::White);
    src.endDraw();
    t2.beginDraw();
    t2.clear(Colors::Black);
    t2.drawBitmap(src.getBitmap(), Rect{ 0, 0, 64, 64 }, Rect{ 0, 0, 8, 8 });
    t2.endDraw();
    const auto c = probe("fallback image white", t2.getBitmap());

    // (d) the fork directly: a fresh windowless canvas, white fill, screenshot
    {
        visage::Canvas canvas;
        canvas.setWindowless(16, 16);
        canvas.setClampBounds(0, 0, 16, 16);
        canvas.setBlendMode(visage::BlendMode::Opaque);
        canvas.setColor(visage::Color(1.0f, 1.0f, 1.0f, 1.0f));
        canvas.fill(0, 0, 16, 16);
        canvas.submit();
        visage::Color s = canvas.takeScreenshot().sample(8, 8);
        std::printf("[probe] %-28s r %d g %d b %d\n", "fork canvas white", s.hexRed(), s.hexGreen(), s.hexBlue());
    }
    EXPECT_NEAR(a[2], 1.0f, 0.02f);
    EXPECT_NEAR(b[2], 1.0f, 0.02f);
    EXPECT_NEAR(c[2], 1.0f, 0.02f);
}
#endif
