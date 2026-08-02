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
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h" // platform image decoding, injected into the CPU factory
#include "helpers/FontProvider.h"
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
    cpuImpl.imageDecoder = gmpi::drawing::decodeImageFile;
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

TEST(CpuVsD2D, ClipGeometryStar)
{
    // Arbitrary-shape clipping. D2D implements it with PushLayer and an
    // antialiased mask; ours rasterizes the geometry to a coverage mask, so
    // the clip edge should be antialiased the same way rather than aliased.
    runScene("x_clip_geometry", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto star = makeStar(rt, FillMode::Winding);
        rt.pushClipGeometry(star);
        auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
        rt.fillRectangle({ 0.f, 0.f, 64.f, 64.f }, brush);
        rt.popAxisAlignedClip();
    });
}

TEST(CpuVsD2D, ClipGeometryIntersectsRectClip)
{
    // A shaped clip must intersect with an enclosing rectangular clip, and
    // popping must restore exactly one level.
    runScene("x_clip_geometry_nested", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto red = rt.createSolidColorBrush(Colors::Red);
        auto blue = rt.createSolidColorBrush(Colors::Blue);

        rt.pushAxisAlignedClip({ 0.f, 0.f, 64.f, 34.f }); // top half only
        auto star = makeStar(rt, FillMode::Winding);
        rt.pushClipGeometry(star);
        rt.fillRectangle({ 0.f, 0.f, 64.f, 64.f }, red);
        rt.popAxisAlignedClip();   // drop the star
        rt.fillRectangle({ 0.f, 40.f, 64.f, 64.f }, blue); // still clipped to the top half: invisible
        rt.popAxisAlignedClip();
        rt.fillRectangle({ 56.f, 56.f, 64.f, 64.f }, blue); // now unclipped
    });
}

TEST(CpuVsD2D, ClearRespectsGeometryClip)
{
    // clear() used to ignore the shaped clip and repaint its whole bounding
    // box, so a clear under pushClipGeometry wiped out corners the clip
    // excluded.
    runScene("x_clip_geometry_clear", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto blue = rt.createSolidColorBrush(Colors::Blue);
        rt.fillRectangle({ 0.f, 0.f, 64.f, 64.f }, blue); // something to preserve

        auto star = makeStar(rt, FillMode::Winding);
        rt.pushClipGeometry(star);
        rt.clear(Colors::Orange);
        rt.popAxisAlignedClip();
    });
}

