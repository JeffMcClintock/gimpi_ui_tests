// Milestone-2 cross-backend tests: render identical scenes through the
// Direct2D backend (DrawingTestContext) and the pure-software CPU backend
// (backends/CpuGfx.h) and compare, treating D2D as the reference
// implementation. Runs on Windows only.
//
// Comparison is done in sRGB 8-bit (the same decode as the golden-image
// fixture): interior pixels must match near-exactly; antialiased edge pixels
// may differ by a few code values because the two rasterizers compute
// coverage slightly differently (D2D's exact analytic AA vs our signed-area
// midpoint rule, plus curve-flattening tolerance for arcs/beziers).
// Per-scene stats print so drift is visible even while tests pass.

#ifdef _WIN32

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "Drawing.h"
#include "backends/CpuGfx.h"
#include "helpers/SavePng.h"
#include "DrawingTestContext.h"

using namespace gmpi::drawing;

namespace
{

constexpr float kPi = 3.14159265358979f;

Matrix3x2 makeRotationAbout(float radians, float cx, float cy)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return { c, s, -s, c,
             cx - c * cx + s * cy,
             cy - s * cx - c * cy };
}

// Decode one fp16 premultiplied-linear RGBA pixel to sRGB u8 (straight alpha).
std::array<uint8_t, 4> decodeToSRGB(const uint8_t* addr, int32_t bpr, int x, int y)
{
    const uint16_t* h = reinterpret_cast<const uint16_t*>(addr + size_t(y) * bpr + size_t(x) * 8);
    float r = detail::halfToFloat(h[0]);
    float g = detail::halfToFloat(h[1]);
    float b = detail::halfToFloat(h[2]);
    const float a = detail::halfToFloat(h[3]);

    const uint8_t a8 = static_cast<uint8_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
    if (a > 0.0f)
    {
        r = std::clamp(r / a, 0.0f, 1.0f);
        g = std::clamp(g / a, 0.0f, 1.0f);
        b = std::clamp(b / a, 0.0f, 1.0f);
    }
    else
    {
        r = g = b = 0.0f;
    }
    return { detail::linearToSRGB_f(r), detail::linearToSRGB_f(g), detail::linearToSRGB_f(b), a8 };
}

struct DiffStats
{
    int totalPixels{};
    int diffCount{};
    int maxChanDiff{};
    double diffPercent{};
    double meanDiffBad{}; // mean per-channel diff across differing pixels only
};

DiffStats compareBitmaps(Bitmap& cpu, Bitmap& ref, int tolerance)
{
    const auto sizeA = cpu.getSize();
    const auto sizeB = ref.getSize();
    EXPECT_EQ(sizeA.width, sizeB.width);
    EXPECT_EQ(sizeA.height, sizeB.height);

    auto pxA = cpu.lockPixels(BitmapLockFlags::Read);
    auto pxB = ref.lockPixels(BitmapLockFlags::Read);

    DiffStats stats;
    stats.totalPixels = int(sizeA.width * sizeA.height);
    int64_t totalDiffBad = 0;

    for (uint32_t y = 0; y < sizeA.height; ++y)
    {
        for (uint32_t x = 0; x < sizeA.width; ++x)
        {
            const auto a = decodeToSRGB(pxA.getAddress(), pxA.getBytesPerRow(), int(x), int(y));
            const auto b = decodeToSRGB(pxB.getAddress(), pxB.getBytesPerRow(), int(x), int(y));

            int pixelMax = 0;
            int pixelSum = 0;
            for (int c = 0; c < 4; ++c)
            {
                const int d = std::abs(int(a[c]) - int(b[c]));
                pixelMax = (std::max)(pixelMax, d);
                pixelSum += d;
            }
            stats.maxChanDiff = (std::max)(stats.maxChanDiff, pixelMax);
            if (pixelMax > tolerance)
            {
                ++stats.diffCount;
                totalDiffBad += pixelSum;
            }
        }
    }

    stats.diffPercent = 100.0 * stats.diffCount / stats.totalPixels;
    stats.meanDiffBad = stats.diffCount ? double(totalDiffBad) / (stats.diffCount * 4) : 0.0;
    return stats;
}

