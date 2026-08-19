// The C-projection half of the drawing-ABI tripwire.
//
// drawing_abi_tests.cpp pins the C++ vtable (GmpiApiDrawing.h) to the frozen
// slot table. c_projection_calls.c pins the C mirror
// (projections/plain_c/gmpi_drawing.h) to the same table at compile time.
// This file closes the loop at runtime: a real backend (cpugfx) is driven
// entirely through the C structs - createCpuRenderTarget in, getBitmap and
// pixels out - so if the two headers ever disagree, the mismatch fails here
// as a test instead of shipping as a segfault in somebody's C module.
//
// The C mirror was resynced against the frozen vtable in Aug 2026, after
// drifting so far it had never compiled (missing getFactory at slot 3,
// missing drawRichTextU/drawTextLayout, an IFactory five methods short, two
// stale GUIDs). These tests are what keeps that from recurring.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "Drawing.h"
#include "backends/CpuGfx.h"

#include "gmpi_drawing.h" // the plain-C projection under test
#include "c_projection_calls.h"

namespace
{
namespace api = gmpi::drawing::api;

// The value-type structs cross the ABI boundary; C and C++ must agree on
// every size (the old projection padded StrokeStyleProperties and FontMetrics
// with phantom members).
static_assert(sizeof(GMPI_Color) == sizeof(gmpi::drawing::Color));
static_assert(sizeof(GMPI_Point) == sizeof(gmpi::drawing::Point));
static_assert(sizeof(GMPI_Rect) == sizeof(gmpi::drawing::Rect));
static_assert(sizeof(GMPI_SizeU) == sizeof(gmpi::drawing::SizeU));
static_assert(sizeof(GMPI_Matrix3x2) == sizeof(gmpi::drawing::Matrix3x2));
static_assert(sizeof(GMPI_Gradientstop) == sizeof(gmpi::drawing::Gradientstop));
static_assert(sizeof(GMPI_BrushProperties) == sizeof(gmpi::drawing::BrushProperties));
static_assert(sizeof(GMPI_ArcSegment) == sizeof(gmpi::drawing::ArcSegment));
static_assert(sizeof(GMPI_StrokeStyleProperties) == sizeof(gmpi::drawing::StrokeStyleProperties));
static_assert(sizeof(GMPI_FontMetrics) == sizeof(gmpi::drawing::FontMetrics));
static_assert(sizeof(GMPI_TextStyleRun) == sizeof(gmpi::drawing::TextStyleRun));

// Return codes and the flag/enum values the tests pass across the boundary.
static_assert(GMPI_RETURN_CODE_OK == static_cast<int32_t>(gmpi::ReturnCode::Ok));
static_assert(GMPI_RETURN_CODE_FAIL == static_cast<int32_t>(gmpi::ReturnCode::Fail));
static_assert(GMPI_RETURN_CODE_NO_SUPPORT == static_cast<int32_t>(gmpi::ReturnCode::NoSupport));
static_assert(GMPI_BITMAP_RENDER_TARGET_FLAGS_SRGB_PIXELS == static_cast<int32_t>(gmpi::drawing::BitmapRenderTargetFlags::SRGBPixels));
static_assert(GMPI_BITMAP_RENDER_TARGET_FLAGS_CPU_READABLE == static_cast<int32_t>(gmpi::drawing::BitmapRenderTargetFlags::CpuReadable));
static_assert(GMPI_BITMAP_LOCK_FLAGS_READ == static_cast<int32_t>(gmpi::drawing::BitmapLockFlags::Read));
static_assert(int32_t{ GMPI_PIXEL_FORMAT_BGRA_SRGB_8I } == api::IBitmapPixels::BGRA_sRGB_8i);
static_assert(int32_t{ GMPI_PIXEL_FORMAT_RGBA_SRGB_8I } == api::IBitmapPixels::RGBA_sRGB_8i);
static_assert(int32_t{ GMPI_PIXEL_FORMAT_RGBA_16F } == api::IBitmapPixels::RGBA_16f);
static_assert(int32_t{ GMPI_PIXEL_FORMAT_ALPHA_8I } == api::IBitmapPixels::Alpha_8i);
static_assert(GMPI_FONT_FLAGS_CAP_HEIGHT == static_cast<int32_t>(gmpi::drawing::FontFlags::CapHeight));
static_assert(int32_t{ GMPI_TEXT_STYLE_FLAGS_HAS_COLOR } == gmpi::drawing::TextStyleFlags::HasColor);
static_assert(int32_t{ GMPI_DRAW_TEXT_OPTIONS_NO_MAC_SMOOTH } == gmpi::drawing::DrawTextOptions::noMacSmooth);

static_assert(sizeof(GMPI_Guid) == sizeof(gmpi::api::Guid));

::testing::AssertionResult sameGuid(const GMPI_Guid& c, const gmpi::api::Guid& cpp)
{
    if (0 == std::memcmp(&c, &cpp, sizeof(GMPI_Guid)))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "the C projection's IID differs from the C++ header's guid - "
           "queryInterface through the C API would fail or fetch the wrong "
           "interface";
}

// A bare CPU factory: fills, geometry and readback need no fonts and no image
// decoder, so nothing else is wired in.
struct CProjectionContext
{
    gmpi::cpugfx::Factory factoryImpl;