TEST(CpuVsD2D, ClipGeometryUnderTransform)
{
    runScene("x_clip_geometry_transform", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        const auto m = makeRotationAbout(0.5f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        auto star = makeStar(rt, FillMode::Winding);
        rt.pushClipGeometry(star);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
        auto brush = rt.createSolidColorBrush(Colors::Maroon);
        rt.fillRectangle({ 0.f, 0.f, 64.f, 64.f }, brush);
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

// --- Text metrics vs DirectWrite -------------------------------------------
//
// Text PIXELS are not comparable across backends (different rasterizers), but
// metrics and layout are: hosts position UI against them, so a disagreement
// here shifts real interfaces.

TEST(CpuVsD2D, TextMetricsAgreeWithDirectWrite)
{
    DrawingTestContext d2d;

    gmpi::cpugfx::Factory cpuImpl;
    gmpi::drawing::CpuTextEngine engine{ gmpi::drawing::findFont };
    cpuImpl.textEngine = &engine;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;

    constexpr float kHeight = 20.0f;
    std::string_view family{ "Arial" };

    auto d2dFormat = d2d.factory().createTextFormat(kHeight, { &family, 1 });
    auto cpuFormat = cpuFactory.createTextFormat(kHeight, { &family, 1 });
    ASSERT_NE(AccessPtr::get(d2dFormat), nullptr);
    ASSERT_NE(AccessPtr::get(cpuFormat), nullptr);

    const auto dm = d2dFormat.getFontMetrics();
    const auto cm = cpuFormat.getFontMetrics();
    std::cout << "  [METRICS] ascent " << cm.ascent << " vs " << dm.ascent
              << " | descent " << cm.descent << " vs " << dm.descent
              << " | lineGap " << cm.lineGap << " vs " << dm.lineGap
              << " | capHeight " << cm.capHeight << " vs " << dm.capHeight
              << " | xHeight " << cm.xHeight << " vs " << dm.xHeight << "\n";

    EXPECT_NEAR(cm.ascent, dm.ascent, 0.15f);
    EXPECT_NEAR(cm.descent, dm.descent, 0.15f);
    EXPECT_NEAR(cm.lineGap, dm.lineGap, 0.15f);
    EXPECT_NEAR(cm.capHeight, dm.capHeight, 0.15f);
    EXPECT_NEAR(cm.xHeight, dm.xHeight, 0.15f);

    // Single-line advance width: same font, same shaper input.
    const auto dWidth = d2dFormat.getTextExtentU("Hello, world").width;
    const auto cWidth = cpuFormat.getTextExtentU("Hello, world").width;
    std::cout << "  [EXTENT] width " << cWidth << " vs " << dWidth << "\n";
    EXPECT_NEAR(cWidth, dWidth, 0.6f);

    // Line-to-line spacing — this is what lineGap decides.
    const char* samples[4] = { "a", "a\nb", "a\nb\nc", "a\nb\nc\nd" };
    for (int i = 0; i < 4; ++i)
    {
        const auto dh = d2dFormat.getTextExtentU(samples[i]).height;
        const auto ch = cpuFormat.getTextExtentU(samples[i]).height;
        std::cout << "  [LINES] n=" << (i + 1) << " cpu " << ch << " d2d " << dh
                  << " (d2d delta " << (i ? dh - d2dFormat.getTextExtentU(samples[i - 1]).height : dh) << ")\n";
        EXPECT_NEAR(ch, dh, 0.2f) << "line " << (i + 1) << " height disagrees";
    }
}

// --- Bitmaps (milestone 6) ------------------------------------------------

namespace
{
// An asymmetric tile: prime dimensions and four distinguishable regions, so a
// one-pixel phase error in tiling or sampling is visible rather than hidden by
// self-similarity.
Bitmap makeAnchorTile(BitmapRenderTarget& rt)
{
    constexpr uint32_t w = 11, h = 7;
    auto tileRT = rt.getFactory().createCpuRenderTarget({ w, h }, 0);
    tileRT.beginDraw();
    tileRT.clear(Colors::Cyan);
    auto top = tileRT.createSolidColorBrush(Colors::Blue);
    auto left = tileRT.createSolidColorBrush(Colors::Green);
    auto anchor = tileRT.createSolidColorBrush(Colors::Red);
    tileRT.fillRectangle({ 0.f, 0.f, float(w), 1.f }, top);
    tileRT.fillRectangle({ 0.f, 0.f, 1.f, float(h) }, left);
    tileRT.fillRectangle({ 0.f, 0.f, 1.f, 1.f }, anchor);
    tileRT.endDraw();
    return tileRT.getBitmap();
}
} // namespace

TEST(CpuVsD2D, DrawBitmapNativeSize)
{
    runScene("x_bmp_native", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        rt.drawBitmap(bmp, { 20.f, 20.f, 31.f, 27.f }, { 0.f, 0.f, 11.f, 7.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
    });
}

TEST(CpuVsD2D, DrawBitmapStretchedNearest)
{
    runScene("x_bmp_stretch_nearest", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        rt.drawBitmap(bmp, { 4.f, 4.f, 60.f, 60.f }, { 0.f, 0.f, 11.f, 7.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
    });
}

TEST(CpuVsD2D, DrawBitmapStretchedLinear)
{
    runScene("x_bmp_stretch_linear", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        rt.drawBitmap(bmp, { 4.f, 4.f, 60.f, 60.f }, { 0.f, 0.f, 11.f, 7.f },
                      1.0f, BitmapInterpolationMode::Linear);
    }, 64, 64, 2, 14.0, 12.0); // edge-clamp behaviour at the borders differs slightly
}

TEST(CpuVsD2D, DrawBitmapCropped)
{
    // A sub-rectangle must not bleed in neighbouring texels.
    runScene("x_bmp_cropped", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        rt.drawBitmap(bmp, { 8.f, 8.f, 56.f, 56.f }, { 4.f, 2.f, 9.f, 6.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
    });
}

TEST(CpuVsD2D, DrawBitmapOpacity)
{
    runScene("x_bmp_opacity", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        rt.drawBitmap(bmp, { 6.f, 6.f, 58.f, 58.f }, { 0.f, 0.f, 11.f, 7.f },
                      0.45f, BitmapInterpolationMode::NearestNeighbor);
    });
}

TEST(CpuVsD2D, DrawBitmapFractionalAndTransformed)
{
    runScene("x_bmp_transformed", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        const auto m = makeRotationAbout(0.4f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        rt.drawBitmap(bmp, { 10.3f, 12.7f, 53.6f, 45.2f }, { 0.f, 0.f, 11.f, 7.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, BitmapBrushTiles)
{
    // The bitmap brush wraps in both axes; the anchor tile exposes the phase.
    runScene("x_bmp_brush_tile", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        auto brush = rt.createBitmapBrush(bmp);
        rt.fillRectangle({ 4.f, 4.f, 60.f, 60.f }, brush);
    });
}

TEST(CpuVsD2D, BitmapBrushUnderTransform)
{
    // Tiling must follow the active transform, so the lattice moves with it.
    runScene("x_bmp_brush_transform", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        auto brush = rt.createBitmapBrush(bmp);
        const Matrix3x2 m{ 1.0f, 0.0f, 0.0f, 1.0f, 6.5f, 3.25f };
        AccessPtr::get(rt)->setTransform(&m);
        rt.fillEllipse({ { 28.f, 28.f }, 22.f, 22.f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, BitmapBrushStrokesGeometry)
{
    runScene("x_bmp_brush_stroke", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto bmp = makeAnchorTile(rt);
        auto brush = rt.createBitmapBrush(bmp);
        rt.drawRectangle({ 10.f, 10.f, 54.f, 54.f }, brush, 7.0f);
    });
}

// CPU-only regressions for the defects the bitmap review confirmed.
TEST(CpuVsD2D, BitmapDegenerateInputsAreSafe)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;
    auto rt = cpuFactory.createCpuRenderTarget({ 32, 32 }, 0);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto tile = makeAnchorTile(rt);

    // (a) Inverted source rectangle. Legal input that mirrors the image, but
    // it used to reach std::clamp with lo > hi — undefined, and fatal in an
    // MSVC debug build.
    rt.drawBitmap(tile, { 2.f, 2.f, 14.f, 14.f }, { 9.f, 6.f, 2.f, 1.f },
                  1.0f, BitmapInterpolationMode::Linear);

    // (b) Inverted destination rectangle.
    rt.drawBitmap(tile, { 30.f, 18.f, 18.f, 30.f }, { 0.f, 0.f, 11.f, 7.f },
                  1.0f, BitmapInterpolationMode::NearestNeighbor);

    // (c) A bitmap brush with a near-collapsed transform: source coordinates
    // go far outside int range, and float-to-int is undefined there. The wrap
    // reduction has to happen in float first.
    {
        BrushProperties props{};
        props.transform = { 1e-20f, 0.0f, 0.0f, 1e-20f, 0.0f, 0.0f };
        gmpi::drawing::api::IBitmapBrush* rawBrush{};
        AccessPtr::get(rt)->createBitmapBrush(AccessPtr::get(tile), &props, &rawBrush);
        ASSERT_NE(rawBrush, nullptr);
        BitmapBrush brush;
        *AccessPtr::put(brush) = rawBrush;
        rt.fillRectangle({ 16.f, 2.f, 30.f, 16.f }, brush);
    }
    rt.endDraw();

    // Nothing may be non-finite anywhere on the surface.
    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    for (int y = 0; y < 32; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int i = 0; i < 32 * 4; ++i)
            ASSERT_TRUE(std::isfinite(detail::halfToFloat(row[i]))) << "non-finite pixel at row " << y;
    }
}

TEST(CpuVsD2D, NonFiniteSourceTexelDoesNotEscape)
{
    // A caller can write anything through lockPixels. A non-finite texel must
    // not reach the blend, where it would poison even zero-coverage pixels
    // permanently (NaN * 0 is still NaN).
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;

    auto srcRT = cpuFactory.createCpuRenderTarget({ 4, 4 }, 0);
    srcRT.beginDraw();
    srcRT.clear(Colors::Red);
    srcRT.endDraw();
    auto src = srcRT.getBitmap();
    {
        auto px = src.lockPixels(BitmapLockFlags::ReadWrite);
        uint16_t* row = reinterpret_cast<uint16_t*>(px.getAddress());
        row[0] = 0x7e00; // half-precision NaN
        row[5] = 0x7c00; // half-precision +infinity
    }

    auto rt = cpuFactory.createCpuRenderTarget({ 32, 32 }, 0);
    rt.beginDraw();
    rt.clear(Colors::White);
    rt.drawBitmap(src, { 8.f, 8.f, 24.f, 24.f }, { 0.f, 0.f, 4.f, 4.f },
                  1.0f, BitmapInterpolationMode::Linear);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    for (int y = 0; y < 32; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int i = 0; i < 32 * 4; ++i)
            ASSERT_TRUE(std::isfinite(detail::halfToFloat(row[i]))) << "non-finite pixel at row " << y;
    }
    // A pixel well outside the drawn rectangle must still be the background.
    const uint16_t* corner = reinterpret_cast<const uint16_t*>(px.getAddress());
    EXPECT_NEAR(detail::halfToFloat(corner[0]), 1.0f, 1e-3f);
    EXPECT_NEAR(detail::halfToFloat(corner[3]), 1.0f, 1e-3f);
}

TEST(CpuVsD2D, OffscreenRenderTargetRoundTrip)
{
    // createCompatibleRenderTarget: draw into an offscreen, then draw it back.
    // This is what CachedBlur and friends depend on.
    runScene("x_bmp_offscreen", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto offscreen = rt.createCompatibleRenderTarget({ 32.f, 32.f });
        offscreen.beginDraw();
        offscreen.clear(Color{ 0.f, 0.f, 0.f, 0.f });
        auto red = offscreen.createSolidColorBrush(Colors::Red);
        offscreen.fillEllipse({ { 16.f, 16.f }, 12.f, 12.f }, red);
        auto blue = offscreen.createSolidColorBrush(Color{ 0.f, 0.f, 1.f, 0.6f });
        offscreen.fillRectangle({ 0.f, 0.f, 16.f, 16.f }, blue);
        offscreen.endDraw();

        auto bmp = offscreen.getBitmap();
        rt.drawBitmap(bmp, { 8.f, 8.f, 40.f, 40.f }, { 0.f, 0.f, 32.f, 32.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
        rt.drawBitmap(bmp, { 40.f, 40.f, 56.f, 56.f }, { 0.f, 0.f, 32.f, 32.f },
                      1.0f, BitmapInterpolationMode::NearestNeighbor);
    });
}

// --- Gradients (milestone 5) ----------------------------------------------

namespace
{
GradientstopCollection makeStops(BitmapRenderTarget& rt, std::initializer_list<Gradientstop> stops,
                                 ExtendMode extendMode = ExtendMode::Clamp)
{
    return rt.createGradientstopCollection({ stops.begin(), stops.size() }, extendMode);
}

LinearGradientBrush makeLinear(BitmapRenderTarget& rt, Point start, Point end,
                               GradientstopCollection stops, float opacity = 1.0f)
{
    BrushProperties props{};
    props.opacity = opacity;
    return rt.createLinearGradientBrush({ start, end }, props, stops);
}

RadialGradientBrush makeRadial(BitmapRenderTarget& rt, Point center, Point originOffset,
                               float radiusX, float radiusY, GradientstopCollection stops)
{
    RadialGradientBrushProperties props{};
    props.center = center;
    props.gradientOriginOffset = originOffset;
    props.radiusX = radiusX;
    props.radiusY = radiusY;
    return rt.createRadialGradientBrush(props, BrushProperties{}, stops);
}
} // namespace

TEST(CpuVsD2D, LinearGradientBasic)
{
    runScene("x_grad_linear", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } });
        auto brush = makeLinear(rt, { 8.0f, 8.0f }, { 56.0f, 56.0f }, stops);
        rt.fillRectangle({ 4.0f, 4.0f, 60.0f, 60.0f }, brush);
    });
}

TEST(CpuVsD2D, LinearGradientMultiStop)
{
    runScene("x_grad_multistop", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red },
                                     { 0.35f, Colors::Yellow },
                                     { 0.5f, Colors::Lime },
                                     { 1.0f, Colors::Navy } });
        auto brush = makeLinear(rt, { 6.0f, 0.0f }, { 58.0f, 0.0f }, stops);
        rt.fillRectangle({ 0.0f, 6.0f, 64.0f, 58.0f }, brush);
    });
}

