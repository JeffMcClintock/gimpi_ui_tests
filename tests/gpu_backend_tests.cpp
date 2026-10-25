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
    g.fillRectangle(gmpi::drawing::Rect{ 8.0f, 8.0f, 24.0f, 24.0f }, brush);
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
    other.drawBitmap(bitmap, gmpi::drawing::Rect{ 0.0f, 0.0f, float(kWidth), float(kHeight) },
                     gmpi::drawing::Rect{ 0.0f, 0.0f, float(kWidth), float(kHeight) });
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
    g.fillRectangle(gmpi::drawing::Rect{ 0.0f, 0.0f, 32.0f, 32.0f }, half);

    // Mid-frame read-back (what CachedBlur/applyMask do).
    auto bitmap = g.getBitmap();
    float greyBefore;
    {
        auto px = bitmap.lockPixels(BitmapLockFlags::Read);
        greyBefore = pixelAt(px, 16, 16)[0];
    }

    auto blue = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 1.0f });
    g.fillRectangle(gmpi::drawing::Rect{ 40.0f, 40.0f, 60.0f, 60.0f }, blue);
    g.endDraw();
    drawingActive = false;

    // A second frame that draws nothing over the grey.
    g.beginDraw();
    g.fillRectangle(gmpi::drawing::Rect{ 40.0f, 0.0f, 60.0f, 20.0f }, blue);
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
    t1.fillRectangle(gmpi::drawing::Rect{ 0, 0, 64, 64 }, t1.createSolidColorBrush(Colors::White));
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
    t2.drawBitmap(src.getBitmap(), gmpi::drawing::Rect{ 0, 0, 64, 64 }, gmpi::drawing::Rect{ 0, 0, 8, 8 });
    t2.endDraw();
    const auto c = probe("fallback image white", t2.getBitmap());

    // (d) the fork directly: a fresh windowless canvas, white fill, screenshot
    {
        gmpi::gpu::Canvas canvas;
        canvas.setWindowless(16, 16);
        canvas.setClampBounds(0, 0, 16, 16);
        canvas.setBlendMode(gmpi::gpu::BlendMode::Opaque);
        canvas.setColor(gmpi::gpu::Color(1.0f, 1.0f, 1.0f, 1.0f));
        canvas.fill(0, 0, 16, 16);
        canvas.submit();
        gmpi::gpu::Color s = canvas.takeScreenshot().sample(8, 8);
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
    g.pushAxisAlignedClip(gmpi::drawing::Rect{ 40.0f, 24.0f, 64.0f, 64.0f });
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

    std::vector<gmpi::drawing::Point> pts;
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
    target.drawBitmap(bitmap, gmpi::drawing::Point{ 4.0f, 4.0f }, gmpi::drawing::Rect{ 0.0f, 0.0f, 8.0f, 8.0f });
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
    target.drawBitmap(bitmap, gmpi::drawing::Point{ 4.0f, 4.0f }, gmpi::drawing::Rect{ 0.0f, 0.0f, 8.0f, 8.0f });
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
        rt.drawBitmap(bitmap, gmpi::drawing::Rect{ 5.0f, 4.0f, 57.0f, 44.0f }, gmpi::drawing::Rect{ 0.0f, 0.0f, 6.0f, 6.0f }, 0.75f,
                      BitmapInterpolationMode::Linear);
    }, "stretch");
    EXPECT_LT(m.worst, 0.01f) << "worst at (" << m.x << "," << m.y << ")";
    EXPECT_EQ(m.outliers, 0u);
}

#endif

// --- Geometry clips as GPU coverage masks -----------------------------------
// A shaped clip is an entry in the path atlas that every shape under it
// samples (gmpi_ui backends/gpu/clip_mask.h). The CPU oracle applies the
// same clip as cpugfx coverage, which CpuVsD2D holds to Direct2D; both are
// exact area coverage, so they should agree to quantisation.

namespace
{
PathGeometry clipStar(gmpi::drawing::Graphics& rt, FillMode fillMode, float cx, float cy, float radius)
{
    auto geometry = rt.getFactory().createPathGeometry();
    auto sink = geometry.open();
    gmpi::drawing::Point pts[5];
    for (int k = 0; k < 5; ++k)
    {
        const float ang = -3.14159265f / 2.0f + float(k) * (4.0f * 3.14159265f / 5.0f);
        pts[k] = { cx + radius * std::cos(ang), cy + radius * std::sin(ang) };
    }
    sink.setFillMode(fillMode);
    sink.beginFigure(pts[0], FigureBegin::Filled);
    for (int k = 1; k < 5; ++k)
        sink.addLine(pts[k]);
    sink.endFigure(FigureEnd::Closed);
    sink.close();
    return geometry;
}

void expectClipMatch(const Mismatch& m, const char* what, uint32_t maxOutliers = 2)
{
    EXPECT_LT(m.worst, 0.01f) << what << ": worst at (" << m.x << "," << m.y << ")";
    EXPECT_LE(m.outliers, maxOutliers) << what << ": outlier pixels";
}
} // namespace