    GMPI_IFactory* cFactory()
    {
        api::IFactory* raw = &factoryImpl;
        // The exact cast a C module lives by: the object's first word is the
        // method table pointer.
        return reinterpret_cast<GMPI_IFactory*>(raw);
    }
};

constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;
constexpr int32_t kSrgbFlags = GMPI_BITMAP_RENDER_TARGET_FLAGS_SRGB_PIXELS; // createCpuRenderTarget force-ORs CPU_READABLE
} // namespace

// Every interface id in the projection must be byte-identical to the C++
// header's. The pre-resync projection carried two stale ones (IDeviceContext,
// IFactory) plus an IFactory2 the C++ API no longer has.
TEST(CProjectionAbi, InterfaceIdsMatchTheCppHeader)
{
    EXPECT_TRUE(sameGuid(GMPI_IID_TEXT_FORMAT, api::ITextFormat::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_RICH_TEXT_FORMAT, api::IRichTextFormat::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_TEXT_LAYOUT, api::ITextLayout::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_RESOURCE, api::IResource::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_BITMAP_PIXELS, api::IBitmapPixels::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_BITMAP, api::IBitmap::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_GRADIENTSTOP_COLLECTION, api::IGradientstopCollection::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_BITMAP_BRUSH, api::IBitmapBrush::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_SOLID_COLOR_BRUSH, api::ISolidColorBrush::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_LINEAR_GRADIENT_BRUSH, api::ILinearGradientBrush::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_RADIAL_GRADIENT_BRUSH, api::IRadialGradientBrush::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_STROKE_STYLE, api::IStrokeStyle::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_GEOMETRY_SINK, api::IGeometrySink::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_PATH_GEOMETRY, api::IPathGeometry::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_DEVICE_CONTEXT, api::IDeviceContext::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_BITMAP_RENDER_TARGET, api::IBitmapRenderTarget::guid));
    EXPECT_TRUE(sameGuid(GMPI_IID_FACTORY, api::IFactory::guid));
}