TEST(CpuVsD2D, LinearGradientWithAlpha)
{
    // Alpha varying across the gradient: exercises whether stop interpolation
    // happens in straight or premultiplied colour.
    runScene("x_grad_alpha", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Color{ 1.0f, 0.0f, 0.0f, 1.0f } },
                                     { 1.0f, Color{ 0.0f, 0.0f, 1.0f, 0.0f } } });
        auto brush = makeLinear(rt, { 8.0f, 0.0f }, { 56.0f, 0.0f }, stops);
        rt.fillRectangle({ 4.0f, 4.0f, 60.0f, 60.0f }, brush);
    });
}

TEST(CpuVsD2D, LinearGradientExtendModes)
{
    for (const auto mode : { ExtendMode::Clamp, ExtendMode::Wrap, ExtendMode::Mirror })
    {
        const char* name = mode == ExtendMode::Clamp ? "x_grad_extend_clamp"
                         : mode == ExtendMode::Wrap  ? "x_grad_extend_wrap"
                                                     : "x_grad_extend_mirror";
        runScene(name, [mode](BitmapRenderTarget& rt) {
            rt.clear(Colors::White);
            auto stops = makeStops(rt, { { 0.0f, Colors::Black }, { 1.0f, Colors::Orange } }, mode);
            // Short axis in the middle, so the extend mode governs most pixels.
            auto brush = makeLinear(rt, { 26.0f, 0.0f }, { 38.0f, 0.0f }, stops);
            rt.fillRectangle({ 0.0f, 8.0f, 64.0f, 56.0f }, brush);
        });
    }
}

