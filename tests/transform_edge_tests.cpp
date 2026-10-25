// Transforms and extremes that only the Windows CpuVsD2D suite used to cover:
// a fill under a mirroring transform, a nearest-neighbour bitmap under a
// rotation, and a rect whose far edges are 1e7 away. The first Direct3D 12 run
// found the GPU backend wrong on all three (gmpi_ui 9bf65f8, deviceBounds()),
// while the Linux and Mac suites ran nothing that could see it.
//
// Pixel assertions, not reference images, so the same expectations hold on
// every backend: Direct2D, CoreGraphics, cpugfx and the GPU renderer.

#include "DrawingTestFixture.h"

#include <array>
#include <cmath>
#include <cstring>

namespace
{

// Linear premultiplied RGBA at (x, y), in R, G, B, A order whatever the
// surface's own format and channel order.
std::array<float, 4> linearPixel(BitmapPixels& px, int x, int y)
{
    const uint8_t* p = px.getAddress() + y * px.getBytesPerRow() + x * px.getBytesPerPixel();
    float c[4]{};
    if (px.getBytesPerPixel() == 16)
    {
        std::memcpy(c, p, sizeof(c));
    }
    else if (px.getBytesPerPixel() == 8)
    {
        const auto* h = reinterpret_cast<const uint16_t*>(p);
        for (int i = 0; i < 4; ++i)
            c[i] = px.isInteger() ? h[i] / 65535.0f : gmpi::drawing::detail::halfToFloat(h[i]);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            c[i] = p[i] / 255.0f;
    }

    struct Order { int r, g, b, a; };
    const Order o = [&]() -> Order {
        switch (px.channelLayout())
        {
            case 1:  return { 0, 1, 2, 3 }; // RGBA
            case 2:  return { 1, 2, 3, 0 }; // ARGB
            case 3:  return { 3, 2, 1, 0 }; // ABGR
            default: return { 2, 1, 0, 3 }; // BGRA
        }
    }();
    std::array<float, 4> rgba{ c[o.r], c[o.g], c[o.b], c[o.a] };

    if (px.getBytesPerPixel() == 4 && px.isSRGB())
    {
        // 8-bit sRGB storage: back to linear, then premultiply.
        for (int i = 0; i < 3; ++i)
        {
            const float v = rgba[i];
            rgba[i] = (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f)) * rgba[3];
        }
    }
    return rgba;
}

// Black ink on an opaque white surface: the fraction of the pixel covered.
float inkCoverage(BitmapPixels& px, int x, int y)
{
    return 1.0f - linearPixel(px, x, y)[1];
}

} // namespace

// A mirroring transform maps the rect's left edge to the right. The GPU
// backend once took the transformed rect as two corners, got right < left and
// drew nothing at all.
TEST_F(DrawingTest, FillUnderNegativeScaleIsMirrored)
{
    auto black = g.createSolidColorBrush(Colors::Black);
    g.setTransform(Matrix3x2{ -1.0f, 0.0f, 0.0f, 1.0f, 64.0f, 0.0f }); // x -> 64 - x
    g.fillRectangle({ 8.3f, 8.3f, 30.6f, 30.6f }, black);              // device x 33.4 .. 55.7
    g.setTransform(Matrix3x2{});
    g.endDraw();
    drawingActive = false;

    auto bitmap = g.getBitmap();
    auto px = bitmap.lockPixels(BitmapLockFlags::Read);

    EXPECT_GT(inkCoverage(px, 44, 20), 0.98f) << "inside the mirrored rect";
    EXPECT_LT(inkCoverage(px, 20, 20), 0.02f) << "where the rect would be without the mirror";
    EXPECT_NEAR(inkCoverage(px, 33, 20), 0.6f, 0.1f) << "left edge at x 33.4";
    EXPECT_NEAR(inkCoverage(px, 55, 20), 0.7f, 0.1f) << "right edge at x 55.7";
    EXPECT_LT(inkCoverage(px, 57, 20), 0.02f);
    EXPECT_LT(inkCoverage(px, 31, 20), 0.02f);
}

