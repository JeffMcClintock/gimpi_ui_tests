// Tests specific to the GPU backend (backends/GpuGfx.h): the seams between
// GPU surfaces and the CPU-side helpers that lock, modify and re-upload them.
// Compiled only with -DGMPI_UI_TESTS_GPU_BACKEND=ON.

#include "DrawingTestContext.h"

#if GMPI_UI_TESTS_BACKEND_GPU

#include "DrawingTestFixture.h"
#include "helpers/BitmapMask.h"
#include "backends/GpuGfx.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

#if GMPI_UI_TESTS_BACKEND_GPU
// clear() under a clip that does not start at the origin fills exactly the
// clip. The GPU backend once filled a rect of the clip's SIZE at the origin
// instead (the editor's right-hand panel at x = 1010 stayed unpainted).
TEST_F(DrawingTest, GpuClearUnderOffsetClip)
{
    g.pushAxisAlignedClip(Rect{ 40.0f, 24.0f, 64.0f, 64.0f });
    g.clear(Color{ 0.0f, 0.0f, 1.0f, 1.0f });
    g.popAxisAlignedClip();
    g.endDraw();
    drawingActive = false;

    auto px = g.getBitmap().lockPixels(BitmapLockFlags::Read);
    const auto inside = pixelAt(px, 50, 40);
    const auto outsideLeft = pixelAt(px, 20, 40);
    const auto outsideTop = pixelAt(px, 50, 10);
    EXPECT_NEAR(inside[2], 1.0f, 0.01f) << "blue inside the clip";
    EXPECT_NEAR(inside[0], 0.0f, 0.01f);
    EXPECT_NEAR(outsideLeft[0], 1.0f, 0.01f) << "white (untouched) left of the clip";
    EXPECT_NEAR(outsideTop[0], 1.0f, 0.01f) << "white (untouched) above the clip";
}
#endif

#if GMPI_UI_TESTS_BACKEND_GPU

// The Freq Analyser's graph: a 1-pixel solid stroke along a 400-point
// polyline, drawn on a dark background. Seen in the editor: faint lines of
// the stroke colour fanning out from the polyline's first point across the
// empty area above the curve. Every pixel more than three pixels from the
// polyline must stay exactly background.
TEST_F(DrawingTest, GpuStrokedGraphLeavesTheBackgroundClean)
{
    g.endDraw();
    drawingActive = false;

    // The fixture's target is 64x64; a graph needs room for long slivers.
    auto rt = drawingContext.createCpuRenderTarget({ 256u, 128u }, kRenderFlags);
    rt.beginDraw();
    const Color background{ 0.05f, 0.05f, 0.08f, 1.0f };
    rt.clear(background);

    auto graphY = [](float x) {
        float y = 100.0f - 0.1f * x;
        y -= 80.0f * std::exp(-((x - 100.0f) * (x - 100.0f)) / 18.0f);
        y -= 60.0f * std::exp(-((x - 130.0f) * (x - 130.0f)) / 8.0f);
        y -= 50.0f * std::exp(-((x - 150.0f) * (x - 150.0f)) / 5.0f);
        y -= 40.0f * std::exp(-((x - 165.0f) * (x - 165.0f)) / 3.0f);
        return y;
    };

    std::vector<Point> pts;
    for (float x = 10.0f; x < 190.0f; x += 0.45f)
        pts.push_back({ x, graphY(x) });

    auto geometry = rt.getFactory().createPathGeometry();
    {
        auto sink = geometry.open();
        sink.beginFigure(pts.front(), FigureBegin::Hollow);
        for (size_t i = 1; i < pts.size(); ++i)
            sink.addLine(pts[i]);
        sink.endFigure(FigureEnd::Open);
        sink.close();
    }
    auto brush = rt.createSolidColorBrush(Color{ 0.4f, 0.7f, 0.8f, 1.0f });
    rt.drawGeometry(geometry, brush, 1.0f);
    rt.endDraw();

    auto distanceToPolyline = [&](float px, float py) {
        float best = 1e9f;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const float ax = pts[i - 1].x, ay = pts[i - 1].y, bx = pts[i].x, by = pts[i].y;
            const float dx = bx - ax, dy = by - ay;
            const float len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
            t = (std::max)(0.0f, (std::min)(1.0f, t));
            const float ex = ax + t * dx - px, ey = ay + t * dy - py;
            best = (std::min)(best, ex * ex + ey * ey);
        }
        return std::sqrt(best);
    };

    auto px = rt.getBitmap().lockPixels(BitmapLockFlags::Read);
    float worst = 0.0f;
    uint32_t worstX = 0, worstY = 0;
    for (uint32_t y = 0; y < 120; ++y)
    {
        for (uint32_t x = 0; x < 200; ++x)
        {
            if (distanceToPolyline(x + 0.5f, y + 0.5f) < 3.0f)
                continue;
            const auto c = pixelAt(px, x, y);
            // premultiplied linear: green is background 0.05 unless the stroke leaked
            const float leak = std::fabs(c[1] - background.g);
            if (leak > worst)
            {
                worst = leak;
                worstX = x;
                worstY = y;
            }
        }
    }
    if (worst >= 0.0005f)
    {
        // Diagnostic: the leak, amplified 50x, as an 8-bit PGM.
        if (FILE* f = std::fopen("/tmp/claude-1000/-home-jef-SE/4a109b28-eaee-4758-9548-aef5c3153c55/scratchpad/stroke_leak.pgm", "wb"))
        {
            std::fprintf(f, "P5\n256 128\n255\n");
            for (uint32_t y = 0; y < 128; ++y)
                for (uint32_t x = 0; x < 256; ++x)
                {
                    const auto c = pixelAt(px, x, y);
                    const float v = (std::min)(1.0f, std::fabs(c[1] - background.g) * 50.0f);
                    std::fputc(int(v * 255.0f), f);
                }
            std::fclose(f);
        }
    }
    EXPECT_LT(worst, 0.0005f) << "stroke colour leaked onto the background at (" << worstX << "," << worstY
                              << "): green " << worst << " above background";
}