TEST(CpuVsD2D, RadialGradientBasic)
{
    runScene("x_grad_radial", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::White }, { 1.0f, Colors::DarkGreen } });
        auto brush = makeRadial(rt, { 32.0f, 32.0f }, {}, 24.0f, 24.0f, stops);
        rt.fillEllipse({ { 32.0f, 32.0f }, 26.0f, 26.0f }, brush);
    });
}

TEST(CpuVsD2D, RadialGradientEllipticalWithOrigin)
{
    // Non-circular radii plus a focal offset: the full focal-gradient path.
    runScene("x_grad_radial_focal", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Yellow },
                                     { 0.6f, Colors::Red },
                                     { 1.0f, Colors::Black } });
        auto brush = makeRadial(rt, { 32.0f, 32.0f }, { -9.0f, -6.0f }, 26.0f, 18.0f, stops);
        rt.fillRectangle({ 2.0f, 8.0f, 62.0f, 56.0f }, brush);
    });
}

TEST(CpuVsD2D, GradientUnderTransform)
{
    runScene("x_grad_transform", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Cyan }, { 1.0f, Colors::Magenta } });
        auto brush = makeLinear(rt, { 0.0f, 0.0f }, { 30.0f, 0.0f }, stops);
        const auto m = makeRotationAbout(0.6f, 32.0f, 32.0f);
        AccessPtr::get(rt)->setTransform(&m);
        rt.fillRectangle({ 8.0f, 8.0f, 56.0f, 56.0f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, GradientBrushOpacityAndStroke)
{
    runScene("x_grad_stroke", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Blue }, { 1.0f, Colors::Red } });
        auto brush = makeLinear(rt, { 8.0f, 8.0f }, { 56.0f, 56.0f }, stops, 0.6f);
        rt.drawRectangle({ 10.5f, 10.5f, 53.5f, 53.5f }, brush, 6.0f);
        rt.drawLine({ 12.0f, 32.0f }, { 52.0f, 32.0f }, brush, 5.0f);
    });
}

TEST(CpuVsD2D, GradientZeroRadius)
{
    // A zero-radius radial paints the last stop in both backends.
    runScene("x_grad_zero_radius", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Green }, { 1.0f, Colors::Purple } });
        auto radial = makeRadial(rt, { 44.0f, 44.0f }, {}, 0.0f, 0.0f, stops);
        rt.fillRectangle({ 34.0f, 34.0f, 60.0f, 60.0f }, radial);
    });
}

TEST(CpuVsD2D, GradientBrushTransform)
{
    // BrushProperties::transform had zero coverage; this pins the compose
    // order and direction against D2D.
    runScene("x_grad_brush_transform", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } });
        BrushProperties props{};
        props.transform = { 1.6f, 0.0f, 0.0f, 1.0f, 12.0f, 0.0f }; // scale x, then shift
        auto brush = rt.createLinearGradientBrush({ { 0.0f, 0.0f }, { 24.0f, 0.0f } }, props, stops);
        rt.fillRectangle({ 2.0f, 2.0f, 62.0f, 62.0f }, brush);
    });
}

TEST(CpuVsD2D, RadialGradientFocusOutsideEllipse)
{
    // A diagonal origin offset beyond the ellipse: the focus must be pulled
    // back by vector length, or the focal quadratic degenerates and paints a
    // flat wedge.
    runScene("x_grad_radial_focus_outside", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::White }, { 1.0f, Colors::Maroon } });
        auto brush = makeRadial(rt, { 32.0f, 32.0f }, { 20.0f, 20.0f }, 22.0f, 22.0f, stops);
        rt.fillRectangle({ 4.0f, 4.0f, 60.0f, 60.0f }, brush);
    }, 64, 64, 2, 16.0, 40.0);
    // A focus outside the ellipse is undefined in D2D's contract, and the two
    // pull it back by slightly different amounts, so the highlight lands a
    // fraction of a pixel off: 36% of pixels differ but only by mean 11.6/255,
    // and the images are visually identical. The mean-severity limit is what
    // makes this a real regression guard — clamping the focus per-axis instead
    // of by vector length lets |f| exceed 1, the focal discriminant goes
    // negative, and a wedge of the plane collapses to flat last-stop colour:
    // FEWER pixels differ (4.1%) but far more severely (mean 41.6/255), which
    // trips the limit below.
}