// A rotated bitmap reaches beyond the box of its destination's top-left and
// bottom-right corners. The GPU backend's CPU fallback once drew it into a
// patch of exactly that box, cutting off two corners. Nearest-neighbour, so
// each corner keeps its texel's exact colour: red on the left, blue on the right.
TEST_F(DrawingTest, RotatedNearestBitmapKeepsEveryCorner)
{
    constexpr uint32_t kW = 11, kH = 7;
    auto tileRT = drawingContext.factory().createCpuRenderTarget({ kW, kH }, kRenderFlags);
    tileRT.beginDraw();
    tileRT.clear(Colors::Red);
    auto blue = tileRT.createSolidColorBrush(Colors::Blue);
    tileRT.fillRectangle({ 6.0f, 0.0f, float(kW), float(kH) }, blue);
    tileRT.endDraw();
    auto tile = tileRT.getBitmap();

    const gmpi::drawing::Rect dest{ 10.3f, 12.7f, 53.6f, 45.2f };
    const float angle = 0.4f;
    const float cx = 32.0f, cy = 32.0f;
    const auto m = makeRotation(angle, gmpi::drawing::Point{ cx, cy });
    g.setTransform(m);
    g.drawBitmap(tile, dest, { 0.0f, 0.0f, float(kW), float(kH) }, 1.0f, BitmapInterpolationMode::NearestNeighbor);
    g.setTransform(Matrix3x2{});
    g.endDraw();
    drawingActive = false;

    auto bitmap = g.getBitmap();
    auto px = bitmap.lockPixels(BitmapLockFlags::Read);

    // A point 85% of the way from the destination's centre to each corner, in device pixels.
    const float mx = (dest.left + dest.right) * 0.5f, my = (dest.top + dest.bottom) * 0.5f;
    const auto nearCorner = [&](float x, float y) {
        const float lx = mx + 0.85f * (x - mx), ly = my + 0.85f * (y - my);
        const auto d = transformPoint(m, { lx, ly });
        return std::array<int, 2>{ int(std::floor(d.x)), int(std::floor(d.y)) };
    };
    struct Corner { float x, y; bool red; const char* name; };
    for (const Corner c : { Corner{ dest.left, dest.top, true, "top-left" },
                            Corner{ dest.right, dest.top, false, "top-right" },
                            Corner{ dest.right, dest.bottom, false, "bottom-right" },
                            Corner{ dest.left, dest.bottom, true, "bottom-left" } })
    {
        const auto [x, y] = nearCorner(c.x, c.y);
        const auto p = linearPixel(px, x, y);
        EXPECT_GT(c.red ? p[0] : p[2], 0.98f) << c.name << " corner at " << x << "," << y;
        EXPECT_LT(c.red ? p[2] : p[0], 0.02f) << c.name << " corner at " << x << "," << y;
        EXPECT_LT(p[1], 0.02f) << c.name << " corner at " << x << "," << y;
    }

    // Outside the rotated rect stays white: the corner of the unrotated
    // destination, which the rotation swings away from.
    const auto outside = linearPixel(px, 52, 14);
    EXPECT_GT(outside[0], 0.98f);
    EXPECT_GT(outside[1], 0.98f);
}

// A rect whose far edges are 1e7 away, with a fractional edge on the surface.
// At 1e7 a float's spacing is 1, so a shape evaluated there loses its edge; the
// GPU backend's SDF quad did, until it cut the rect to the surface first.
TEST_F(DrawingTest, HugeRectKeepsItsFractionalEdge)
{
    auto black = g.createSolidColorBrush(Colors::Black);
    g.fillRectangle({ -1.0e7f, -1.0e7f, 1.0e7f, 32.4f }, black);
    g.endDraw();
    drawingActive = false;

    auto bitmap = g.getBitmap();
    auto px = bitmap.lockPixels(BitmapLockFlags::Read);

    for (int x : { 0, 31, 63 })
    {
        EXPECT_GT(inkCoverage(px, x, 0), 0.98f) << "x " << x;
        EXPECT_GT(inkCoverage(px, x, 31), 0.98f) << "x " << x;
        EXPECT_NEAR(inkCoverage(px, x, 32), 0.4f, 0.1f) << "bottom edge at y 32.4, x " << x;
        EXPECT_LT(inkCoverage(px, x, 33), 0.02f) << "x " << x;
    }
}