#endif

#if GMPI_UI_TESTS_BACKEND_GPU

// The GPU paints gradients and bitmaps itself now; cpugfx (D2D-exact by its
// own suite) is the oracle. Both targets are the same fp16 format, so the
// comparison is on the premultiplied linear values.
namespace
{

gmpi::drawing::BitmapRenderTarget cpuOracleTarget(DrawingTestContext& ctx, uint32_t w, uint32_t h)
{
    auto* gpuFactory = dynamic_cast<gmpi::gpugfx::Factory*>(gmpi::drawing::AccessPtr::get(ctx.factory()));
    EXPECT_NE(gpuFactory, nullptr);
    gmpi::drawing::api::IBitmapRenderTarget* native{};
    gpuFactory->cpu.createCpuRenderTarget({ w, h }, 0, &native, 96.0f);
    gmpi::drawing::BitmapRenderTarget rt;
    *gmpi::drawing::AccessPtr::put(rt) = native; // takes the reference
    return rt;
}

// Two same-sized locked targets compared channel by channel. `worst` is
// the largest difference over pixels that are not outliers; `outliers` the
// count of pixels differing by more than kOutlier. A few outliers are
// expected where the two rasterisers legitimately disagree by a whole
// pixel: a wrapped gradient's seam lands on one side or the other of a
// pixel centre, and a shape corner's SDF coverage is not its area.
struct Mismatch { float worst{}; uint32_t x{}, y{}; uint32_t outliers{}; uint32_t pixels{}; };
constexpr float kOutlier = 0.02f;
Mismatch compareTargets(gmpi::drawing::BitmapRenderTarget& a, gmpi::drawing::BitmapRenderTarget& b, uint32_t w, uint32_t h)
{
    auto pa = a.getBitmap().lockPixels(BitmapLockFlags::Read);
    auto pb = b.getBitmap().lockPixels(BitmapLockFlags::Read);
    Mismatch m;
    m.pixels = w * h;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const auto ca = pixelAt(pa, x, y);
            const auto cb = pixelAt(pb, x, y);
            float d = 0.0f;
            for (int c = 0; c < 4; ++c)
                d = (std::max)(d, std::fabs(ca[c] - cb[c]));
            if (d > kOutlier)
                ++m.outliers;
            else if (d > m.worst)
                m = { d, x, y, m.outliers, m.pixels };
        }
    return m;
}

// Diagnostic: a locked target as an 8-bit PPM (linear values, clamped).
void dumpPpm(const char* name, gmpi::drawing::BitmapRenderTarget& rt, uint32_t w, uint32_t h)
{
    std::string path = std::string("/tmp/claude-1000/-home-jef-SE/4a109b28-eaee-4758-9548-aef5c3153c55/scratchpad/oracle_") + name + ".ppm";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return;
    auto px = rt.getBitmap().lockPixels(BitmapLockFlags::Read);
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const auto c = pixelAt(px, x, y);
            for (int i = 0; i < 3; ++i)
                std::fputc(int((std::min)(1.0f, (std::max)(0.0f, c[i])) * 255.0f + 0.5f), f);
        }
    std::fclose(f);
}

