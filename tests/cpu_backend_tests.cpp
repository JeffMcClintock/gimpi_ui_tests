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
#include "helpers/DecodeImage.h" // platform image decoding, injected into the CPU factory
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

// Premultiplied linear RGBA, whichever of the two formats the bitmap locks as:
// render targets are fp16 RGBA, file-loaded images are 8-bit BGRA (matching what
// the DirectX backend gets from WIC).
std::array<float, 4> readPixel(Bitmap& bmp, int x, int y)
{
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const uint8_t* addr = px.getAddress();
    const int32_t bpr = px.getBytesPerRow();
    const int32_t bpp = px.getBytesPerPixel();
    const uint8_t* p = addr + size_t(y) * bpr + size_t(x) * bpp;

    if (bpp == 4) // BGRA_sRGB_8i — linear bytes despite the name
        return { p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, p[3] / 255.0f };

    const uint16_t* h = reinterpret_cast<const uint16_t*>(p);
    return { detail::halfToFloat(h[0]), detail::halfToFloat(h[1]),
             detail::halfToFloat(h[2]), detail::halfToFloat(h[3]) };
}

// One 8-bit step is 1/255, so anything read back through a byte format needs a
// looser bound than the fp16 default.
constexpr float k8BitTolerance = 6e-3f;

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
    // Qualified: macOS pulls the legacy QuickDraw ::Point in through
    // DecodeImage.h (ImageIO -> MacTypes.h), and the using-directive above
    // makes the bare name ambiguous there.
    gmpi::drawing::Point pts[5];
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

#if GMPI_UI_HAVE_IMAGE_DECODER
// Deliberately cross-platform: this is the check that alpha survives a PNG
// round trip, and each platform encodes differently. Windows hands the encoder
// straight alpha; macOS has no straight-alpha 8-bit RGBA bitmap context, so it
// hands CoreGraphics premultiplied pixels and relies on ImageIO to convert when
// writing (PNG itself has no premultiplied form). That reliance is exactly the
// assumption that proved FALSE on Windows — WIC was told the buffer was
// premultiplied and wrote those bytes into the file verbatim, saving every
// translucent pixel too dark. So this test must run everywhere, not just where
// the bug was first found.
TEST(CpuBackend, PngAlphaRoundTrip)
{
    CpuTestContext ctx; // initialises COM on Windows, which WIC needs
    ctx.factoryImpl.imageDecoder = gmpi::drawing::decodeImageFile;

    const auto path = std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "x_alpha_roundtrip.png";

    {
        auto rt = ctx.createRenderTarget(4, 1);
        rt.beginDraw();
        rt.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
        const Color colors[4] = { Colors::Red, Colors::Lime, Colors::Blue,
                                  Color{ 1.0f, 1.0f, 1.0f, 0.5f } };
        for (int i = 0; i < 4; ++i)
        {
            auto brush = rt.createSolidColorBrush(colors[i]);
            rt.fillRectangle({ float(i), 0.f, float(i) + 1.f, 1.f }, brush);
        }
        rt.endDraw();
        auto bmp = rt.getBitmap();
        ASSERT_TRUE(savePng(path, bmp));
    }

    auto loaded = ctx.factory.loadImageU(path.string());
    ASSERT_NE(AccessPtr::get(loaded), nullptr) << "loadImageU failed";
    ASSERT_EQ(loaded.getSize().width, 4u);

    const auto at = [&](int x, int c) { return readPixel(loaded, x, 0)[c]; };

    // Opaque primaries survive intact.
    expectPixel(loaded, 0, 0, kRed, k8BitTolerance);
    EXPECT_NEAR(at(1, 1), 1.0f, 0.01f);
    EXPECT_NEAR(at(2, 2), 1.0f, 0.01f);

    // The one that catches a premultiplied encode: half-alpha WHITE. Stored
    // premultiplied, so colour lands at alpha (0.5). If the encoder wrote
    // premultiplied bytes into the PNG, the file holds 50% grey instead of
    // white and this reads back at roughly 0.11 after the sRGB decode.
    EXPECT_NEAR(at(3, 3), 0.5f, 0.02f) << "alpha channel corrupted by the PNG round trip";
    EXPECT_NEAR(at(3, 0), 0.5f, 0.02f)
        << "colour darkened by the PNG round trip: the encoder is writing premultiplied "
           "bytes into PNG, which has no premultiplied form";
}