// Render `scene` through both backends and compare.
// tolerance:      per-channel u8 diff below which a pixel counts as matching.
// maxMeanDiff:    cap on mean per-channel diff across differing pixels.
// maxDiffPercent: cap on the fraction of differing pixels (AA edges).
void runScene(const std::string& name,
              const std::function<void(BitmapRenderTarget&)>& scene,
              uint32_t width = 64, uint32_t height = 64,
              int tolerance = 2,
              double maxMeanDiff = 12.0,   // ~2x the worst observed AA-edge jitter
              double maxDiffPercent = 8.0)
{
    // Reference: Direct2D.
    DrawingTestContext d2d; // CoInitializes; keeps COM alive for savePng below
    auto rtRef = d2d.createCpuRenderTarget({ width, height }, 0);
    rtRef.beginDraw();
    scene(rtRef);
    rtRef.endDraw();

    // Software backend.
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;
    auto rtCpu = cpuFactory.createCpuRenderTarget({ width, height }, 0);
    rtCpu.beginDraw();
    scene(rtCpu);
    rtCpu.endDraw();

    auto bmpRef = rtRef.getBitmap();
    auto bmpCpu = rtCpu.getBitmap();

    const auto stats = compareBitmaps(bmpCpu, bmpRef, tolerance);
    std::cout << "  [COMPARE] " << name
              << ": diff=" << stats.diffCount << "/" << stats.totalPixels
              << " (" << stats.diffPercent << "%)"
              << " meanBad=" << stats.meanDiffBad
              << " maxChan=" << stats.maxChanDiff << "\n";

    const bool ok = stats.meanDiffBad <= maxMeanDiff && stats.diffPercent <= maxDiffPercent;
    EXPECT_TRUE(ok) << name << ": CPU vs D2D mismatch — diff% " << stats.diffPercent
                    << " (max " << maxDiffPercent << "), meanBad " << stats.meanDiffBad
                    << " (max " << maxMeanDiff << "), maxChan " << stats.maxChanDiff;
    if (!ok)
    {
        const auto dir = std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview";
        savePng(dir / (name + "_cpu.png"), bmpCpu);
        savePng(dir / (name + "_d2d.png"), bmpRef);
    }
}

// Scene helpers ------------------------------------------------------------

void addStar(GeometrySink& sink, FillMode fillMode, float cx, float cy, float radius)
{
    Point pts[5];
    for (int k = 0; k < 5; ++k)
    {
        const float ang = -kPi / 2.0f + float(k) * (4.0f * kPi / 5.0f);
        pts[k] = { cx + radius * std::cos(ang), cy + radius * std::sin(ang) };
    }
    sink.setFillMode(fillMode);
    sink.beginFigure(pts[0], FigureBegin::Filled);
    for (int k = 1; k < 5; ++k)
        sink.addLine(pts[k]);
    sink.endFigure(FigureEnd::Closed);
}

PathGeometry makeStar(BitmapRenderTarget& rt, FillMode fillMode)
{
    auto factory = rt.getFactory();
    auto geometry = factory.createPathGeometry();
    auto sink = geometry.open();
    addStar(sink, fillMode, 32.0f, 32.0f, 24.0f);
    sink.close();
    return geometry;
}

} // namespace

TEST(CpuVsD2D, FractionalRect)
{
    runScene("x_fractional_rect", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Teal);
        rt.fillRectangle({ 8.3f, 8.7f, 40.6f, 39.2f }, brush);
    });
}