template<typename Draw>
Mismatch gpuVersusCpu(DrawingTestContext& ctx, uint32_t w, uint32_t h, Draw draw, const char* dumpName = nullptr)
{
    auto gpu = ctx.createCpuRenderTarget({ w, h }, 0);
    auto cpu = cpuOracleTarget(ctx, w, h);
    for (auto* rt : { &gpu, &cpu })
    {
        rt->beginDraw();
        rt->clear(Color{ 0.1f, 0.1f, 0.1f, 1.0f });
        draw(*rt);
        rt->endDraw();
    }
    const auto m = compareTargets(gpu, cpu, w, h);
    if (dumpName && (m.worst >= 0.01f || m.outliers > 0))
    {
        dumpPpm((std::string(dumpName) + "_gpu").c_str(), gpu, w, h);
        dumpPpm((std::string(dumpName) + "_cpu").c_str(), cpu, w, h);
        // and the difference, x20
        std::string path = std::string("/tmp/claude-1000/-home-jef-SE/4a109b28-eaee-4758-9548-aef5c3153c55/scratchpad/oracle_") + dumpName + "_diff.ppm";
        if (FILE* f = std::fopen(path.c_str(), "wb"))
        {
            auto pa = gpu.getBitmap().lockPixels(BitmapLockFlags::Read);
            auto pb = cpu.getBitmap().lockPixels(BitmapLockFlags::Read);
            std::fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                {
                    const auto ca = pixelAt(pa, x, y);
                    const auto cb = pixelAt(pb, x, y);
                    for (int i = 0; i < 3; ++i)
                        std::fputc(int((std::min)(1.0f, std::fabs(ca[i] - cb[i]) * 20.0f) * 255.0f + 0.5f), f);
                }
            std::fclose(f);
        }
    }
    return m;
}

} // namespace

// A three-stop gradient whose axis spans a third of the rectangle, under
// each extend mode: the GPU strip is a 512-entry lookup table, so a few
// thousandths is the expected quantisation.
TEST_F(DrawingTest, GpuGradientExtendModesMatchCpu)
{
    g.endDraw();
    drawingActive = false;

    for (auto mode : { ExtendMode::Clamp, ExtendMode::Wrap, ExtendMode::Mirror })
    {
        const auto m = gpuVersusCpu(drawingContext, 96, 48, [&](gmpi::drawing::Graphics& rt) {
            const Gradientstop stops[] = { { 0.0f, Colors::Red }, { 0.4f, Colors::Lime }, { 1.0f, Color{ 0.0f, 0.0f, 1.0f, 0.5f } } };
            auto collection = rt.createGradientstopCollection(stops, mode);
            auto brush = rt.createLinearGradientBrush(collection, { 8.0f, 0.0f }, { 40.0f, 10.0f });
            rt.fillRectangle({ 0.0f, 0.0f, 96.0f, 48.0f }, brush);
        }, mode == ExtendMode::Wrap ? "wrap" : mode == ExtendMode::Mirror ? "mirror" : "clamp");
        EXPECT_LT(m.worst, 0.01f) << "extend mode " << int(mode) << " worst at (" << m.x << "," << m.y << ")";
        EXPECT_LE(m.outliers * 50, m.pixels) << "extend mode " << int(mode) << ": " << m.outliers << " outlier pixels (a seam is ~1%)";
    }
}

// A radial gradient with an off-centre origin, unequal radii, a brush
// transform and a non-uniform context transform: everything the ellipse
// maths has to get right.
TEST_F(DrawingTest, GpuRadialGradientMatchesCpu)
{
    g.endDraw();
    drawingActive = false;

    const auto m = gpuVersusCpu(drawingContext, 96, 64, [&](gmpi::drawing::Graphics& rt) {
        const Gradientstop stops[] = { { 0.0f, Colors::Yellow }, { 0.6f, Colors::DarkBlue }, { 1.0f, Colors::White } };
        auto collection = rt.createGradientstopCollection(stops, ExtendMode::Clamp);
        RadialGradientBrushProperties props{};
        props.center = { 30.0f, 20.0f };
        props.gradientOriginOffset = { 8.0f, -5.0f };
        props.radiusX = 24.0f;
        props.radiusY = 14.0f;
        BrushProperties bp{};
        bp.opacity = 0.8f;
        bp.transform = makeRotation(0.3f) * makeTranslation(4.0f, 2.0f);
        auto brush = rt.createRadialGradientBrush(props, bp, collection);
        const auto t = makeScale(1.4f, 0.9f) * makeTranslation(3.0f, 5.0f);
        rt.setTransform(t);
        // 60 x 60 lands on whole device pixels (87, 59): the fill's own edge
        // coverage (SDF vs area) is not what this test is about.
        rt.fillRectangle({ 0.0f, 0.0f, 60.0f, 60.0f }, brush);
        rt.setTransform(Matrix3x2{});
    }, "radial");
    EXPECT_LT(m.worst, 0.01f) << "worst at (" << m.x << "," << m.y << ")";
    EXPECT_LE(m.outliers, 4u) << "outlier pixels (the fill's corners may differ)";
}