TEST(CpuBackend, LoadImageFailsCleanly)
{
    CpuTestContext ctx;
    ctx.factoryImpl.imageDecoder = gmpi::drawing::decodeImageFile;

    auto missing = ctx.factory.loadImageU(
        (std::filesystem::path(REFERENCE_IMAGES_DIR) / "no_such_file.png").string());
    EXPECT_EQ(AccessPtr::get(missing), nullptr);
}
#endif // GMPI_UI_HAVE_IMAGE_DECODER

TEST(CpuBackend, LoadImageWithoutDecoderReportsNoSupport)
{
    // The backend contains no platform code of its own, so with nothing wired
    // it must decline rather than crash.
    gmpi::cpugfx::Factory bare;
    gmpi::drawing::api::IBitmap* raw{};
    EXPECT_EQ(bare.loadImageU("anything.png", &raw), gmpi::ReturnCode::NoSupport);
    EXPECT_EQ(raw, nullptr);
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

// A stroked circle drawn far from the origin used to grow a horizontal spike
// tangential to its top — plainly visible on SynthEdit's module pins, which are
// small circles stacked down a module, so the first couple looked right and
// every one below them did not.
//
// Two compounding causes, both fixed:
//   1. Gfx_base.h's arc flattener derived its END point from the centre
//      parameterization (atan2 -> cos/sin -> rotate -> translate) instead of
//      using the end point the caller supplied. That lands about one ULP off,
//      and one ULP is not small once the coordinate is a few hundred.
//   2. CpuGfx.h's stroker then measured "is this point a duplicate of the start"
//      with an ABSOLUTE 1e-6 epsilon, which cannot see a one-ULP gap at that
//      magnitude. The surviving hair-length segment pointed in a direction made
//      entirely of rounding noise, the miter join read it as a ~180-degree turn,
//      and out came a spike as long as the miter limit permitted.
//
// The assertion is deliberately about the BOUNDS of what got painted rather than
// exact pixels: any spike, of any length or direction, puts ink outside the
// circle's own bounding box, and nothing else here can.
TEST(CpuBackend, StrokedCircleFarFromOriginHasNoMiterSpike)
{
    CpuTestContext ctx;
    constexpr uint32_t kW = 128, kH = 640;
    auto rt = ctx.createRenderTarget(kW, kH);

    // Radius and centres mirror SynthEdit's pin column (radius 3.1, pitch 12,
    // zoomed 8x) — the case that surfaced this.
    constexpr float kRadius = 3.1f * 8.0f;
    constexpr float kX = 64.0f;
    const float centresY[] = { 48.0f, 144.0f, 336.0f, 528.0f };

    rt.beginDraw();
    rt.clear(Colors::White);
    auto brush = rt.createSolidColorBrush(Colors::Black);
    for (const float cy : centresY)
        rt.drawCircle({ kX, cy }, kRadius, brush, 8.0f);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
    const uint8_t* base = pixels.getAddress();
    const int32_t bpr = pixels.getBytesPerRow();

    // Stroke straddles the radius, so ink may reach radius + halfWidth. Allow a
    // couple of pixels of antialiasing slop on top of that.
    const float reach = kRadius + 4.0f + 2.0f;

    for (uint32_t y = 0; y < kH; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(base + size_t(y) * bpr);
        for (uint32_t x = 0; x < kW; ++x)
        {
            // Background is opaque white; anything darker is ink.
            if (detail::halfToFloat(row[size_t(x) * 4]) > 0.9f)
                continue;

            bool insideSomeCircle = false;
            for (const float cy : centresY)
            {
                const float dx = float(x) - kX;
                const float dy = float(y) - cy;
                if (std::sqrt(dx * dx + dy * dy) <= reach)
                {
                    insideSomeCircle = true;
                    break;
                }
            }
            EXPECT_TRUE(insideSomeCircle)
                << "ink at (" << x << ", " << y << ") lies outside every circle — a stroke spike";
            if (!insideSomeCircle)
                return; // one report is enough; don't flood on a full-width bar
        }
    }
}
