// Milestone-1 tests for the pure-software CPU backend (backends/CpuGfx.h).
//
// These do not use the golden-image fixture: the CPU backend's output is
// asserted directly with fp16 pixel probes (exact values are knowable since
// the pipeline is premultiplied linear throughout). Rendered PNGs are also
// saved under reference_images/cpu_backend_preview/ for eyeballing.
//
// The shapes deliberately route through the generic geometry paths
// (fillRectangle -> rect path, fillEllipse -> arcs -> beziers -> lines) to
// prove the "no specialised code paths" rule end to end.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include "Drawing.h"
#include "backends/CpuGfx.h"
#include "helpers/SavePng.h"
#include "DrawingTestContext.h"

#ifdef _WIN32
#include <objbase.h> // savePng encodes via WIC, which needs COM on this thread
#endif

using namespace gmpi::drawing;

namespace
{

struct CpuTestContext
{
    gmpi::cpugfx::Factory factoryImpl;
    Factory factory;

    CpuTestContext()
    {
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        *AccessPtr::put(factory) = &factoryImpl;
    }

    ~CpuTestContext()
    {
#ifdef _WIN32
        CoUninitialize();
#endif
    }

    BitmapRenderTarget createRenderTarget(uint32_t w = 64, uint32_t h = 64)
    {
        return factory.createCpuRenderTarget({ w, h }, 0);
    }
};

std::array<float, 4> readPixel(Bitmap& bmp, int x, int y)
{
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const uint8_t* addr = px.getAddress();
    const int32_t bpr = px.getBytesPerRow();
    const uint16_t* h = reinterpret_cast<const uint16_t*>(addr + size_t(y) * bpr + size_t(x) * 8);
    return { detail::halfToFloat(h[0]), detail::halfToFloat(h[1]),
             detail::halfToFloat(h[2]), detail::halfToFloat(h[3]) };
}

void expectPixel(Bitmap& bmp, int x, int y, std::array<float, 4> want, float tol = 2e-3f)
{
    const auto got = readPixel(bmp, x, y);
    for (int c = 0; c < 4; ++c)
        EXPECT_NEAR(got[c], want[c], tol) << "channel " << c << " at (" << x << ", " << y << ")";
}

void savePreview(const char* name, Bitmap bmp)
{
    EXPECT_TRUE(savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / name, bmp))
        << "savePng failed for " << name;
}

constexpr std::array<float, 4> kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
constexpr std::array<float, 4> kRed{ 1.0f, 0.0f, 0.0f, 1.0f }; // premultiplied

} // namespace

TEST(CpuBackend, ClearAndFillRectangle)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();
    ASSERT_NE(AccessPtr::get(rt), nullptr);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);
    rt.fillRectangle({ 8.0f, 8.0f, 40.0f, 40.0f }, red); // routes through path geometry (Gfx_base)
    rt.endDraw();

    auto bmp = rt.getBitmap();
    expectPixel(bmp, 4, 4, kWhite);    // untouched background
    expectPixel(bmp, 20, 20, kRed);    // interior
    expectPixel(bmp, 7, 20, kWhite);   // last pixel left of the rect
    expectPixel(bmp, 8, 20, kRed);     // first covered column (integer edge = no AA fringe)
    expectPixel(bmp, 39, 20, kRed);    // last covered column
    expectPixel(bmp, 40, 20, kWhite);  // first pixel right of the rect
    savePreview("fill_rect.png", bmp);
}

TEST(CpuBackend, AlphaBlendsInLinearSpace)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();

    rt.beginDraw();
    rt.clear(Colors::White);
    auto halfBlack = rt.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.5f });
    rt.fillRectangle({ 8.0f, 8.0f, 56.0f, 56.0f }, halfBlack);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    // Premultiplied over in linear light: 0.5-alpha black over white = 0.5 grey, alpha 1.
    expectPixel(bmp, 32, 32, { 0.5f, 0.5f, 0.5f, 1.0f });
    savePreview("alpha_blend.png", bmp);
}