// The canary, through C: create a render target from the tail of the factory
// table (slot 11), draw through the middle of the device-context table, then
// fetch the bitmap through getBitmap at slot 32 and prove the fill actually
// landed. A projection with a shifted slot table cannot pass this - the calls
// land in neighbouring virtuals and return garbage or crash.
TEST(CProjectionAbi, GetBitmapRoundTripsThroughARealBackend)
{
    CProjectionContext ctx;

    GMPI_IBitmapRenderTarget* renderTarget{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_createCpuRenderTarget(ctx.cFactory(), kWidth, kHeight,
                                                kSrgbFlags, 96.0f, &renderTarget));
    ASSERT_NE(renderTarget, nullptr);

    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_fillWholeTarget(renderTarget, kWidth, kHeight,
                                          1.0f, 0.0f, 0.0f, 1.0f));

    uint8_t rgba[4]{};
    int32_t pixelFormat{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_readBackPixel(renderTarget, kWidth / 2, kHeight / 2,
                                        rgba, &pixelFormat));

    EXPECT_NE(pixelFormat & (1 << 11), 0)
        << "asked for an SRGB_PIXELS target, got a non-sRGB face";
    // Pure red through the sRGB encode is exact: 1.0f -> 255, 0.0f -> 0.
    EXPECT_EQ(rgba[0], 255);
    EXPECT_EQ(rgba[1], 0);
    EXPECT_EQ(rgba[2], 0);
    EXPECT_EQ(rgba[3], 255);

    cProjection_release(renderTarget);
}

// Slot 3 - getFactory - is where the pre-resync projection first diverged
// (it was simply missing, shifting all 28 methods after it up by one). The
// factory that comes back through C must be alive and answer a factory-tail
// method with the same result the C++ interface gives.
TEST(CProjectionAbi, GetFactoryAtSlotThreeReturnsAWorkingFactory)
{
    CProjectionContext ctx;

    GMPI_IBitmapRenderTarget* renderTarget{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_createCpuRenderTarget(ctx.cFactory(), kWidth, kHeight,
                                                kSrgbFlags, 96.0f, &renderTarget));
    ASSERT_NE(renderTarget, nullptr);

    GMPI_IFactory* roundTripped{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK, cProjection_getFactory(renderTarget, &roundTripped));
    ASSERT_NE(roundTripped, nullptr);

    int32_t viaC{};
    EXPECT_EQ(GMPI_RETURN_CODE_OK, cProjection_getPlatformPixelFormat(roundTripped, &viaC));

    int32_t viaCpp{};
    EXPECT_EQ(gmpi::ReturnCode::Ok, ctx.factoryImpl.getPlatformPixelFormat(&viaCpp));
    EXPECT_EQ(viaC, viaCpp);

    cProjection_release(roundTripped);
    cProjection_release(renderTarget);
}

// pushClipGeometry sits at slot 30 - the first of the two beta-period
// appends, and the first slot the June 2026 hand-append to the stale
// projection put in the wrong place (29, because getFactory was missing).
// Clipping a whole-target fill to a triangle built through the C geometry
// sink proves the slot AND the by-value GMPI_Point passing convention.
TEST(CProjectionAbi, ClipGeometryBuiltAndPushedThroughCLandsWhereItShould)
{
    CProjectionContext ctx;

    GMPI_IBitmapRenderTarget* renderTarget{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_createCpuRenderTarget(ctx.cFactory(), kWidth, kHeight,
                                                kSrgbFlags, 96.0f, &renderTarget));
    ASSERT_NE(renderTarget, nullptr);

    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_fillWithTriangleClip(renderTarget, kWidth, kHeight,
                                               1.0f, 0.0f, 0.0f, 1.0f));

    // Triangle (16,16) (48,16) (48,48). (40,24) is deep inside; (8,32) is
    // well outside and must still be the transparent clear.
    uint8_t inside[4]{};
    uint8_t outside[4]{};
    int32_t pixelFormat{};
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_readBackPixel(renderTarget, 40, 24, inside, &pixelFormat));
    ASSERT_EQ(GMPI_RETURN_CODE_OK,
              cProjection_readBackPixel(renderTarget, 8, 32, outside, &pixelFormat));

    EXPECT_EQ(inside[0], 255);
    EXPECT_EQ(inside[1], 0);
    EXPECT_EQ(inside[2], 0);
    EXPECT_EQ(inside[3], 255);

    EXPECT_EQ(outside[3], 0) << "fill leaked outside the pushed clip geometry";

    cProjection_release(renderTarget);
}