TEST_F(DrawingTest, GpuClipGeometryMatchesCpu)
{
    g.endDraw();
    drawingActive = false;

    for (auto rule : { FillMode::Winding, FillMode::Alternate })
    {
        const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
            auto star = clipStar(rt, rule, 32.0f, 32.0f, 24.0f);
            rt.pushClipGeometry(star);
            auto red = rt.createSolidColorBrush(Colors::Red);
            rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, red);
            // Translucent, overlapping the star's edge: the clip must scale
            // alpha, not just gate it. (An SDF circle here would add its own
            // edge difference from cpugfx, which is not what this tests.)
            auto blue = rt.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 0.5f });
            rt.fillRectangle({ 30.0f, 26.0f, 60.0f, 60.0f }, blue);
            rt.popAxisAlignedClip();
        }, rule == FillMode::Winding ? "clip_star_winding" : "clip_star_evenodd");
        expectClipMatch(m, rule == FillMode::Winding ? "winding" : "even-odd");
    }
}

TEST_F(DrawingTest, GpuClipGeometryIntersectsRectClip)
{
    g.endDraw();
    drawingActive = false;

    const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
        rt.pushAxisAlignedClip({ 0.0f, 0.0f, 64.0f, 36.0f });
        auto star = clipStar(rt, FillMode::Winding, 32.0f, 32.0f, 24.0f);
        rt.pushClipGeometry(star);
        auto red = rt.createSolidColorBrush(Colors::Red);
        rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, red);
        rt.popAxisAlignedClip();
        auto blue = rt.createSolidColorBrush(Colors::Blue);
        rt.fillRectangle({ 0.0f, 40.0f, 64.0f, 64.0f }, blue); // still clipped to the top: invisible
        rt.popAxisAlignedClip();
        rt.fillRectangle({ 56.0f, 56.0f, 64.0f, 64.0f }, blue); // unclipped
    }, "clip_star_nested_rect");
    expectClipMatch(m, "star inside a rect clip");
}

TEST_F(DrawingTest, GpuClearRespectsGeometryClip)
{
    g.endDraw();
    drawingActive = false;

    // A clear under a shaped clip replaces only what the clip admits, edge
    // interpolated - for an opaque colour and for transparent, which a
    // "fill over" could not do.
    for (auto colour : { Colors::Orange, Color{ 0.0f, 0.0f, 0.0f, 0.0f }, Color{ 0.2f, 0.9f, 0.3f, 0.5f } })
    {
        const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
            auto blue = rt.createSolidColorBrush(Colors::Blue);
            rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, blue); // something to preserve
            auto star = clipStar(rt, FillMode::Winding, 32.0f, 32.0f, 24.0f);
            rt.pushClipGeometry(star);
            rt.clear(colour);
            rt.popAxisAlignedClip();
        }, "clip_clear");
        expectClipMatch(m, "masked clear");
    }
}

TEST_F(DrawingTest, GpuClipGeometryUnderTransform)
{
    g.endDraw();
    drawingActive = false;

    const auto m = gpuVersusCpu(drawingContext, 96, 96, [&](gmpi::drawing::Graphics& rt) {
        rt.setTransform(makeRotation(0.4f, { 48.0f, 48.0f }) * makeTranslation(8.0f, 4.0f));
        auto star = clipStar(rt, FillMode::Winding, 40.0f, 44.0f, 30.0f);
        rt.pushClipGeometry(star);
        rt.setTransform(Matrix3x2{}); // draws under the clip need not share its transform
        const Gradientstop stops[] = { { 0.0f, Colors::Yellow }, { 1.0f, Colors::DarkBlue } };
        auto brush = rt.createLinearGradientBrush(rt.createGradientstopCollection(stops), { 0.0f, 0.0f }, { 96.0f, 96.0f });
        rt.fillRectangle({ 0.0f, 0.0f, 96.0f, 96.0f }, brush);
        rt.popAxisAlignedClip();
    }, "clip_star_transform");
    expectClipMatch(m, "rotated clip, gradient fill");
}

