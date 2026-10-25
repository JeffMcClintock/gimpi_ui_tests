// Shader brushes (gmpi_ui/GmpiApiShaders.h) on the GPU backend: the brush is
// created through IShaderFactory, probed from the factory, and used with the
// ordinary fills. The shader under test is tests/shaders/test_shade.sh:
//
//   colour = u_params[0] + p.x * u_params[1] + p.y * u_params[2] + u_time.x * u_params[3]
//
// Compiled only with -DGMPI_UI_TESTS_GPU_BACKEND=ON. Deterministic under
// lavapipe except where noted (time).

#include "DrawingTestContext.h"

#if GMPI_UI_TESTS_BACKEND_GPU

#include "DrawingTestFixture.h"
#include "GmpiApiShaders.h"
#include "helpers/BitmapMask.h"
#include "backends/GpuGfx.h"
#include "backends/gpu/graphics_caches.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <thread>

using namespace gmpi::drawing;

namespace gmpi::drawing::shaders
{
extern const api::ShaderBlob test_shade_blobs[];
extern const int32_t test_shade_blobCount;
}

namespace
{

// Premultiplied linear fp16 RGBA at (x, y) of a locked target.
std::array<float, 4> pixelAt(BitmapPixels& px, uint32_t x, uint32_t y)
{
    EXPECT_EQ(px.getBytesPerPixel(), 8);
    const auto* h = reinterpret_cast<const uint16_t*>(px.getAddress() + y * px.getBytesPerRow() + x * 8);
    return { detail::halfToFloat(h[0]), detail::halfToFloat(h[1]), detail::halfToFloat(h[2]), detail::halfToFloat(h[3]) };
}

void expectPixel(BitmapPixels& pixels, uint32_t x, uint32_t y, float r, float g, float b, float a, float tol = 0.02f)
{
    SCOPED_TRACE(::testing::Message() << "pixel (" << x << ", " << y << ")");
    const auto px = pixelAt(pixels, x, y);
    EXPECT_NEAR(px[0], r, tol);
    EXPECT_NEAR(px[1], g, tol);
    EXPECT_NEAR(px[2], b, tol);
    EXPECT_NEAR(px[3], a, tol);
}

struct Params
{
    float v[api::kShaderBrushMaxParams * 4]{};
    Params& set(int i, float x, float y, float z, float w)
    {
        v[i * 4 + 0] = x; v[i * 4 + 1] = y; v[i * 4 + 2] = z; v[i * 4 + 3] = w;
        return *this;
    }
};

const Color kFallback{ 1.0f, 0.0f, 0.0f, 1.0f }; // red, never what the shader paints in these tests

} // namespace

class GpuShader : public DrawingTest
{
protected:
    api::IShaderFactory* shaderFactory()
    {
        api::IShaderFactory* sf{};
        AccessPtr::get(drawingContext.factory())->queryInterface(&api::IShaderFactory::guid, reinterpret_cast<void**>(&sf));
        return sf; // leaked reference: the factory is NO_DELETE anyway
    }

    // A brush from the test shader, or null (with a failed expectation).
    api::IShaderBrush* makeBrush(const Params& p, int paramCount = 4, const BrushProperties* props = nullptr, const Color& fallback = kFallback)
    {
        auto* sf = shaderFactory();
        if (!sf)
        {
            ADD_FAILURE() << "no IShaderFactory on the GPU factory";
            return nullptr;
        }
        api::IShaderBrush* brush{};
        const auto r = sf->createShaderBrush(shaders::test_shade_blobs, shaders::test_shade_blobCount, p.v, paramCount, &fallback, props, &brush);
        EXPECT_EQ(r, gmpi::ReturnCode::Ok);
        return brush;
    }

    api::IDeviceContext* dc() { return AccessPtr::get(g); }

    BitmapPixels finish()
    {
        g.endDraw();
        drawingActive = false;
        bitmap = g.getBitmap();
        return bitmap.lockPixels(BitmapLockFlags::Read);
    }

    Bitmap bitmap;
};