TEST(CpuVsD2D, RotatedSquare)
{
    runScene("x_rotated_square", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Navy);
        const auto m = makeRotationAbout(kPi / 6.0f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        rt.fillRectangle({ 16.0f, 16.0f, 48.0f, 48.0f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, StarNonzeroScaled)
{
    runScene("x_star_nonzero", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Red);
        const Matrix3x2 scale{ 1.4f, 0.0f, 0.0f, 0.8f, -10.0f, 6.0f };
        AccessPtr::get(rt)->setTransform(&scale);
        auto star = makeStar(rt, FillMode::Winding);
        rt.fillGeometry(star, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, StarEvenOddRotated)
{
    runScene("x_star_evenodd", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Red);
        const auto m = makeRotationAbout(0.35f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        auto star = makeStar(rt, FillMode::Alternate);
        rt.fillGeometry(star, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, EllipseFractional)
{
    // Curve flattening differs from D2D's native ellipse: allow more edge pixels.
    runScene("x_ellipse", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
        rt.fillEllipse({ { 31.7f, 32.4f }, 19.3f, 12.6f }, brush);
    });
}

TEST(CpuVsD2D, RoundedRectFractional)
{
    runScene("x_rounded_rect", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Orange);
        rt.fillRoundedRectangle({ { 8.5f, 8.5f, 55.25f, 50.75f }, 7.3f, 5.9f }, brush);
    });
}

TEST(CpuVsD2D, SliverTriangle)
{
    runScene("x_sliver", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        sink.beginFigure({ 10.0f, 4.0f }, FigureBegin::Filled);
        sink.addLine({ 11.0f, 60.0f });
        sink.addLine({ 10.5f, 4.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(geometry, brush);
    });
}

TEST(CpuVsD2D, TrapezoidHorizontalEdges)
{
    runScene("x_trapezoid", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Purple);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        sink.beginFigure({ 8.0f, 16.0f }, FigureBegin::Filled);
        sink.addLine({ 56.0f, 16.0f });
        sink.addLine({ 48.0f, 48.0f });
        sink.addLine({ 16.0f, 48.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(geometry, brush);
    });
}

TEST(CpuVsD2D, PartiallyOffSurface)
{
    runScene("x_partial_offsurface", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Teal);
        rt.fillRectangle({ -20.5f, -13.2f, 30.6f, 30.9f }, brush);
    });
}

TEST(CpuVsD2D, FullyOffSurface)
{
    runScene("x_fully_offsurface", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Red);
        rt.fillRectangle({ -50.0f, -50.0f, -10.0f, -10.0f }, brush);
        rt.fillRectangle({ 70.0f, 70.0f, 90.0f, 90.0f }, brush);
    }, 64, 64, 1, 1.0, 0.1); // must equal a plain clear
}

TEST(CpuVsD2D, AliasedFractionalClip)
{
    // Exercises the ALIASED pixel-centre rule on all four clip edges.
    runScene("x_aliased_clip", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Maroon);
        rt.pushAxisAlignedClip({ 10.5f, 10.5f, 50.25f, 49.75f });
        rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, brush);
        rt.popAxisAlignedClip();
    }, 64, 64, 1, 4.0, 2.0); // clip edges are aliased: near-exact match expected
}

TEST(CpuVsD2D, ClipWithRotatedFill)
{
    runScene("x_clip_rotated", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Navy);
        rt.pushAxisAlignedClip({ 12.3f, 10.7f, 52.6f, 53.2f });
        const auto m = makeRotationAbout(kPi / 5.0f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        rt.fillRectangle({ 10.0f, 10.0f, 54.0f, 54.0f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
        rt.popAxisAlignedClip();
    });
}

TEST(CpuVsD2D, NestedClips)
{
    runScene("x_nested_clips", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto red = rt.createSolidColorBrush(Colors::Red);
        auto blue = rt.createSolidColorBrush(Colors::Blue);
        rt.pushAxisAlignedClip({ 8.0f, 8.0f, 56.0f, 56.0f });
        rt.pushAxisAlignedClip({ 16.5f, 16.5f, 40.5f, 40.5f });
        rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, red);
        rt.popAxisAlignedClip();
        rt.fillRectangle({ 44.0f, 44.0f, 64.0f, 64.0f }, blue);
        rt.popAxisAlignedClip();
    }, 64, 64, 1, 4.0, 2.0);
}