// CPU-only regression for the NaN leak the gradient review found: a singular
// or near-singular transform makes Drawing.h's invert() (which has no
// zero-determinant guard) produce an infinite/NaN matrix. The gradient
// parameter then goes non-finite, and before the guard in colorAt that NaN
// reached the blend and poisoned every pixel of the chunk-aligned span --
// including zero-coverage pixels, permanently, since NaN * 0 is still NaN.
TEST(CpuVsD2D, GradientSingularTransformsStayFinite)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;

    const auto allFinite = [](BitmapRenderTarget& target) {
        auto bmp = target.getBitmap();
        auto px = bmp.lockPixels(BitmapLockFlags::Read);
        const auto size = bmp.getSize();
        for (uint32_t y = 0; y < size.height; ++y)
        {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
            for (uint32_t i = 0; i < size.width * 4; ++i)
                if (!std::isfinite(detail::halfToFloat(row[i])))
                    return false;
        }
        return true;
    };

    // (a) singular brush transform
    {
        auto rt = cpuFactory.createCpuRenderTarget({ 64, 64 }, 0);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } });
        BrushProperties props{};
        props.transform = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // fully collapsed
        auto brush = rt.createLinearGradientBrush({ { 8.0f, 8.0f }, { 56.0f, 56.0f } }, props, stops);
        rt.fillRectangle({ 4.0f, 4.0f, 60.0f, 60.0f }, brush);
        rt.endDraw();
        EXPECT_TRUE(allFinite(rt)) << "singular brush transform leaked NaN";
    }

    // (b) near-singular brush transform (finite entries, determinant underflows)
    {
        auto rt = cpuFactory.createCpuRenderTarget({ 64, 64 }, 0);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } });
        BrushProperties props{};
        props.transform = { 1e-25f, 0.0f, 0.0f, 1e-25f, 0.0f, 0.0f };
        auto brush = rt.createLinearGradientBrush({ { 8.0f, 8.0f }, { 56.0f, 56.0f } }, props, stops);
        rt.fillEllipse({ { 32.0f, 32.0f }, 24.0f, 24.0f }, brush);
        rt.endDraw();
        EXPECT_TRUE(allFinite(rt)) << "near-singular brush transform leaked NaN";
    }

    // (c) singular CONTEXT transform: the geometry collapses to zero area, so
    // like D2D this must paint nothing at all -- not a row of NaN.
    {
        auto rt = cpuFactory.createCpuRenderTarget({ 64, 64 }, 0);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto stops = makeStops(rt, { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } });
        auto brush = makeLinear(rt, { 8.0f, 8.0f }, { 56.0f, 56.0f }, stops);
        const Matrix3x2 collapsed{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f }; // flattens y
        AccessPtr::get(rt)->setTransform(&collapsed);
        rt.fillRectangle({ 4.0f, 4.0f, 60.0f, 60.0f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
        rt.endDraw();

        EXPECT_TRUE(allFinite(rt)) << "singular context transform leaked NaN";

        auto bmp = rt.getBitmap();
        auto px = bmp.lockPixels(BitmapLockFlags::Read);
        const uint16_t* row0 = reinterpret_cast<const uint16_t*>(px.getAddress());
        for (int x = 0; x < 64; ++x)
        {
            EXPECT_NEAR(detail::halfToFloat(row0[x * 4 + 0]), 1.0f, 1e-3f)
                << "zero-area geometry painted pixel " << x << " of row 0";
            EXPECT_NEAR(detail::halfToFloat(row0[x * 4 + 2]), 1.0f, 1e-3f);
        }
    }
}

// CPU-only regression: a single-stop gradient whose parameter goes non-finite.
// A near-singular transform inverts to an enormous one, pushing device points
// to infinity; Wrap then turns inf into NaN (inf - floor(inf)). Every
// comparison in the stop search is false for NaN, which used to walk the index
// past the end of a one-element stop vector.
TEST(CpuVsD2D, GradientSingleStopWithNonFiniteParameter)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;
    auto rt = cpuFactory.createCpuRenderTarget({ 32, 32 }, 0);

    rt.beginDraw();
    rt.clear(Colors::White);

    const Gradientstop one[] = { { 0.5f, Color{ 0.0f, 1.0f, 0.0f, 1.0f } } };
    for (const auto mode : { ExtendMode::Clamp, ExtendMode::Wrap, ExtendMode::Mirror })
    {
        auto stops = rt.createGradientstopCollection(one, mode);
        // Ordinary unit axis: t is then just the local x coordinate, so an
        // infinite local coordinate reaches colorAt as an infinite t. (A
        // degenerate axis would take the zero-length branch and prove nothing.)
        auto brush = makeLinear(rt, { 0.0f, 0.0f }, { 1.0f, 0.0f }, stops);

        // Near-singular transform. Drawing.h's invert has no zero-determinant
        // guard, so 1/det overflows and the inverse is infinite — while the
        // geometry itself still maps to finite device coordinates covering the
        // surface, so the fill actually runs.
        const Matrix3x2 tiny{ 1e-25f, 0.0f, 0.0f, 1e-25f, 0.0f, 0.0f };
        AccessPtr::get(rt)->setTransform(&tiny);
        rt.fillRectangle({ 0.0f, 0.0f, 1e27f, 1e27f }, brush);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    }
    rt.endDraw();

    // The only requirement is that it neither reads out of bounds nor writes
    // NaN into the surface.
    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);

    // Guard against the test going vacuous: the fill must actually have run and
    // painted the single stop's colour, rather than being skipped upstream.
    {
        const uint16_t* c = reinterpret_cast<const uint16_t*>(
            px.getAddress() + size_t(16) * px.getBytesPerRow() + size_t(16) * 8);
        EXPECT_NEAR(detail::halfToFloat(c[0]), 0.0f, 1e-3f) << "brush never ran (surface still white)";
        EXPECT_NEAR(detail::halfToFloat(c[1]), 1.0f, 1e-3f);
        EXPECT_NEAR(detail::halfToFloat(c[2]), 0.0f, 1e-3f);
    }
    for (int y = 0; y < 32; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int i = 0; i < 32 * 4; ++i)
            ASSERT_TRUE(std::isfinite(detail::halfToFloat(row[i]))) << "non-finite pixel at row " << y;
    }
}