TEST(CpuBackend, PentagramNonzeroVersusEvenOdd)
{
    CpuTestContext ctx;

    // Self-intersecting 5-point star: winding number is 2 at the centre,
    // 1 in the spikes. Nonzero fills the centre; even-odd leaves it hollow.
    constexpr float pi = 3.14159265358979f;
    Point pts[5];
    for (int k = 0; k < 5; ++k)
    {
        const float ang = -pi / 2.0f + float(k) * (4.0f * pi / 5.0f);
        pts[k] = { 32.0f + 24.0f * std::cos(ang), 32.0f + 24.0f * std::sin(ang) };
    }

    for (const auto fillMode : { FillMode::Winding, FillMode::Alternate })
    {
        auto rt = ctx.createRenderTarget();
        rt.beginDraw();
        rt.clear(Colors::White);

        auto geometry = ctx.factory.createPathGeometry();
        auto sink = geometry.open();
        sink.setFillMode(fillMode);
        sink.beginFigure(pts[0], FigureBegin::Filled);
        for (int k = 1; k < 5; ++k)
            sink.addLine(pts[k]);
        sink.endFigure(FigureEnd::Closed);
        sink.close();

        auto red = rt.createSolidColorBrush(Colors::Red);
        rt.fillGeometry(geometry, red);
        rt.endDraw();

        auto bmp = rt.getBitmap();
        expectPixel(bmp, 32, 12, kRed); // top spike: winding 1, filled either way
        expectPixel(bmp, 32, 32, fillMode == FillMode::Winding ? kRed : kWhite); // centre: winding 2
        expectPixel(bmp, 4, 4, kWhite);
        savePreview(fillMode == FillMode::Winding ? "star_nonzero.png" : "star_evenodd.png", bmp);
    }
}

TEST(CpuBackend, FillEllipseThroughGeometryRouting)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();

    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);
    rt.fillEllipse({ { 32.0f, 32.0f }, 20.0f, 20.0f }, red); // arcs -> cubics -> lines -> coverage
    rt.endDraw();

    auto bmp = rt.getBitmap();
    expectPixel(bmp, 32, 32, kRed);  // centre
    expectPixel(bmp, 4, 4, kWhite);  // outside
    expectPixel(bmp, 32, 20, kRed);  // well inside the top edge

    // Antialiasing: somewhere on the left edge of row 32 there must be a
    // partially covered pixel. Red over white keeps r = 1; blue tells coverage.
    bool foundPartial = false;
    for (int x = 9; x <= 14 && !foundPartial; ++x)
    {
        const auto p = readPixel(bmp, x, 32);
        foundPartial = p[2] > 0.02f && p[2] < 0.98f;
    }
    EXPECT_TRUE(foundPartial) << "no antialiased pixel found on the ellipse edge";
    savePreview("fill_ellipse.png", bmp);
}

TEST(CpuBackend, FillRoundedRectangle)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();

    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);
    rt.fillRoundedRectangle({ { 8.0f, 8.0f, 56.0f, 56.0f }, 8.0f, 8.0f }, red);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    expectPixel(bmp, 32, 32, kRed);  // interior
    expectPixel(bmp, 9, 9, kWhite);  // clipped corner (outside the corner radius)
    expectPixel(bmp, 12, 12, kRed);  // inside the corner radius
    savePreview("rounded_rect.png", bmp);
}

TEST(CpuBackend, TransformTranslatesGeometry)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();

    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);

    const Matrix3x2 translate{ 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 5.0f };
    AccessPtr::get(rt)->setTransform(&translate);
    rt.fillRectangle({ 0.0f, 0.0f, 8.0f, 8.0f }, red); // device space: (10,5)-(18,13)
    const Matrix3x2 identity;
    AccessPtr::get(rt)->setTransform(&identity);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    expectPixel(bmp, 14, 9, kRed);   // inside the translated rect
    expectPixel(bmp, 5, 3, kWhite);  // where the untranslated rect would have been
    expectPixel(bmp, 11, 6, kRed);
    expectPixel(bmp, 19, 9, kWhite); // right of the translated rect
    savePreview("transform_translate.png", bmp);
}