TEST_F(GpuShader, FactoryServesIShaderFactory)
{
    EXPECT_NE(shaderFactory(), nullptr);
}

TEST_F(GpuShader, CpuFactoryDoesNot)
{
    // The CPU backend has no GPU: it answers NoSupport, which is the module's
    // cue to draw its own fallback.
    gmpi::cpugfx::Factory cpuFactory;
    api::IShaderFactory* sf{};
    EXPECT_EQ(cpuFactory.queryInterface(&api::IShaderFactory::guid, reinterpret_cast<void**>(&sf)), gmpi::ReturnCode::NoSupport);
    EXPECT_EQ(sf, nullptr);
}

TEST_F(GpuShader, ConstantShadeFillsItsRect)
{
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f)); // opaque blue
    ASSERT_NE(brush, nullptr);
    const Rect rect{ 8.0f, 8.0f, 24.0f, 24.0f };
    EXPECT_EQ(dc()->fillRectangle(&rect, brush), gmpi::ReturnCode::Ok);

    auto px = finish();
    expectPixel(px, 16, 16, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 9, 9, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 23, 23, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 40, 40, 1.0f, 1.0f, 1.0f, 1.0f); // the white clear
    expectPixel(px, 7, 16, 1.0f, 1.0f, 1.0f, 1.0f);
    brush->release();
}

TEST_F(GpuShader, ParamsReachTheShader)
{
    // Same shader, different u_params: a different colour. Two brushes in
    // one frame must also keep their own parameters (immutability is what
    // makes that safe with deferred draws).
    auto* green = makeBrush(Params().set(0, 0.0f, 1.0f, 0.0f, 1.0f));
    auto* half = makeBrush(Params().set(0, 0.0f, 0.0f, 0.0f, 0.5f)); // 50% black
    ASSERT_NE(green, nullptr);
    ASSERT_NE(half, nullptr);
    const Rect a{ 0.0f, 0.0f, 32.0f, 64.0f };
    const Rect b{ 32.0f, 0.0f, 64.0f, 64.0f };
    dc()->fillRectangle(&a, green);
    dc()->fillRectangle(&b, half);

    auto px = finish();
    expectPixel(px, 16, 32, 0.0f, 1.0f, 0.0f, 1.0f);
    expectPixel(px, 48, 32, 0.5f, 0.5f, 0.5f, 1.0f); // half black over white
    green->release();
    half->release();
}

TEST_F(GpuShader, BrushSpaceFollowsTheBrushTransform)
{
    // red = p.x, with brush space = local / 64: a ramp across the target.
    BrushProperties props;
    props.transform = makeScale(Size{ 64.0f, 64.0f }); // brush -> local
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 0.0f, 1.0f).set(1, 1.0f, 0.0f, 0.0f, 0.0f), 4, &props);
    ASSERT_NE(brush, nullptr);
    const Rect rect{ 0.0f, 0.0f, 64.0f, 64.0f };
    dc()->fillRectangle(&rect, brush);

    auto px = finish();
    EXPECT_NEAR(pixelAt(px, 16, 32)[0], 16.5f / 64.0f, 0.03f);
    EXPECT_NEAR(pixelAt(px, 48, 32)[0], 48.5f / 64.0f, 0.03f);
    EXPECT_NEAR(pixelAt(px, 48, 32)[1], 0.0f, 0.02f);
    brush->release();
}

TEST_F(GpuShader, BrushSpaceFollowsTheContextTransform)
{
    // The same ramp, drawn under a translated context: brush space is LOCAL
    // (through the context transform), so the ramp moves with the fill.
    BrushProperties props;
    props.transform = makeScale(Size{ 32.0f, 32.0f });
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 0.0f, 1.0f).set(1, 1.0f, 0.0f, 0.0f, 0.0f), 4, &props);
    ASSERT_NE(brush, nullptr);
    const auto shift = makeTranslation(Size{ 32.0f, 0.0f });
    dc()->setTransform(&shift);
    const Rect rect{ 0.0f, 0.0f, 32.0f, 64.0f }; // lands on device x 32..64
    dc()->fillRectangle(&rect, brush);

    auto px = finish();
    EXPECT_NEAR(pixelAt(px, 32 + 8, 32)[0], 8.5f / 32.0f, 0.03f);
    EXPECT_NEAR(pixelAt(px, 32 + 24, 32)[0], 24.5f / 32.0f, 0.03f);
    expectPixel(px, 16, 32, 1.0f, 1.0f, 1.0f, 1.0f); // untouched
    brush->release();
}