TEST_F(DrawingTest, GpuClipGeometryClipsTextAndBitmaps)
{
    g.endDraw();
    drawingActive = false;

    auto tf = makeTextFormat(28.0f);
    auto image = drawingContext.factory().createImage(32, 32);
    {
        auto px = image.lockPixels(BitmapLockFlags::ReadWrite);
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x)
            {
                auto* p = reinterpret_cast<uint16_t*>(px.getAddress() + y * px.getBytesPerRow() + x * px.getBytesPerPixel());
                const bool check = ((x / 4) + (y / 4)) % 2 == 0;
                if (px.getBytesPerPixel() == 8)
                {
                    p[0] = detail::floatToHalf(check ? 1.0f : 0.0f);
                    p[1] = detail::floatToHalf(check ? 0.5f : 0.0f);
                    p[2] = detail::floatToHalf(check ? 0.0f : 1.0f);
                    p[3] = detail::floatToHalf(1.0f);
                }
            }
    }
    const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
        auto star = clipStar(rt, FillMode::Winding, 32.0f, 32.0f, 28.0f);
        rt.pushClipGeometry(star);
        rt.drawBitmap(image, Point{ 0.0f, 32.0f }, Rect{ 0.0f, 0.0f, 32.0f, 32.0f }); // 1:1 atlas image
        auto white = rt.createSolidColorBrush(Colors::White);
        rt.drawTextU("Mg", tf, { 6.0f, 2.0f, 64.0f, 40.0f }, white);              // cached text
        rt.popAxisAlignedClip();
    }, "clip_text_bitmap");
    expectClipMatch(m, "text and bitmap under a clip", 8);
}

TEST_F(DrawingTest, GpuNestedGeometryClips)
{
    g.endDraw();
    drawingActive = false;

    // The outer clip is a GPU mask; the inner one cannot be (the atlas holds
    // one coverage per draw), so draws under both take the CPU path for the
    // inner clip, and the outer mask still applies to what they upload.
    const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
        auto star = clipStar(rt, FillMode::Winding, 32.0f, 32.0f, 26.0f);
        rt.pushClipGeometry(star);
        auto circle = clipStar(rt, FillMode::Winding, 24.0f, 40.0f, 20.0f); // a second shape, overlapping the first
        rt.pushClipGeometry(circle);
        auto red = rt.createSolidColorBrush(Colors::Red);
        rt.fillRectangle({ 0.0f, 0.0f, 64.0f, 64.0f }, red);
        rt.popAxisAlignedClip();
        auto green = rt.createSolidColorBrush(Color{ 0.0f, 1.0f, 0.0f, 0.6f });
        rt.fillRectangle({ 32.0f, 0.0f, 64.0f, 64.0f }, green); // outer clip only
        rt.popAxisAlignedClip();
    }, "clip_nested");
    expectClipMatch(m, "nested geometry clips", 4);
}

// --- Text as tinted coverage ----------------------------------------------------
// Text is cached as coverage per coverage class (opaque at a luminance, or
// translucent) and tinted on the GPU (GpuGfx.h, Bitmap::TextKey). The engine
// warps opaque coverage by luminance to match DirectWrite, so the tint must
// reproduce what rasterising in the real colour gives - the CPU oracle.

TEST_F(DrawingTest, GpuTintedTextMatchesCpu)
{
    g.endDraw();
    drawingActive = false;

    auto tf = makeTextFormat(13.0f); // small: the luminance warp is at full strength
    const Color colours[] = {
        Colors::Black, Colors::White, Color{ 0.9f, 0.1f, 0.1f, 1.0f }, Color{ 0.1f, 0.6f, 0.9f, 1.0f },
        Color{ 1.0f, 1.0f, 1.0f, 0.6f }, Color{ 0.0f, 0.0f, 0.0f, 0.4f }, Color{ 0.2f, 0.9f, 0.3f, 0.75f },
    };
    for (const auto& c : colours)
    {
        const auto m = gpuVersusCpu(drawingContext, 120, 40, [&](gmpi::drawing::Graphics& rt) {
            auto mid = rt.createSolidColorBrush(Color{ 0.45f, 0.45f, 0.5f, 1.0f });
            rt.fillRectangle({ 0.0f, 0.0f, 120.0f, 40.0f }, mid); // a background both light and dark text show on
            auto brush = rt.createSolidColorBrush(c);
            rt.drawTextU("Gain 3.5dB", tf, { 4.25f, 6.5f, 116.0f, 34.0f }, brush); // on a cached phase (quarter pixels)
        }, "tinted_text");
        expectClipMatch(m, "tinted text", 0);
    }
}