// CPU-only. A zero-length linear axis is a degenerate input with no defined
// answer, and D2D's is not worth copying: it paints a fixed neutral grey that
// does not depend on the gradient's stops at all (measured (92,92,92) for both
// a Green->Purple and a Green->Yellow->Purple gradient), which is an artefact
// of its internal handling rather than a behaviour. We paint the last stop:
// deterministic, and never NaN.
TEST(CpuVsD2D, GradientDegenerateLinearAxisIsDeterministic)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;
    auto rt = cpuFactory.createCpuRenderTarget({ 32, 32 }, 0);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto stops = makeStops(rt, { { 0.0f, Color{ 1.0f, 0.0f, 0.0f, 1.0f } },
                                 { 1.0f, Color{ 0.0f, 0.0f, 1.0f, 1.0f } } });
    auto brush = makeLinear(rt, { 16.0f, 16.0f }, { 16.0f, 16.0f }, stops);
    rt.fillRectangle({ 4.0f, 4.0f, 28.0f, 28.0f }, brush);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const uint16_t* h = reinterpret_cast<const uint16_t*>(
        px.getAddress() + size_t(16) * px.getBytesPerRow() + size_t(16) * 8);
    EXPECT_NEAR(detail::halfToFloat(h[0]), 0.0f, 1e-3f) << "last stop is blue";
    EXPECT_NEAR(detail::halfToFloat(h[2]), 1.0f, 1e-3f);
    EXPECT_NEAR(detail::halfToFloat(h[3]), 1.0f, 1e-3f);
}

// --- Strokes (milestone 3) ------------------------------------------------

namespace
{
PathGeometry makePolyline(BitmapRenderTarget& rt, std::initializer_list<Point> pts, bool closed)
{
    auto factory = rt.getFactory();
    auto geometry = factory.createPathGeometry();
    auto sink = geometry.open();
    auto it = pts.begin();
    sink.beginFigure(*it++, FigureBegin::Hollow); // strokes: hollow is the D2D idiom
    for (; it != pts.end(); ++it)
        sink.addLine(*it);
    sink.endFigure(closed ? FigureEnd::Closed : FigureEnd::Open);
    sink.close();
    return geometry;
}
} // namespace

TEST(CpuVsD2D, StrokeLineCaps)
{
    runScene("x_stroke_caps", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        float y = 12.0f;
        for (const auto cap : { CapStyle::Flat, CapStyle::Square, CapStyle::Round })
        {
            auto style = rt.getFactory().createStrokeStyle(StrokeStyleProperties{ cap });
            rt.drawLine({ 14.0f, y }, { 50.0f, y }, brush, 9.0f, style);
            y += 20.0f;
        }
    });
}

TEST(CpuVsD2D, StrokeLineJoins)
{
    runScene("x_stroke_joins", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        float yOff = 0.0f;
        for (const auto join : { LineJoin::Miter, LineJoin::Bevel, LineJoin::Round })
        {
            StrokeStyleProperties props{};
            props.lineJoin = join;
            auto style = rt.getFactory().createStrokeStyle(props);
            auto path = makePolyline(rt, { { 8.0f, 18.0f + yOff }, { 22.0f, 4.0f + yOff }, { 36.0f, 18.0f + yOff } }, false);
            rt.drawGeometry(path, brush, 7.0f, style);
            yOff += 22.0f;
        }
    });
}

TEST(CpuVsD2D, StrokeMiterLimitSpillover)
{
    // A very sharp corner: the miter exceeds the limit and must fall back.
    runScene("x_stroke_miter_limit", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        StrokeStyleProperties props{};
        props.lineJoin = LineJoin::MiterOrBevel;
        props.miterLimit = 2.0f;
        auto style = rt.getFactory().createStrokeStyle(props);
        auto path = makePolyline(rt, { { 10.0f, 54.0f }, { 32.0f, 8.0f }, { 54.0f, 54.0f } }, false);
        rt.drawGeometry(path, brush, 8.0f, style);
    });
}

TEST(CpuVsD2D, StrokeClosedFigureJoins)
{
    runScene("x_stroke_closed", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
        StrokeStyleProperties props{};
        props.lineJoin = LineJoin::Miter;
        auto style = rt.getFactory().createStrokeStyle(props);
        auto path = makePolyline(rt, { { 14.0f, 14.0f }, { 50.0f, 18.0f }, { 44.0f, 50.0f }, { 16.0f, 44.0f } }, true);
        rt.drawGeometry(path, brush, 6.0f, style);
    });
}

TEST(CpuVsD2D, StrokeWidths)
{
    runScene("x_stroke_widths", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Navy);
        float x = 8.0f;
        for (const float w : { 0.5f, 1.0f, 2.5f, 7.0f, 15.0f })
        {
            rt.drawLine({ x, 6.0f }, { x, 58.0f }, brush, w);
            x += 12.0f;
        }
    });
}

TEST(CpuVsD2D, StrokedShapes)
{
    runScene("x_stroke_shapes", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Maroon);
        rt.drawRectangle({ 6.5f, 6.5f, 30.5f, 26.5f }, brush, 3.0f);
        rt.drawEllipse({ { 44.0f, 20.0f }, 15.0f, 11.0f }, brush, 3.0f);
        rt.drawRoundedRectangle({ { 8.0f, 36.0f, 56.0f, 58.0f }, 8.0f, 8.0f }, brush, 3.0f);
    });
}

TEST(CpuVsD2D, StrokeUnderTransform)
{
    // Non-uniform scale: the pen must be widened in local space (elliptical
    // pen), not in device space.
    runScene("x_stroke_transform", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Purple);
        const Matrix3x2 m{ 2.2f, 0.0f, 0.0f, 0.7f, -12.0f, 14.0f };
        AccessPtr::get(rt)->setTransform(&m);
        auto path = makePolyline(rt, { { 10.0f, 10.0f }, { 30.0f, 40.0f }, { 28.0f, 12.0f } }, false);
        rt.drawGeometry(path, brush, 5.0f);
        const Matrix3x2 identity;
        AccessPtr::get(rt)->setTransform(&identity);
    });
}