TEST(CpuVsD2D, TranslucentStack)
{
    runScene("x_translucent_stack", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto a = rt.createSolidColorBrush(Color{ 1.0f, 0.0f, 0.0f, 0.5f });
        auto b = rt.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 0.4f });
        auto c = rt.createSolidColorBrush(Color{ 0.0f, 0.5f, 0.0f, 0.6f });
        rt.fillRectangle({ 6.0f, 6.0f, 40.0f, 40.0f }, a);
        rt.fillEllipse({ { 36.0f, 36.0f }, 18.0f, 14.0f }, b);
        auto star = makeStar(rt, FillMode::Winding);
        rt.fillGeometry(star, c);
    });
}

TEST(CpuVsD2D, DegenerateFiguresMixedWithNormal)
{
    // A single-point figure and a two-point (zero-area) figure must render
    // nothing; the normal triangle must render as if alone.
    runScene("x_degenerate_mix", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        sink.beginFigure({ 50.0f, 10.0f }, FigureBegin::Filled); // single point
        sink.endFigure(FigureEnd::Closed);
        sink.beginFigure({ 5.0f, 60.0f }, FigureBegin::Filled);  // zero-area line
        sink.addLine({ 60.0f, 5.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.beginFigure({ 12.0f, 12.0f }, FigureBegin::Filled); // normal triangle
        sink.addLine({ 52.0f, 20.0f });
        sink.addLine({ 20.0f, 52.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.beginFigure({ 40.0f, 40.0f }, FigureBegin::Hollow); // hollow: D2D does not fill it
        sink.addLine({ 60.0f, 40.0f });
        sink.addLine({ 40.0f, 60.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(geometry, brush);
    });
}

TEST(CpuVsD2D, HugeCoordinates)
{
    runScene("x_huge_coords", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::DarkRed);
        rt.fillRectangle({ -1.0e7f, -1.0e7f, 1.0e7f, 32.4f }, brush);
    });
}

TEST(CpuVsD2D, NegativeScaleFlip)
{
    runScene("x_flip_x", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Indigo);
        const Matrix3x2 flip{ -1.0f, 0.0f, 0.0f, 1.0f, 64.0f, 0.0f };
        AccessPtr::get(rt)->setTransform(&flip);
        rt.fillRectangle({ 8.3f, 8.3f, 30.6f, 30.6f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, PaddedStrideWidth)
{
    // Width 90 -> CPU stride pads to 96 pixels; D2D bytesPerRow stays tight.
    runScene("x_padded_stride", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Crimson);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        addStar(sink, FillMode::Winding, 45.0f, 20.0f, 17.0f);
        sink.close();
        rt.fillGeometry(geometry, brush);
        rt.fillRectangle({ 60.3f, 4.2f, 88.8f, 35.7f }, brush);
    }, 90, 40);
}

TEST(CpuVsD2D, ArcSweepAndLargeArc)
{
    // Exercises the sweep-direction and large-arc branches of the arc
    // flattener (only small clockwise arcs were covered before).
    runScene("x_arcs", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Color{ 0.27f, 0.5f, 0.7f, 1.0f });
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        sink.beginFigure({ 16.0f, 32.0f }, FigureBegin::Filled);
        sink.addArc({ { 48.0f, 32.0f }, { 16.0f, 10.0f }, 0.0f, SweepDirection::CounterClockwise, ArcSize::Small });
        sink.addArc({ { 16.0f, 32.0f }, { 16.0f, 13.0f }, 0.0f, SweepDirection::Clockwise, ArcSize::Large });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.fillGeometry(geometry, brush);
    }, 64, 64, 2, 16.0, 12.0); // curved edges: flattening-tolerance differences
}

TEST(CpuVsD2D, TransparentDestinationAlpha)
{
    // Destination alpha != 1: exercises the alpha lane of the blend.
    runScene("x_transparent_dest", [](BitmapRenderTarget& rt) {
        rt.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
        auto a = rt.createSolidColorBrush(Color{ 1.0f, 0.2f, 0.1f, 0.6f });
        auto b = rt.createSolidColorBrush(Color{ 0.1f, 0.3f, 1.0f, 0.5f });
        rt.fillRectangle({ 8.3f, 8.3f, 40.6f, 40.6f }, a);
        rt.fillEllipse({ { 36.0f, 36.0f }, 16.0f, 12.0f }, b);
    }, 64, 64, 2, 24.0, 8.0); // un-premultiplying near-zero-alpha AA pixels amplifies tiny diffs
}

TEST(CpuVsD2D, WindingBeyondTwo)
{
    // Three nested same-direction rects: winding 3 in the centre. Nonzero
    // saturates; even-odd alternates filled/hollow/filled.
    runScene("x_winding_deep", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Color{ 0.2f, 0.0f, 0.6f, 0.7f });
        auto factory = rt.getFactory();
        for (const auto fillMode : { FillMode::Winding, FillMode::Alternate })
        {
            auto geometry = factory.createPathGeometry();
            auto sink = geometry.open();
            sink.setFillMode(fillMode);
            for (int k = 0; k < 3; ++k)
            {
                const float inset = 6.0f + 6.0f * float(k);
                sink.beginFigure({ inset, inset }, FigureBegin::Filled);
                sink.addLine({ 64.0f - inset, inset });
                sink.addLine({ 64.0f - inset, 64.0f - inset });
                sink.addLine({ inset, 64.0f - inset });
                sink.endFigure(FigureEnd::Closed);
            }
            sink.close();
            rt.fillGeometry(geometry, brush);
        }
    });
}