TEST_F(DrawingTest, GpuTextLayoutIsCachedAndMatchesCpu)
{
    g.endDraw();
    drawingActive = false;

    auto tf = makeTextFormat(14.0f);
    auto layout = drawingContext.factory().createTextLayout("Oscillator", tf, 100.0f, 24.0f);
    if (!AccessPtr::get(layout))
        GTEST_SKIP() << "backend declines createTextLayout";
    for (const auto& c : { Colors::White, Color{ 0.1f, 0.1f, 0.1f, 1.0f }, Color{ 1.0f, 0.8f, 0.2f, 0.5f } })
    {
        const auto m = gpuVersusCpu(drawingContext, 110, 32, [&](gmpi::drawing::Graphics& rt) {
            auto brush = rt.createSolidColorBrush(c);
            rt.drawTextLayout(layout, { 3.25f, 4.5f }, brush);
        }, "layout_text");
        expectClipMatch(m, "layout text", 0);
    }
}

TEST_F(DrawingTest, GpuTextCacheSharesOneRasterAcrossColours)
{
    // Translucent text has one coverage class whatever its colour, so three
    // colours of the same string are one cache entry; an opaque colour is
    // another (its own luminance class). Drawing them all again in a second
    // frame adds nothing.
    auto tf = makeTextFormat(13.0f);
    auto targetBitmap = g.getBitmap();
    auto* gpuBitmap = dynamic_cast<gmpi::gpugfx::Bitmap*>(AccessPtr::get(targetBitmap));
    ASSERT_NE(gpuBitmap, nullptr);
    const size_t before = gpuBitmap->textCache.size();

    const Color translucent[] = { Color{ 1.0f, 0.0f, 0.0f, 0.5f }, Color{ 0.0f, 1.0f, 0.0f, 0.7f }, Color{ 0.2f, 0.2f, 1.0f, 0.3f } };
    for (int frame = 0; frame < 2; ++frame)
    {
        if (frame == 1)
        {
            g.beginDraw();
            drawingActive = true;
        }
        float y = 2.0f;
        for (const auto& c : translucent)
        {
            auto brush = g.createSolidColorBrush(c);
            g.drawTextU("shared", tf, { 2.0f, y, 60.0f, y + 18.0f }, brush);
            y += 18.0f; // same phase, different place: same key
        }
        auto opaque = g.createSolidColorBrush(Colors::Black);
        g.drawTextU("shared", tf, { 2.0f, 2.0f, 60.0f, 20.0f }, opaque);
        g.endDraw();
        drawingActive = false;
        EXPECT_EQ(gpuBitmap->textCache.size(), before + 2) << "frame " << frame;
    }
}

// --- Bitmaps: cropped and nearest-neighbour stretches on the GPU -----------------
// The image shader clamps its sample coordinate to the source rect and, for
// nearest, snaps to the texel centre - cpugfx's sampling rule, so a cropped
// stretch cannot bleed past its crop and needs no CPU fallback.

namespace
{
Bitmap checkerImage(DrawingTestContext& ctx)
{
    auto image = ctx.factory().createImage(16, 16);
    auto px = image.lockPixels(BitmapLockFlags::ReadWrite);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x)
        {
            auto* p = reinterpret_cast<uint16_t*>(px.getAddress() + y * px.getBytesPerRow() + x * px.getBytesPerPixel());
            // Distinct neighbours everywhere, so any bleed past a crop shows.
            const float r = float(x) / 15.0f, g = float(y) / 15.0f, b = ((x + y) % 2) ? 1.0f : 0.0f;
            p[0] = detail::floatToHalf(r);
            p[1] = detail::floatToHalf(g);
            p[2] = detail::floatToHalf(b);
            p[3] = detail::floatToHalf(1.0f);
        }
    return image;
}
} // namespace