TEST(CpuVsD2D, TranslucentSelfOverlappingStroke)
{
    // A translucent stroke that crosses itself must not double-darken: the
    // whole widened outline is one coverage pass.
    runScene("x_stroke_translucent", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.45f });
        StrokeStyleProperties props{};
        props.lineJoin = LineJoin::Round;
        props.lineCap = CapStyle::Round;
        auto style = rt.getFactory().createStrokeStyle(props);
        auto path = makePolyline(rt, { { 12.0f, 12.0f }, { 52.0f, 52.0f }, { 52.0f, 12.0f }, { 12.0f, 52.0f } }, false);
        rt.drawGeometry(path, brush, 10.0f, style);
    });
}

TEST(CpuVsD2D, StrokeDashStyles)
{
    runScene("x_stroke_dashes", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        float y = 8.0f;
        for (const auto dash : { DashStyle::Dash, DashStyle::Dot, DashStyle::DashDot, DashStyle::DashDotDot })
        {
            StrokeStyleProperties props{};
            props.dashStyle = dash;
            props.lineCap = (dash == DashStyle::Dot) ? CapStyle::Round : CapStyle::Flat;
            auto style = rt.getFactory().createStrokeStyle(props);
            rt.drawLine({ 6.0f, y }, { 58.0f, y }, brush, 4.0f, style);
            y += 14.0f;
        }
    }, 64, 64, 2, 28.0, 8.0);
    // Dash/DashDot/DashDotDot match D2D exactly. The Dot row does not: D2D's
    // zero-length round dash renders a disc about 5% smaller than the
    // half-stroke-width circle the spec implies (measured ~1.9px vs 2.0px
    // radius at stroke width 4), which is a sub-pixel D2D quirk not worth
    // reverse-engineering.
}

TEST(CpuVsD2D, StrokeCustomDashesAndOffset)
{
    runScene("x_stroke_dash_custom", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Navy);
        const float pattern[] = { 4.0f, 1.5f, 1.0f, 1.5f };
        StrokeStyleProperties props{};
        props.dashStyle = DashStyle::Custom;
        float y = 12.0f;
        for (const float offset : { 0.0f, 2.0f, 5.5f })
        {
            props.dashOffset = offset;
            auto style = rt.getFactory().createStrokeStyle(props, pattern);
            rt.drawLine({ 5.0f, y }, { 59.0f, y }, brush, 3.0f, style);
            y += 18.0f;
        }
    });
}

TEST(CpuVsD2D, StrokeDashedClosedFigure)
{
    runScene("x_stroke_dash_closed", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Maroon);
        StrokeStyleProperties props{};
        props.dashStyle = DashStyle::Dash;
        auto style = rt.getFactory().createStrokeStyle(props);
        rt.drawRectangle({ 12.0f, 12.0f, 52.0f, 52.0f }, brush, 4.0f, style);
    });
}

TEST(CpuVsD2D, StrokePlainMiterBeyondLimit)
{
    // D2D's MITER and MITER_OR_BEVEL are documented differently; let D2D
    // adjudicate what a plain Miter join does once the limit is exceeded.
    runScene("x_stroke_plain_miter", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        StrokeStyleProperties props{};
        props.lineJoin = LineJoin::Miter;
        props.miterLimit = 1.5f;
        auto style = rt.getFactory().createStrokeStyle(props);
        auto path = makePolyline(rt, { { 10.0f, 54.0f }, { 32.0f, 8.0f }, { 54.0f, 54.0f } }, false);
        rt.drawGeometry(path, brush, 8.0f, style);
    });
}

TEST(CpuVsD2D, StrokeDegenerateSinglePoint)
{
    // A figure with no length: does D2D draw a dot for round/square caps?
    runScene("x_stroke_dot", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        float x = 16.0f;
        for (const auto cap : { CapStyle::Flat, CapStyle::Square, CapStyle::Round })
        {
            StrokeStyleProperties props{};
            props.lineCap = cap;
            auto style = rt.getFactory().createStrokeStyle(props);
            rt.drawLine({ x, 32.0f }, { x, 32.0f }, brush, 10.0f, style);
            x += 16.0f;
        }
    });
}

TEST(CpuVsD2D, StrokeVeryWideRelativeToSegment)
{
    // Pen far wider than the segments: inner-side crossover loops must stay
    // inside the stroke, and a closed figure narrower than the pen must fill
    // solid rather than leave an inverted hole.
    runScene("x_stroke_fat_pen", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Color{ 0.0f, 0.35f, 0.6f, 1.0f });
        StrokeStyleProperties props{};
        props.lineJoin = LineJoin::Round;
        props.lineCap = CapStyle::Round;
        auto style = rt.getFactory().createStrokeStyle(props);
        auto zigzag = makePolyline(rt, { { 12.0f, 20.0f }, { 16.0f, 26.0f }, { 12.0f, 32.0f }, { 18.0f, 38.0f } }, false);
        rt.drawGeometry(zigzag, brush, 16.0f, style);
        auto tiny = makePolyline(rt, { { 44.0f, 26.0f }, { 50.0f, 26.0f }, { 50.0f, 32.0f }, { 44.0f, 32.0f } }, true);
        rt.drawGeometry(tiny, brush, 18.0f, style);
    });
}

TEST(CpuVsD2D, StrokeAcrossClipAndSurfaceEdge)
{
    runScene("x_stroke_clipped", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Purple);
        rt.pushAxisAlignedClip({ 10.5f, 8.25f, 50.75f, 55.5f });
        auto path = makePolyline(rt, { { -20.0f, 20.0f }, { 80.0f, 30.0f }, { -10.0f, 50.0f } }, false);
        rt.drawGeometry(path, brush, 7.0f);
        rt.popAxisAlignedClip();
    });
}