// CPU-only robustness: NaN/Inf points must not crash and must not corrupt
// other figures. (No D2D comparison: D2D's behaviour with NaN is unspecified.)
TEST(CpuVsD2D, NonFinitePointsAreSkippedSafely)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;
    auto rt = cpuFactory.createCpuRenderTarget({ 64, 64 }, 0);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto brush = rt.createSolidColorBrush(Colors::Red);

    auto factory = rt.getFactory();
    auto geometry = factory.createPathGeometry();
    auto sink = geometry.open();
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    sink.beginFigure({ nan, 10.0f }, FigureBegin::Filled); // poisoned figure
    sink.addLine({ 30.0f, inf });
    sink.addLine({ 40.0f, 40.0f });
    sink.endFigure(FigureEnd::Closed);
    sink.beginFigure({ 10.0f, 10.0f }, FigureBegin::Filled); // valid triangle
    sink.addLine({ 50.0f, 10.0f });
    sink.addLine({ 10.0f, 50.0f });
    sink.endFigure(FigureEnd::Closed);
    sink.close();
    rt.fillGeometry(geometry, brush);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const uint8_t* addr = px.getAddress();
    const int32_t bpr = px.getBytesPerRow();
    auto at = [&](int x, int y) {
        const uint16_t* h = reinterpret_cast<const uint16_t*>(addr + size_t(y) * bpr + size_t(x) * 8);
        return std::array<float, 4>{ detail::halfToFloat(h[0]), detail::halfToFloat(h[1]),
                                     detail::halfToFloat(h[2]), detail::halfToFloat(h[3]) };
    };
    EXPECT_NEAR(at(15, 15)[1], 0.0f, 2e-3f) << "valid triangle should render (green=0 for red fill)";
    EXPECT_NEAR(at(15, 15)[0], 1.0f, 2e-3f);
    EXPECT_NEAR(at(60, 60)[0], 1.0f, 2e-3f) << "background must stay white";
    EXPECT_NEAR(at(60, 60)[2], 1.0f, 2e-3f);
}

#endif // _WIN32