TEST_F(DrawingTest, GpuBitmapCroppedAndNearestStretchesMatchCpu)
{
    g.endDraw();
    drawingActive = false;

    auto image = checkerImage(drawingContext);
    struct Case { Rect dst, src; BitmapInterpolationMode mode; const char* name; };
    const Case cases[] = {
        { { 4.0f, 4.0f, 60.0f, 44.0f }, { 3.0f, 2.0f, 11.0f, 9.0f }, BitmapInterpolationMode::Linear, "cropped linear magnify" },
        { { 2.0f, 2.0f, 9.0f, 7.0f },   { 1.0f, 1.0f, 15.0f, 12.0f }, BitmapInterpolationMode::Linear, "cropped linear minify" },
        { { 4.0f, 4.0f, 59.0f, 59.0f }, { 0.0f, 0.0f, 16.0f, 16.0f }, BitmapInterpolationMode::NearestNeighbor, "whole nearest" },
        { { 6.0f, 5.0f, 58.0f, 47.0f }, { 5.0f, 3.0f, 12.0f, 10.0f }, BitmapInterpolationMode::NearestNeighbor, "cropped nearest" },
    };
    for (const auto& c : cases)
    {
        const auto m = gpuVersusCpu(drawingContext, 64, 64, [&](gmpi::drawing::Graphics& rt) {
            rt.drawBitmap(image, c.dst, c.src, 1.0f, c.mode);
        }, "bitmap_stretch");
        // Nearest: a pixel centre exactly on a texel boundary may take either
        // neighbour (fs_tinted_texture.sc). A 16 -> 55 stretch has one such
        // column and one row (at texel 8), so allow those two lines of
        // pixels and nothing else; filtered cases must match outright.
        const uint32_t tieLines = c.mode == BitmapInterpolationMode::NearestNeighbor ? 2u * 64u : 0u;
        expectClipMatch(m, c.name, tieLines);
    }
}

// Gradient strips are cached per stop collection AND opacity: one collection
// drawn at two opacities in one frame must give two strips.
TEST_F(DrawingTest, GpuGradientStripsCachedPerOpacity)
{
    g.endDraw();
    drawingActive = false;

    const auto m = gpuVersusCpu(drawingContext, 96, 48, [&](gmpi::drawing::Graphics& rt) {
        const Gradientstop stops[] = { { 0.0f, Colors::Red }, { 1.0f, Colors::Blue } };
        auto collection = rt.createGradientstopCollection(stops);
        for (int i = 0; i < 2; ++i)
        {
            BrushProperties bp{};
            bp.opacity = i == 0 ? 1.0f : 0.4f;
            LinearGradientBrushProperties lp{ { 0.0f, 0.0f }, { 96.0f, 0.0f } };
            auto brush = rt.createLinearGradientBrush(lp, bp, collection);
            rt.fillRectangle({ 0.0f, 24.0f * i, 96.0f, 24.0f * (i + 1) }, brush);
        }
    }, "gradient_opacity");
    EXPECT_LT(m.worst, 0.01f) << "worst at (" << m.x << "," << m.y << ")";
    EXPECT_EQ(m.outliers, 0u);
}

// A tall multi-line block is padded by a LINE's height for overhanging ink,
// not by the block's height: padding by the block's height made a browser
// pane's 600 px text block a ~1.9 Mpx raster, over the cache's limit, and it
// went to the CPU on every document open. It must cache, and lose no ink.
TEST_F(DrawingTest, GpuTallTextBlockIsCachedAndComplete)
{
    g.endDraw();
    drawingActive = false;

    std::string text;
    for (int i = 0; i < 40; ++i)
        text += "Line " + std::to_string(i) + " gypsy jQ\n";
    auto tf = makeTextFormat(14.0f);

    const auto m = gpuVersusCpu(drawingContext, 200, 760, [&](gmpi::drawing::Graphics& rt) {
        auto brush = rt.createSolidColorBrush(Colors::White);
        rt.drawTextU(text, tf, { 4.0f, 4.0f, 196.0f, 756.0f }, brush);
    }, "tall_text");
    EXPECT_LT(m.worst, 0.01f) << "worst at (" << m.x << "," << m.y << ")";
    EXPECT_EQ(m.outliers, 0u);

    // ...and it went through the cache: drawing it again on a fresh target
    // adds an entry rather than taking the CPU path.
    auto target = drawingContext.createCpuRenderTarget({ 200, 760 }, 0);
    auto targetBitmap = target.getBitmap();
    auto* gpuBitmap = dynamic_cast<gmpi::gpugfx::Bitmap*>(AccessPtr::get(targetBitmap));
    ASSERT_NE(gpuBitmap, nullptr);
    target.beginDraw();
    auto brush = target.createSolidColorBrush(Colors::White);
    target.drawTextU(text, tf, { 4.0f, 4.0f, 196.0f, 756.0f }, brush);
    target.endDraw();
    EXPECT_EQ(gpuBitmap->textCache.size(), 1u);
}