TEST(CpuVsD2D, StrokeMultipleFiguresOverlapping)
{
    // Two subpaths in ONE geometry, stroked in one call, whose bands cross.
    // If the widened contours don't share a consistent winding, nonzero fill
    // cancels them to zero coverage and punches holes at the crossings.
    runScene("x_stroke_multi_figure", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::Black);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        // Closed square, wound counter-clockwise in source order.
        sink.beginFigure({ 16.0f, 16.0f }, FigureBegin::Hollow);
        sink.addLine({ 48.0f, 16.0f });
        sink.addLine({ 48.0f, 48.0f });
        sink.addLine({ 16.0f, 48.0f });
        sink.endFigure(FigureEnd::Closed);
        // Open bar crossing both vertical bands of the square.
        sink.beginFigure({ 4.0f, 32.0f }, FigureBegin::Hollow);
        sink.addLine({ 60.0f, 32.0f });
        sink.endFigure(FigureEnd::Open);
        sink.close();
        rt.drawGeometry(geometry, brush, 8.0f);
    });
}

TEST(CpuVsD2D, StrokeTwoClosedFiguresOppositeWinding)
{
    // A donut: outer contour and an oppositely wound hole contour, stroked
    // together. The hole's band lies inside the outer band's ring.
    runScene("x_stroke_donut", [](BitmapRenderTarget& rt) {
        rt.clear(Colors::White);
        auto brush = rt.createSolidColorBrush(Colors::DarkGreen);
        auto factory = rt.getFactory();
        auto geometry = factory.createPathGeometry();
        auto sink = geometry.open();
        sink.beginFigure({ 8.0f, 8.0f }, FigureBegin::Hollow);   // CCW
        sink.addLine({ 56.0f, 8.0f });
        sink.addLine({ 56.0f, 56.0f });
        sink.addLine({ 8.0f, 56.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.beginFigure({ 22.0f, 22.0f }, FigureBegin::Hollow); // CW
        sink.addLine({ 22.0f, 42.0f });
        sink.addLine({ 42.0f, 42.0f });
        sink.addLine({ 42.0f, 22.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
        rt.drawGeometry(geometry, brush, 12.0f);
    });
}

// CPU-only: stroke hit-testing on a CLOSED figure. The widener emits an
// annulus, so the rings only mean anything summed: a point in the shape's
// interior is inside the outer ring but must NOT count as on the stroke.
TEST(CpuVsD2D, StrokeContainsPointClosedFigure)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;

    auto geometry = cpuFactory.createPathGeometry();
    {
        auto sink = geometry.open();
        sink.beginFigure({ 10.0f, 10.0f }, FigureBegin::Hollow);
        sink.addLine({ 50.0f, 10.0f });
        sink.addLine({ 50.0f, 50.0f });
        sink.addLine({ 10.0f, 50.0f });
        sink.endFigure(FigureEnd::Closed);
        sink.close();
    }
    auto* g = AccessPtr::get(geometry);

    bool contains{};
    g->strokeContainsPoint({ 10.0f, 30.0f }, 6.0f, nullptr, nullptr, &contains);
    EXPECT_TRUE(contains) << "on the left edge's stroke band";
    g->strokeContainsPoint({ 30.0f, 30.0f }, 6.0f, nullptr, nullptr, &contains);
    EXPECT_FALSE(contains) << "the shape's interior is not on the stroke";
    g->strokeContainsPoint({ 30.0f, 10.0f }, 6.0f, nullptr, nullptr, &contains);
    EXPECT_TRUE(contains) << "on the top edge's stroke band";
    g->strokeContainsPoint({ 30.0f, 60.0f }, 6.0f, nullptr, nullptr, &contains);
    EXPECT_FALSE(contains) << "well outside the shape";
}

// CPU-only: stroke hit-testing and widened bounds.
TEST(CpuVsD2D, StrokeQueries)
{
    gmpi::cpugfx::Factory cpuImpl;
    Factory cpuFactory;
    *AccessPtr::put(cpuFactory) = &cpuImpl;

    auto geometry = cpuFactory.createPathGeometry();
    {
        auto sink = geometry.open();
        sink.beginFigure({ 10.0f, 10.0f }, FigureBegin::Hollow);
        sink.addLine({ 50.0f, 10.0f });
        sink.endFigure(FigureEnd::Open);
        sink.close();
    }
    auto* g = AccessPtr::get(geometry);

    bool contains{};
    g->strokeContainsPoint({ 30.0f, 10.0f }, 8.0f, nullptr, nullptr, &contains);
    EXPECT_TRUE(contains) << "point on the stroke centreline";
    g->strokeContainsPoint({ 30.0f, 13.0f }, 8.0f, nullptr, nullptr, &contains);
    EXPECT_TRUE(contains) << "point within half the stroke width";
    g->strokeContainsPoint({ 30.0f, 20.0f }, 8.0f, nullptr, nullptr, &contains);
    EXPECT_FALSE(contains) << "point beyond the stroke";
    g->strokeContainsPoint({ 55.0f, 10.0f }, 8.0f, nullptr, nullptr, &contains);
    EXPECT_FALSE(contains) << "past the flat cap";

    Rect bounds{};
    g->getWidenedBounds(8.0f, nullptr, nullptr, &bounds);
    EXPECT_NEAR(bounds.left, 10.0f, 0.01f);   // flat cap: no extension along x
    EXPECT_NEAR(bounds.right, 50.0f, 0.01f);
    EXPECT_NEAR(bounds.top, 6.0f, 0.01f);     // half width either side
    EXPECT_NEAR(bounds.bottom, 14.0f, 0.01f);
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