// A cached atlas image must follow the bitmap: draw, modify through a write
// lock, draw again, and the second draw shows the new pixels.
TEST_F(DrawingTest, GpuBitmapRedrawFollowsWriteLock)
{
    g.endDraw();
    drawingActive = false;

    auto* gpuFactory = dynamic_cast<gmpi::gpugfx::Factory*>(gmpi::drawing::AccessPtr::get(drawingContext.factory()));
    ASSERT_NE(gpuFactory, nullptr);
    gmpi::drawing::api::IBitmapRenderTarget* native{};
    gpuFactory->cpu.createCpuRenderTarget({ 8, 8 }, 0, &native, 96.0f);
    gmpi::drawing::BitmapRenderTarget source;
    *gmpi::drawing::AccessPtr::put(source) = native;
    source.beginDraw();
    source.clear(Colors::Red);
    source.endDraw();
    auto bitmap = source.getBitmap();

    auto target = drawingContext.createCpuRenderTarget({ 32, 32 }, 0);
    target.beginDraw();
    target.clear(Colors::Black);
    target.drawBitmap(bitmap, Point{ 4.0f, 4.0f }, Rect{ 0.0f, 0.0f, 8.0f, 8.0f });
    target.endDraw();
    {
        auto px = target.getBitmap().lockPixels(BitmapLockFlags::Read);
        const auto c = pixelAt(px, 8, 8);
        EXPECT_NEAR(c[0], 1.0f, 0.01f);
        EXPECT_NEAR(c[2], 0.0f, 0.01f);
    }

    // Paint it blue through a write lock (fp16 premultiplied).
    {
        auto px = bitmap.lockPixels(BitmapLockFlags::ReadWrite);
        ASSERT_EQ(px.getBytesPerPixel(), 8);
        for (uint32_t y = 0; y < 8; ++y)
        {
            auto* row = reinterpret_cast<uint16_t*>(px.getAddress() + y * px.getBytesPerRow());
            for (uint32_t x = 0; x < 8; ++x)
            {
                row[x * 4 + 0] = detail::floatToHalf(0.0f);
                row[x * 4 + 1] = detail::floatToHalf(0.0f);
                row[x * 4 + 2] = detail::floatToHalf(1.0f);
                row[x * 4 + 3] = detail::floatToHalf(1.0f);
            }
        }
    }

    target.beginDraw();
    target.drawBitmap(bitmap, Point{ 4.0f, 4.0f }, Rect{ 0.0f, 0.0f, 8.0f, 8.0f });
    target.endDraw();
    {
        auto px = target.getBitmap().lockPixels(BitmapLockFlags::Read);
        const auto c = pixelAt(px, 8, 8);
        EXPECT_NEAR(c[0], 0.0f, 0.01f) << "stale red: the atlas copy did not follow the write lock";
        EXPECT_NEAR(c[2], 1.0f, 0.01f);
    }
}

// A linear stretch of the whole bitmap samples on the GPU; it must match
// cpugfx's bilinear sampling, edge texels included.
TEST_F(DrawingTest, GpuStretchedBitmapMatchesCpu)
{
    g.endDraw();
    drawingActive = false;

    auto* gpuFactory = dynamic_cast<gmpi::gpugfx::Factory*>(gmpi::drawing::AccessPtr::get(drawingContext.factory()));
    ASSERT_NE(gpuFactory, nullptr);
    gmpi::drawing::api::IBitmapRenderTarget* native{};
    gpuFactory->cpu.createCpuRenderTarget({ 6, 6 }, 0, &native, 96.0f);
    gmpi::drawing::BitmapRenderTarget source;
    *gmpi::drawing::AccessPtr::put(source) = native;
    source.beginDraw();
    source.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
    auto red = source.createSolidColorBrush(Colors::Red);
    auto green = source.createSolidColorBrush(Color{ 0.0f, 1.0f, 0.0f, 0.5f });
    source.fillRectangle({ 0.0f, 0.0f, 3.0f, 6.0f }, red);
    source.fillRectangle({ 3.0f, 0.0f, 6.0f, 6.0f }, green);
    source.endDraw();
    auto bitmap = source.getBitmap();

    const auto m = gpuVersusCpu(drawingContext, 64, 48, [&](gmpi::drawing::Graphics& rt) {
        rt.drawBitmap(bitmap, Rect{ 5.0f, 4.0f, 57.0f, 44.0f }, Rect{ 0.0f, 0.0f, 6.0f, 6.0f }, 0.75f,
                      BitmapInterpolationMode::Linear);
    }, "stretch");
    EXPECT_LT(m.worst, 0.01f) << "worst at (" << m.x << "," << m.y << ")";
    EXPECT_EQ(m.outliers, 0u);
}

#endif