TEST_F(GpuShader, PathFillIsShaded)
{
    // A triangle through the path atlas: shaded inside, untouched outside,
    // and nothing of the fallback red anywhere.
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    ASSERT_NE(brush, nullptr);
    auto geometry = drawingContext.factory().createPathGeometry();
    {
        auto sink = geometry.open();
        sink.beginFigure(Point{ 4.0f, 60.0f }, FigureBegin::Filled);
        sink.addLine(Point{ 60.0f, 60.0f });
        sink.addLine(Point{ 32.0f, 4.0f });
        sink.endFigure();
        sink.close();
    }
    EXPECT_EQ(dc()->fillGeometry(AccessPtr::get(geometry), brush, nullptr), gmpi::ReturnCode::Ok);

    auto px = finish();
    expectPixel(px, 32, 50, 0.0f, 0.0f, 1.0f, 1.0f); // deep inside
    expectPixel(px, 32, 20, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 4, 8, 1.0f, 1.0f, 1.0f, 1.0f);   // outside: clear colour
    expectPixel(px, 60, 8, 1.0f, 1.0f, 1.0f, 1.0f);
    brush->release();
}

TEST_F(GpuShader, HonoursTheClip)
{
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    ASSERT_NE(brush, nullptr);
    const Rect clip{ 16.0f, 16.0f, 48.0f, 48.0f };
    dc()->pushAxisAlignedClip(&clip);
    const Rect rect{ 0.0f, 0.0f, 64.0f, 64.0f };
    dc()->fillRectangle(&rect, brush);
    dc()->popAxisAlignedClip();

    auto px = finish();
    expectPixel(px, 32, 32, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 17, 17, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 8, 32, 1.0f, 1.0f, 1.0f, 1.0f);
    expectPixel(px, 32, 56, 1.0f, 1.0f, 1.0f, 1.0f);
    brush->release();
}

TEST_F(GpuShader, RotatedTransformPaintsTheFallbackColour)
{
    // No GPU route for a rotated quad yet: the fallback colour, not nothing
    // and not the shader's blue.
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    ASSERT_NE(brush, nullptr);
    const auto rot = makeRotation(0.5f, Point{ 32.0f, 32.0f });
    dc()->setTransform(&rot);
    const Rect rect{ 8.0f, 8.0f, 56.0f, 56.0f };
    dc()->fillRectangle(&rect, brush);

    auto px = finish();
    expectPixel(px, 32, 32, 1.0f, 0.0f, 0.0f, 1.0f);
    brush->release();
}

TEST_F(GpuShader, ShadesUnderAGeometryClip)
{
    // Under a geometry clip the GPU used to refuse every draw and a shader
    // brush painted its fallback colour. With the clip as a GPU mask the
    // shader runs, and only inside the clip: shader blue inside, the clear
    // white outside, no fallback red anywhere.
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    ASSERT_NE(brush, nullptr);
    auto clip = drawingContext.factory().createPathGeometry();
    {
        auto sink = clip.open();
        sink.beginFigure(Point{ 8.0f, 8.0f }, FigureBegin::Filled);
        sink.addLine(Point{ 56.0f, 8.0f });
        sink.addLine(Point{ 8.0f, 56.0f });
        sink.endFigure();
        sink.close();
    }
    dc()->pushClipGeometry(AccessPtr::get(clip));
    const Rect rect{ 0.0f, 0.0f, 64.0f, 64.0f };
    EXPECT_EQ(dc()->fillRectangle(&rect, brush), gmpi::ReturnCode::Ok);
    dc()->popAxisAlignedClip();

    auto px = finish();
    expectPixel(px, 16, 16, 0.0f, 0.0f, 1.0f, 1.0f);  // inside the triangle
    expectPixel(px, 48, 48, 1.0f, 1.0f, 1.0f, 1.0f);  // outside: the clear
    for (uint32_t y = 0; y < kHeight; ++y)
        for (uint32_t x = 0; x < kWidth; ++x)
        {
            const auto p = pixelAt(px, x, y);
            EXPECT_FALSE(p[0] > 0.9f && p[1] < 0.5f && p[2] < 0.5f) << "fallback red at (" << x << "," << y << ")";
        }
    brush->release();
}