TEST(CpuBackend, ReviewRegressions)
{
    CpuTestContext ctx;
    auto rt = ctx.createRenderTarget();
    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);
    auto factory = rt.getFactory();

    // Heap-underflow repro from the adversarial review: the per-row
    // interpolated x rounds an ulp below 0 on raster row 0 (before the fix,
    // a write to acc[-1]). Passes silently in normal builds; ASan catches it.
    {
        auto g = factory.createPathGeometry();
        auto sink = g.open();
        sink.beginFigure({ 1.0f, -16.0f }, FigureBegin::Filled);
        sink.addLine({ 0.0f, 9.5367431640625e-7f }); // 2^-20
        sink.addLine({ 2.0f, -16.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(g, red);
    }

    // Unbalanced pops must not crash or empty the clip stack (D2D reports the
    // imbalance at EndDraw but keeps rendering).
    rt.popAxisAlignedClip();
    rt.popAxisAlignedClip();
    rt.fillRectangle({ 8.0f, 8.0f, 16.0f, 16.0f }, red);

    // Hollow figures are never filled (D2D semantics).
    {
        auto g = factory.createPathGeometry();
        auto sink = g.open();
        sink.beginFigure({ 40.0f, 40.0f }, FigureBegin::Hollow);
        sink.addLine({ 60.0f, 40.0f });
        sink.addLine({ 40.0f, 60.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(g, red);
    }
    rt.endDraw();

    auto bmp = rt.getBitmap();
    expectPixel(bmp, 10, 10, kRed);   // rendering survived the unbalanced pops
    expectPixel(bmp, 45, 45, kWhite); // hollow figure not filled
}

TEST(CpuBackend, FillContainsPoint)
{
    CpuTestContext ctx;

    auto geometry = ctx.factory.createPathGeometry();
    auto sink = geometry.open();
    sink.beginFigure({ 10.0f, 10.0f }, FigureBegin::Filled);
    sink.addLine({ 50.0f, 10.0f });
    sink.addLine({ 50.0f, 50.0f });
    sink.addLine({ 10.0f, 50.0f });
    sink.endFigure(FigureEnd::Closed);
    sink.close();

    bool contains{};
    AccessPtr::get(geometry)->fillContainsPoint({ 30.0f, 30.0f }, nullptr, &contains);
    EXPECT_TRUE(contains);
    AccessPtr::get(geometry)->fillContainsPoint({ 5.0f, 30.0f }, nullptr, &contains);
    EXPECT_FALSE(contains);
}

TEST(CpuBackend, SavePngHandlesPaddedRowStride)
{
    CpuTestContext ctx;

    // 11 wide: the CPU backend pads stride to 16 pixels (multiple of 8), so
    // bytesPerRow / width (128 / 11 = 11) misreports bytes-per-pixel. savePng
    // must take bpp from the pixel format for the PNG to come out right.
    constexpr uint32_t kW = 11, kH = 7;

    auto rt = ctx.createRenderTarget(kW, kH);
    ASSERT_NE(AccessPtr::get(rt), nullptr);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto red = rt.createSolidColorBrush(Colors::Red);
    rt.fillRectangle({ 0.0f, 0.0f, 4.0f, 7.0f }, red); // integer edges = no AA fringe
    rt.endDraw();

    auto bmp = rt.getBitmap();

    { // premise: rows really are padded, else this test proves nothing
        auto px = bmp.lockPixels(BitmapLockFlags::Read);
        ASSERT_GT(px.getBytesPerRow(), int32_t(kW) * px.getBytesPerPixel());
    }
    expectPixel(bmp, 2, 3, kRed);
    expectPixel(bmp, 10, 6, kWhite);

    const auto path = std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "padded_stride_11x7.png";
    ASSERT_TRUE(savePng(path, bmp)) << "savePng failed for " << path;

    // Round-trip: reload the PNG with the platform factory and verify every
    // pixel of the pattern survived.
    DrawingTestContext platform;
    auto png = platform.factory().loadImageU(path.string());
    ASSERT_TRUE(png) << "could not reload " << path;
    ASSERT_EQ(png.getSize().width, kW);
    ASSERT_EQ(png.getSize().height, kH);

    auto px = png.lockPixels(BitmapLockFlags::Read);
    const uint8_t* addr = px.getAddress();
    const int32_t  bpr  = px.getBytesPerRow();
    const int32_t  bpp  = px.getBytesPerPixel();
    const bool     bgra = px.channelLayout() == 0;

    // Decode a loaded-PNG pixel to sRGB RGBA bytes. Windows/macOS lock loaded
    // PNGs as 32bpp sRGB int; JUCE (Linux) as premultiplied linear RGBA half.
    auto readSRGB = [&](uint32_t x, uint32_t y) -> std::array<int, 4>
    {
        const uint8_t* p = addr + size_t(y) * bpr + size_t(x) * bpp;
        if (bpp == 4)
            return { p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2], p[3] };

        const uint16_t* h = reinterpret_cast<const uint16_t*>(p);
        float fr = detail::halfToFloat(h[0]);
        float fg = detail::halfToFloat(h[1]);
        float fb = detail::halfToFloat(h[2]);
        const float fa = detail::halfToFloat(h[3]);
        if (fa > 0.0f) { fr /= fa; fg /= fa; fb /= fa; }
        return { detail::linearToSRGB_f(fr), detail::linearToSRGB_f(fg), detail::linearToSRGB_f(fb),
                 static_cast<int>(std::clamp(fa * 255.0f + 0.5f, 0.0f, 255.0f)) };
    };

    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const auto got = readSRGB(x, y);
            const std::array<int, 4> want = (x < 4)
                ? std::array<int, 4>{ 255, 0, 0, 255 }      // red columns
                : std::array<int, 4>{ 255, 255, 255, 255 }; // white background
            for (int c = 0; c < 4; ++c)
                EXPECT_NEAR(got[c], want[c], 2) << "channel " << c << " at (" << x << ", " << y << ")";
        }
    }
}