TEST_F(GpuShader, TextPaintsTheFallbackColour)
{
    // Text has no shader route (v1): the glyphs come out in the fallback
    // colour. Measured loosely: some pixel in the layout rect is reddish and
    // none is the shader's blue.
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    ASSERT_NE(brush, nullptr);
    auto tf = makeTextFormat(40.0f);
    const Rect rect{ 0.0f, 0.0f, 64.0f, 64.0f };
    const char text[] = "MM";
    dc()->drawTextU(text, 2, AccessPtr::get(tf), &rect, brush, 0);

    auto px = finish();
    bool anyRed = false, anyBlue = false;
    for (uint32_t y = 0; y < kHeight; ++y)
        for (uint32_t x = 0; x < kWidth; ++x)
        {
            const auto p = pixelAt(px, x, y);
            if (p[0] > 0.9f && p[1] < 0.5f && p[2] < 0.5f) anyRed = true;
            if (p[2] > 0.9f && p[0] < 0.5f) anyBlue = true;
        }
    EXPECT_TRUE(anyRed);
    EXPECT_FALSE(anyBlue);
    brush->release();
}

TEST_F(GpuShader, BrushesFromOneBlobTableShareOneProgram)
{
    // Two brushes, two draws, one compiled program per stage: the cache keys
    // on the blob bytes, which both brushes point at.
    auto* a = makeBrush(Params().set(0, 0.0f, 0.0f, 1.0f, 1.0f));
    auto* b = makeBrush(Params().set(0, 0.0f, 1.0f, 0.0f, 1.0f));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    const Rect ra{ 0.0f, 0.0f, 32.0f, 64.0f };
    dc()->fillRectangle(&ra, a);
    g.endDraw();
    drawingActive = false;
    const auto before = gmpi::gpu::ProgramCache::programList().size();

    g.beginDraw();
    drawingActive = true;
    const Rect rb{ 32.0f, 0.0f, 64.0f, 64.0f };
    dc()->fillRectangle(&rb, b);
    auto px = finish();
    EXPECT_EQ(gmpi::gpu::ProgramCache::programList().size(), before);
    expectPixel(px, 16, 32, 0.0f, 0.0f, 1.0f, 1.0f);
    expectPixel(px, 48, 32, 0.0f, 1.0f, 0.0f, 1.0f);
    a->release();
    b->release();
}

TEST_F(GpuShader, TimeAdvancesBetweenFrames)
{
    // red = u_time.x. Not pinned to a value (wall clock), only to monotone
    // growth across two frames some milliseconds apart.
    auto* brush = makeBrush(Params().set(0, 0.0f, 0.0f, 0.0f, 1.0f).set(3, 1.0f, 0.0f, 0.0f, 0.0f));
    ASSERT_NE(brush, nullptr);
    const Rect rect{ 0.0f, 0.0f, 64.0f, 64.0f };
    dc()->fillRectangle(&rect, brush);
    float first{};
    {
        g.endDraw();
        drawingActive = false;
        auto bm = g.getBitmap();
        auto px = bm.lockPixels(BitmapLockFlags::Read);
        first = pixelAt(px, 32, 32)[0];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    g.beginDraw();
    drawingActive = true;
    dc()->fillRectangle(&rect, brush);
    auto px = finish();
    const float second = pixelAt(px, 32, 32)[0];
    EXPECT_GT(second, first + 0.02f);
    brush->release();
}

#endif // GMPI_UI_TESTS_BACKEND_GPU
