// GMPI-UI Drawing Tests (non-text)
// Shapes, gradients, bitmaps, transforms, clipping, blur, etc.

#include "DrawingTestFixture.h"

#include "helpers/BitmapMask.h"
#include "helpers/CachedBlur.h"

// ============================================================
// Tests
// ============================================================

// Clear the entire render target with a flat colour.
TEST_F(DrawingTest, Clear)
{
    g.clear(Colors::CornflowerBlue);
    EXPECT_TRUE(checkResult("clear"));
}

// Fill a rectangle leaving an 8-pixel border.
TEST_F(DrawingTest, FillRectangle)
{
    auto brush = g.createSolidColorBrush(Colors::SteelBlue);
    g.fillRectangle({8.f, 8.f, 56.f, 56.f}, brush);
    EXPECT_TRUE(checkResult("fillRectangle"));
}

// Stroke a rectangle (no fill) with a 2-pixel line.
TEST_F(DrawingTest, DrawRectangle)
{
    auto brush = g.createSolidColorBrush(Colors::DarkRed);
    g.drawRectangle({8.f, 8.f, 56.f, 56.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawRectangle"));
}

// Draw a diagonal line across the bitmap.
TEST_F(DrawingTest, DrawLine)
{
    auto brush = g.createSolidColorBrush(Colors::DarkGreen);
    g.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawLine"));
}

// Fill a circle centred in the bitmap.
TEST_F(DrawingTest, FillEllipse)
{
    auto brush = g.createSolidColorBrush(Colors::Orchid);
    g.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, brush);
    EXPECT_TRUE(checkResult("fillEllipse"));
}

// Stroke a circle (no fill) with a 2-pixel line.
TEST_F(DrawingTest, DrawEllipse)
{
    auto brush = g.createSolidColorBrush(Colors::DarkSlateGray);
    g.drawEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawEllipse"));
}

// Fill a rounded rectangle.
TEST_F(DrawingTest, FillRoundedRectangle)
{
    auto brush = g.createSolidColorBrush(Colors::Tomato);
    g.fillRoundedRectangle(RoundedRect{{8.f, 8.f, 56.f, 56.f}, 8.f, 8.f}, brush);
    EXPECT_TRUE(checkResult("fillRoundedRectangle"));
}

// Stroke a rounded rectangle with a 2-pixel line.
TEST_F(DrawingTest, DrawRoundedRectangle)
{
    auto brush = g.createSolidColorBrush(Colors::MidnightBlue);
    g.drawRoundedRectangle(RoundedRect{{8.f, 8.f, 56.f, 56.f}, 8.f, 8.f}, brush, 2.0f);
    EXPECT_TRUE(checkResult("drawRoundedRectangle"));
}

// Fill a rectangle with a horizontal linear gradient (red → blue).
TEST_F(DrawingTest, LinearGradientFill)
{
    const Gradientstop stops[] = {
        {0.0f, Colors::Red},
        {1.0f, Colors::Blue},
    };
    auto brush = g.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("linearGradientFill", 0, 12.0));
}

// Fill a rectangle with a radial gradient (yellow centre → dark edge).
TEST_F(DrawingTest, RadialGradientFill)
{
    auto brush = g.createRadialGradientBrush(
        {32.f, 32.f}, 28.f, Colors::Yellow, Colors::DarkBlue);
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("radialGradientFill", 0, 4.0));
}

// ============================================================
// Bitmap brush tests
// ============================================================

// Stroke a thick line with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushLine)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::Yellow);
    g.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("bitmapBrushLine"));
}

// Fill a rectangle with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushFillRectangle)
{
    auto brush = makeCheckerboardBrush(Colors::DarkGreen, Colors::White);
    g.fillRectangle({4.f, 4.f, 60.f, 60.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushFillRectangle"));
}

// Fill an ellipse with a checkerboard bitmap brush.
TEST_F(DrawingTest, BitmapBrushFillEllipse)
{
    auto brush = makeCheckerboardBrush(Colors::DarkRed, Colors::White);
    g.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 28.f, 28.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushFillEllipse", 0, 3.0));
}

// Draw text using a bitmap brush as the foreground.
TEST_F(DrawingTest, BitmapBrushText)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::Cyan);
    auto tf    = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushText", 2));
}

// ============================================================
// Gradient brush on lines and text
// ============================================================

// Stroke a thick diagonal line with a horizontal linear gradient.
TEST_F(DrawingTest, LinearGradientLine)
{
    const std::array<Gradientstop, 2> stops = {{
        {0.f, Colors::Red},
        {1.f, Colors::Blue},
    }};
    auto brush = g.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    g.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("linearGradientLine"));
}

// Stroke a thick line with a radial gradient (useful for glowing effects).
TEST_F(DrawingTest, RadialGradientLine)
{
    auto brush = g.createRadialGradientBrush(
        {32.f, 32.f}, 40.f, Colors::White, Colors::DarkBlue);
    g.drawLine({8.f, 8.f}, {56.f, 56.f}, brush, 8.0f);
    EXPECT_TRUE(checkResult("radialGradientLine"));
}

// Draw large text with a horizontal linear gradient foreground.
TEST_F(DrawingTest, LinearGradientText)
{
    const std::array<Gradientstop, 2> stops = {{
        {0.f, Colors::Red},
        {1.f, Colors::Blue},
    }};
    auto brush = g.createLinearGradientBrush(stops, {0.f, 0.f}, {64.f, 0.f});
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("linearGradientText", 2));
}

// Draw large text with a radial gradient foreground.
TEST_F(DrawingTest, RadialGradientText)
{
    auto brush = g.createRadialGradientBrush(
        {32.f, 32.f}, 32.f, Colors::Yellow, Colors::DarkRed);
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("radialGradientText", 2));
}

// ============================================================
// Transparent brush tests
// ============================================================

// Semi-transparent fill blended over a solid background.
TEST_F(DrawingTest, TransparentFill)
{
    auto bgBrush  = g.createSolidColorBrush(Colors::DarkBlue);
    auto fgBrush  = g.createSolidColorBrush(colorFromHex(0xFF4500u, 0.5f)); // OrangeRed 50%
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    g.fillRectangle({8.f, 8.f, 56.f, 56.f}, fgBrush);
    EXPECT_TRUE(checkResult("transparentFill", 0, 13.0));
}

// Two semi-transparent shapes overlapping — tests additive blending order.
TEST_F(DrawingTest, TransparentOverlap)
{
    auto red   = g.createSolidColorBrush(colorFromHex(0xFF0000u, 0.6f));
    auto blue  = g.createSolidColorBrush(colorFromHex(0x0000FFu, 0.6f));
    g.fillEllipse(gmpi::drawing::Ellipse{{24.f, 32.f}, 20.f, 20.f}, red);
    g.fillEllipse(gmpi::drawing::Ellipse{{40.f, 32.f}, 20.f, 20.f}, blue);
    EXPECT_TRUE(checkResult("transparentOverlap", 0, 12.0));
}

// Semi-transparent stroke over a filled rectangle.
TEST_F(DrawingTest, TransparentStroke)
{
    auto fill   = g.createSolidColorBrush(Colors::SteelBlue);
    auto stroke = g.createSolidColorBrush(colorFromHex(0x000000u, 0.4f)); // 40% black
    g.fillRectangle({4.f, 4.f, 60.f, 60.f}, fill);
    g.drawRectangle({12.f, 12.f, 52.f, 52.f}, stroke, 6.0f);
    EXPECT_TRUE(checkResult("transparentStroke", 0, 4.0));
}

// Semi-transparent text over a coloured background.
TEST_F(DrawingTest, TransparentText)
{
    auto bgBrush   = g.createSolidColorBrush(Colors::Tomato);
    auto textBrush = g.createSolidColorBrush(colorFromHex(0xFFFFFFu, 0.6f)); // 60% white
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    auto tf = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, textBrush);
    EXPECT_TRUE(checkResult("transparentText", 2));
}

// Three vertical stripes that should all look identical (50% grey):
//   1. Solid 50% grey (128,128,128 fully opaque)
//   2. 50% alpha white over black background
//   3. 50% alpha black over white background
TEST_F(DrawingTest, AlphaEquivalentGrey)
{
    // stripe 1: solid 50% grey
    auto grey  = g.createSolidColorBrush(Color{0.5f, 0.5f, 0.5f, 1.0f});
    g.fillRectangle({0.f, 0.f, 21.f, 64.f}, grey);

    // stripe 2: black bg, then 50% alpha white on top
    auto black = g.createSolidColorBrush(Colors::Black);
    auto white50 = g.createSolidColorBrush(Color{1.f, 1.f, 1.f, 0.5f});
    g.fillRectangle({21.f, 0.f, 43.f, 64.f}, black);
    g.fillRectangle({21.f, 0.f, 43.f, 64.f}, white50);

    // stripe 3: white bg, then 50% alpha black on top
    auto white = g.createSolidColorBrush(Colors::White);
    auto black50 = g.createSolidColorBrush(Color{0.f, 0.f, 0.f, 0.5f});
    g.fillRectangle({43.f, 0.f, 64.f, 64.f}, white);
    g.fillRectangle({43.f, 0.f, 64.f, 64.f}, black50);

    EXPECT_TRUE(checkResult("alphaEquivalentGrey", 0, 22.0));
}

// ============================================================
// Bitmap draw and stretch tests  (drawBitmap, not BitmapBrush)
// ============================================================

// Helper: build a small 16x16 RGBA test pattern (4 coloured quadrants).
// Returns the bitmap; uses the fixture's dxFactory and kRenderFlags.
// (defined as a lambda inside each test to keep captures clear, but the
//  logic is the same so we put it in the fixture as a method.)

// Draw a bitmap 1:1 at its natural size (pixel-exact, no interpolation).
TEST_F(DrawingTest, DrawBitmapNative)
{
    // Build a 16x16 four-quadrant pattern.
    auto patRT = drawingContext.factory().createCpuRenderTarget({ 16, 16 }, kRenderFlags);
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Draw centred, 1:1.
    g.drawBitmap(bmp, {24.f, 24.f, 40.f, 40.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapNative"));
}

// Stretch the same 16x16 bitmap to fill most of the render target.
TEST_F(DrawingTest, DrawBitmapStretched)
{
    auto patRT = drawingContext.factory().createCpuRenderTarget({16, 16}, kRenderFlags);
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Stretch to 56x56 with nearest-neighbour (pixel-exact, no blending).
    g.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapStretched", 0, 7.0));
}

// Stretch with bilinear interpolation — smooth edges between quadrants.
TEST_F(DrawingTest, DrawBitmapLinearInterp)
{
    auto patRT = drawingContext.factory().createCpuRenderTarget({16, 16}, kRenderFlags);
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    g.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  1.0f, BitmapInterpolationMode::Linear);
    EXPECT_TRUE(checkResult("drawBitmapLinearInterp", 0, 7.0));
}

// Draw only a sub-rectangle (top-right quadrant) of the source bitmap.
TEST_F(DrawingTest, DrawBitmapCropped)
{
    auto patRT = drawingContext.factory().createCpuRenderTarget({16, 16}, kRenderFlags);
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    // Source: top-right quadrant (green). Destination: stretched to most of the bitmap.
    g.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {8.f, 0.f, 16.f, 8.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapCropped"));
}

// Draw a bitmap at 50% opacity over a coloured background.
TEST_F(DrawingTest, DrawBitmapOpacity)
{
    auto patRT = drawingContext.factory().createCpuRenderTarget({16, 16}, kRenderFlags);
    patRT.beginDraw();
    auto redBrush    = patRT.createSolidColorBrush(Colors::Red);
    auto greenBrush  = patRT.createSolidColorBrush(Colors::Green);
    auto blueBrush   = patRT.createSolidColorBrush(Colors::Blue);
    auto yellowBrush = patRT.createSolidColorBrush(Colors::Yellow);
    patRT.fillRectangle({0.f,  0.f,  8.f,  8.f},  redBrush);
    patRT.fillRectangle({8.f,  0.f,  16.f, 8.f},  greenBrush);
    patRT.fillRectangle({0.f,  8.f,  8.f,  16.f}, blueBrush);
    patRT.fillRectangle({8.f,  8.f,  16.f, 16.f}, yellowBrush);
    patRT.endDraw();
    auto bmp = patRT.getBitmap();

    auto bgBrush = g.createSolidColorBrush(Colors::DarkSlateGray);
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    g.drawBitmap(bmp, {4.f, 4.f, 60.f, 60.f}, {0.f, 0.f, 16.f, 16.f},
                  0.5f, BitmapInterpolationMode::NearestNeighbor);
    EXPECT_TRUE(checkResult("drawBitmapOpacity", 0, 9.0));
}

// ============================================================
// Clipping tests
// ============================================================

// Fill the whole target, clip to an inner rect, fill again — only the
// inner region should show the second colour.
TEST_F(DrawingTest, ClipBasic)
{
    auto bgBrush = g.createSolidColorBrush(Colors::SteelBlue);
    auto fgBrush = g.createSolidColorBrush(Colors::OrangeRed);
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    g.pushAxisAlignedClip({16.f, 16.f, 48.f, 48.f});
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, fgBrush); // only 16..48 visible
    g.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("clipBasic"));
}

// Two nested clips: only their intersection should receive the fill.
TEST_F(DrawingTest, ClipNested)
{
    auto bgBrush = g.createSolidColorBrush(Colors::DarkSlateGray);
    auto fgBrush = g.createSolidColorBrush(Colors::Yellow);
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    g.pushAxisAlignedClip({8.f,  8.f,  56.f, 56.f});
    g.pushAxisAlignedClip({20.f, 20.f, 64.f, 64.f}); // intersection: 20..56
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, fgBrush);
    g.popAxisAlignedClip();
    g.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("clipNested"));
}

// ============================================================
// Transform tests
// ============================================================

// Scale transform: a small rectangle appears stretched.
TEST_F(DrawingTest, TransformScale)
{
    auto brush = g.createSolidColorBrush(Colors::Tomato);
    g.setTransform(makeScale(2.f, 1.5f));
    g.fillRectangle({8.f, 8.f, 24.f, 24.f}, brush);
    g.setTransform(Matrix3x2{}); // identity
    EXPECT_TRUE(checkResult("transformScale"));
}

// Translation: draw at origin, transform shifts it.
TEST_F(DrawingTest, TransformTranslate)
{
    auto brush = g.createSolidColorBrush(Colors::DodgerBlue);
    g.setTransform(makeTranslation(20.f, 16.f));
    g.fillRectangle({0.f, 0.f, 20.f, 20.f}, brush);
    g.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("transformTranslate"));
}

// 45-degree rotation around the bitmap centre.
TEST_F(DrawingTest, TransformRotate)
{
    constexpr float kPi = 3.14159265f;
    auto brush = g.createSolidColorBrush(Colors::Orchid);
    g.setTransform(makeRotation(kPi / 4.f, {32.f, 32.f}));
    g.fillRectangle({20.f, 20.f, 44.f, 44.f}, brush);
    g.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("transformRotate"));
}

// Draw with a transform then reset — both shapes should be visible.
TEST_F(DrawingTest, TransformReset)
{
    auto red  = g.createSolidColorBrush(Colors::Red);
    auto blue = g.createSolidColorBrush(Colors::Blue);
    g.setTransform(makeTranslation(0.f, 32.f));
    g.fillRectangle({0.f, 0.f, 32.f, 32.f}, red);  // lands at y=32..64
    g.setTransform(Matrix3x2{});
    g.fillRectangle({32.f, 0.f, 64.f, 32.f}, blue); // stays at x=32..64, y=0..32
    EXPECT_TRUE(checkResult("transformReset", 0, 6.0));
}

// ============================================================
// Stroke style tests
// ============================================================

// Dashed line.
TEST_F(DrawingTest, StrokeStyleDash)
{
    StrokeStyleProperties props;
    props.dashStyle = DashStyle::Dash;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::DarkRed);
    g.drawLine({8.f, 32.f}, {56.f, 32.f}, brush, 4.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleDash"));
}

// Dotted line with round caps.
TEST_F(DrawingTest, StrokeStyleDot)
{
    StrokeStyleProperties props;
    props.dashStyle = DashStyle::Dot;
    props.lineCap   = CapStyle::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::DarkGreen);
    g.drawLine({8.f, 32.f}, {56.f, 32.f}, brush, 4.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleDot"));
}

// Round caps extend beyond the line endpoints.
TEST_F(DrawingTest, StrokeStyleRoundCap)
{
    StrokeStyleProperties props;
    props.lineCap = CapStyle::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::DarkSlateBlue);
    g.drawLine({16.f, 32.f}, {48.f, 32.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleRoundCap"));
}

// Square caps extend squarely beyond the endpoints.
TEST_F(DrawingTest, StrokeStyleSquareCap)
{
    StrokeStyleProperties props;
    props.lineCap = CapStyle::Square;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::Sienna);
    g.drawLine({16.f, 32.f}, {48.f, 32.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleSquareCap"));
}

// Bevel join on a stroked rectangle.
TEST_F(DrawingTest, StrokeStyleBevelJoin)
{
    StrokeStyleProperties props;
    props.lineJoin = LineJoin::Bevel;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::DarkOliveGreen);
    g.drawRectangle({12.f, 12.f, 52.f, 52.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleBevelJoin"));
}

// Round join on a stroked rectangle.
TEST_F(DrawingTest, StrokeStyleRoundJoin)
{
    StrokeStyleProperties props;
    props.lineJoin = LineJoin::Round;
    auto strokeStyle = makeStrokeStyle(props);
    auto brush = g.createSolidColorBrush(Colors::DarkMagenta);
    g.drawRectangle({12.f, 12.f, 52.f, 52.f}, brush, 8.f, strokeStyle);
    EXPECT_TRUE(checkResult("strokeStyleRoundJoin", 0, 3.0));
}

// All four line-join styles on an acute V-shape drawn side by side.
// Columns (left to right): default (Miter), Bevel, Round, MiterOrBevel.
TEST_F(DrawingTest, StrokeStyleLineJoins)
{
    g.clear(Colors::White);

    // Each V is drawn in a 16-wide column.  The apex points downward so the
    // acute angle makes the miter spike clearly visible.
    struct JoinCase { LineJoin join; Color color; };
    const JoinCase cases[] = {
        { LineJoin::Miter,        Colors::Crimson      },
        { LineJoin::Bevel,        Colors::DarkGreen    },
        { LineJoin::Round,        Colors::DarkBlue     },
        { LineJoin::MiterOrBevel, Colors::DarkOrange   },
    };

    for (int i = 0; i < 4; ++i)
    {
        const float cx = 8.f + i * 16.f;   // column centre x
        const float top = 8.f;
        const float apexY = 56.f;
        const float halfW = 6.f;            // half-width of the V arms at top

        StrokeStyleProperties props;
        props.lineJoin = cases[i].join;
        auto ss    = makeStrokeStyle(props);
        auto brush = g.createSolidColorBrush(cases[i].color);

        auto geom = g.getFactory().createPathGeometry();
        auto sink = geom.open();
        sink.beginFigure({cx - halfW, top}, FigureBegin::Hollow);
        sink.addLine({cx,            apexY});
        sink.addLine({cx + halfW,    top});
        sink.endFigure(FigureEnd::Open);
        sink.close();

        g.drawGeometry(geom, brush, 4.f, ss);
    }

    EXPECT_TRUE(checkResult("strokeStyleLineJoins", 0, 4.0));
}

// ============================================================
// Degenerate / boundary cases
// ============================================================

// A fully transparent brush (alpha=0) should render nothing.
TEST_F(DrawingTest, FullyTransparentBrush)
{
    auto brush = g.createSolidColorBrush(colorFromHex(0xFF0000u, 0.0f)); // red, alpha=0
    g.fillRectangle({8.f, 8.f, 56.f, 56.f}, brush);
    EXPECT_TRUE(checkResult("fullyTransparentBrush"));
}

// A rect larger than the render target is clamped to its bounds.
TEST_F(DrawingTest, ShapeAtBoundary)
{
    auto brush = g.createSolidColorBrush(Colors::DarkOrchid);
    g.fillRectangle({-8.f, -8.f, 72.f, 72.f}, brush);
    EXPECT_TRUE(checkResult("shapeAtBoundary"));
}

// Zero-width stroke — platform-defined; at minimum must not crash.
TEST_F(DrawingTest, ZeroWidthStroke)
{
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawRectangle({8.f, 8.f, 56.f, 56.f}, brush, 0.0f);
    EXPECT_TRUE(checkResult("zeroWidthStroke", 0, 8.0));
}

// ============================================================
// Bitmap brush origin / offset
// ============================================================

// Fill rect aligned to the pattern grid (rect origin == world origin == tile boundary).
// The 8x8 checkerboard tiles should appear perfectly aligned with the rect corners.
TEST_F(DrawingTest, BitmapBrushOriginAligned)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::LightGray);
    // Rect starts at (0,0) — world origin — so tile boundaries coincide with rect edges.
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushOriginAligned", 0, 4.0));
}

// Fill rect offset from the pattern grid: the brush origin stays at world (0,0),
// so the tile pattern appears shifted by (4,4) relative to the rect's top-left corner.
TEST_F(DrawingTest, BitmapBrushOriginOffset)
{
    auto brush = makeCheckerboardBrush(Colors::DarkBlue, Colors::LightGray);
    // Rect starts at (4,4): 4 pixels into the 8-pixel tile, so the corner pixel
    // comes from the middle of a tile rather than a tile boundary.
    g.fillRectangle({4.f, 4.f, 60.f, 60.f}, brush);
    EXPECT_TRUE(checkResult("bitmapBrushOriginOffset", 0, 3.0));
}

// Demonstrate that setTransform shifts the pattern origin together with geometry:
// the brush samples world-space coordinates, so translating the render target
// shifts both the rect AND the pattern by the same amount — the pattern is
// phase-shifted relative to where an un-transformed rect would land.
TEST_F(DrawingTest, BitmapBrushOriginWithTransform)
{
    auto brush = makeCheckerboardBrush(Colors::Crimson, Colors::LightGray);
    // With a (4,4) translation, drawing rect (0,0)-(56,56) is equivalent to
    // drawing at (4,4)-(60,60) in world space — the pattern tiles from world (0,0)
    // so the visible corner of the rect is 4 pixels into the first tile.
    g.setTransform(makeTranslation(4.f, 4.f));
    g.fillRectangle({0.f, 0.f, 56.f, 56.f}, brush);
    g.setTransform(Matrix3x2{});
    EXPECT_TRUE(checkResult("bitmapBrushOriginWithTransform"));
}

// ============================================================
// Polygon fill rules
// ============================================================
//
// The classic tests for FillMode::Alternate (even-odd) vs FillMode::Winding.
//
// Helper: build a pentagram (5-pointed star) as a self-intersecting closed path.
// The outer tips are at radius r from centre (cx,cy), drawn by connecting every
// other vertex of a regular pentagon.  The inner pentagon is visited twice, so
// its winding number is 2 under the winding rule (filled) but 2 mod 2 = 0 under
// the even-odd rule (hole).
//
// Helper: make a square contour (4 vertices, Y-down screen coords).
//   CW  in screen space: TL→TR→BR→BL
//   CCW in screen space: TL→BL→BR→TR

namespace {

PathGeometry makePentagram(gmpi::drawing::BitmapRenderTarget& rt, FillMode fm)
{
    constexpr float cx = 32.f, cy = 32.f, r = 28.f;
    constexpr float kPi = 3.14159265f;
    auto pt = [&](float deg) -> gmpi::drawing::Point {
        const float rad = deg * kPi / 180.f;
        return {cx + r * std::cos(rad), cy + r * std::sin(rad)};
    };

    auto geom = rt.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(fm);
    // Skip every other vertex: 0°-indexed from top (-90°), step 144°
    sink.beginFigure(pt(-90.f), FigureBegin::Filled);
    sink.addLine(pt(-90.f + 144.f));
    sink.addLine(pt(-90.f + 288.f));
    sink.addLine(pt(-90.f + 432.f));
    sink.addLine(pt(-90.f + 576.f));
    sink.endFigure(FigureEnd::Closed);
    sink.close();
    return geom;
}

// Square contour helper.
// clockwise=true  → TL→TR→BR→BL  (positive winding in screen/Y-down space)
// clockwise=false → TL→BL→BR→TR  (negative winding)
void addSquareFigure(GeometrySink& sink, gmpi::drawing::Rect r, bool clockwise)
{
    if (clockwise)
    {
        sink.beginFigure({r.left,  r.top},    FigureBegin::Filled);
        sink.addLine    ({r.right, r.top});
        sink.addLine    ({r.right, r.bottom});
        sink.addLine    ({r.left,  r.bottom});
    }
    else
    {
        sink.beginFigure({r.left,  r.top},    FigureBegin::Filled);
        sink.addLine    ({r.left,  r.bottom});
        sink.addLine    ({r.right, r.bottom});
        sink.addLine    ({r.right, r.top});
    }
    sink.endFigure(FigureEnd::Closed);
}

} // anonymous namespace

// ---- Stars ----

// Pentagram with Alternate (even-odd) fill: the inner pentagon is a hole because
// a ray from there crosses the path boundary an even number of times (2).
TEST_F(DrawingTest, FillModeAlternateStar)
{
    auto geom  = makePentagram(g, FillMode::Alternate);
    auto brush = g.createSolidColorBrush(Colors::SteelBlue);
    g.fillGeometry(geom, brush);
    auto outline = g.createSolidColorBrush(Colors::DarkSlateGray);
    g.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeAlternateStar"));
}

// Same pentagram with Winding fill: every region has non-zero winding number so
// the entire star — including the inner pentagon — is filled solid.
TEST_F(DrawingTest, FillModeWindingStar)
{
    auto geom  = makePentagram(g, FillMode::Winding);
    auto brush = g.createSolidColorBrush(Colors::Tomato);
    g.fillGeometry(geom, brush);
    auto outline = g.createSolidColorBrush(Colors::DarkRed);
    g.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingStar"));
}

// ---- Nested squares ----

// Alternate rule: inner square always becomes a hole regardless of direction
// (a point inside crosses 2 contour boundaries → even → not filled).
TEST_F(DrawingTest, FillModeAlternateNestedSquares)
{
    auto geom = g.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Alternate);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);  // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, true);  // inner CW — still a hole
    sink.close();

    auto brush   = g.createSolidColorBrush(Colors::CornflowerBlue);
    auto outline = g.createSolidColorBrush(Colors::DarkBlue);
    g.fillGeometry(geom, brush);
    g.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeAlternateNestedSquares", 0, 3.0));
}

// Winding rule, both squares CW: inner winding = 2 (outer CW + inner CW adds),
// so the inner square IS filled (both regions have non-zero winding).
TEST_F(DrawingTest, FillModeWindingNestedSameDir)
{
    auto geom = g.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Winding);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);   // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, true);   // inner CW  → winding=2, filled
    sink.close();

    auto brush   = g.createSolidColorBrush(Colors::DarkOrange);
    auto outline = g.createSolidColorBrush(Colors::Sienna);
    g.fillGeometry(geom, brush);
    g.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingNestedSameDir"));
}

// Winding rule, outer CW + inner CCW: the inner winding cancels to 0,
// so the inner square becomes a hole — same visual as the Alternate case.
TEST_F(DrawingTest, FillModeWindingNestedOppositeDir)
{
    auto geom = g.getFactory().createPathGeometry();
    auto sink = geom.open();
    sink.setFillMode(FillMode::Winding);
    addSquareFigure(sink, {8.f,  8.f,  56.f, 56.f}, true);   // outer CW
    addSquareFigure(sink, {20.f, 20.f, 44.f, 44.f}, false);  // inner CCW → winding=0, hole
    sink.close();

    auto brush   = g.createSolidColorBrush(Colors::MediumPurple);
    auto outline = g.createSolidColorBrush(Colors::DarkMagenta);
    g.fillGeometry(geom, brush);
    g.drawGeometry(geom, outline, 1.f);
    EXPECT_TRUE(checkResult("fillModeWindingNestedOppositeDir"));
}

// ============================================================
// Colour round-trip test
// ============================================================

// Load a PNG with known sRGB grey values, draw it 1:1, save, and compare.
// The source gradient (128x20, column x = sRGB grey value x for x=0..127)
// is also used as the reference image.  A perfect round-trip should
// reproduce every sRGB value exactly; any gamma or precision error will
// appear as differing pixels.
TEST_F(DrawingTest, ColourRoundTrip)
{
    const auto gradPath = referenceDir() / "colourRoundTrip.png";

#ifdef _WIN32
    ASSERT_TRUE(DrawingTestContext::createSRGBGradientPng(gradPath))
        << "Failed to create sRGB gradient PNG at: " << gradPath.string();
#endif

    // Load the gradient PNG as a bitmap.
    auto srcBmp = drawingContext.factory().loadImageU(gradPath.string());
    ASSERT_TRUE(srcBmp) << "Failed to load gradient PNG: " << gradPath.string();

    // Render into a dedicated 128x20 render target, drawing the bitmap 1:1.
    auto rt = drawingContext.factory().createCpuRenderTarget({128, 20}, kRenderFlags);
    rt.beginDraw();
    rt.drawBitmap(srcBmp, {0.f, 0.f, 128.f, 20.f}, {0.f, 0.f, 128.f, 20.f},
                  1.0f, BitmapInterpolationMode::NearestNeighbor);
    rt.endDraw();

    // The gradient PNG is the reference — the round-trip output must match it.
    EXPECT_TRUE(checkBitmap("colourRoundTrip", rt));
}

// ============================================================
// Blur / shadow tests (CachedBlur)
// ============================================================

// Black text on white with a soft drop shadow behind it.
TEST_F(DrawingTest, BlurTextShadow)
{
    constexpr uint32_t kW = 128, kH = 64;
    auto factory = g.getFactory();
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(20.f);

    const gmpi::drawing::Rect textRect{10.f, 14.f, 120.f, 50.f};
    constexpr float shadowOffX = 2.f, shadowOffY = 2.f;
    const gmpi::drawing::Rect bounds{0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH)};

    // Draw shadow: blurred dark text offset down-right.
    cachedBlur shadow;
    shadow.tint = Color{0.f, 0.f, 0.f, 1.f}; // black
    shadow.draw(bigRT, bounds, [&](Graphics& mask) {
        auto brush = mask.createSolidColorBrush(Colors::White);
        mask.drawTextU("Shadow", tf,
            {textRect.left + shadowOffX, textRect.top + shadowOffY,
             textRect.right + shadowOffX, textRect.bottom + shadowOffY}, brush);
    });

    // Draw crisp text on top.
    auto textBrush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU("Shadow", tf, textRect, textBrush);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("blurTextShadow", bigRT, 2));
}

// Lime text on black with a glowing halo.
TEST_F(DrawingTest, BlurTextGlow)
{
    constexpr uint32_t kW = 128, kH = 64;
    auto factory = g.getFactory();
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::Black);

    auto tf = makeTextFormat(20.f);

    const gmpi::drawing::Rect textRect{10.f, 14.f, 120.f, 50.f};
    const gmpi::drawing::Rect bounds{0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH)};

    // Draw glow: blurred lime around the text (no offset).
    cachedBlur glow;
    glow.tint = Colors::Lime;
    glow.draw(bigRT, bounds, [&](Graphics& mask) {
        auto brush = mask.createSolidColorBrush(Colors::White);
        mask.drawTextU("Glow", tf, textRect, brush);
    });

    // Draw crisp text on top.
    auto textBrush = bigRT.createSolidColorBrush(Colors::Lime);
    bigRT.drawTextU("Glow", tf, textRect, textBrush);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("blurTextGlow", bigRT, 2));
}


// Neumorphic "dip" — recessed rounded rectangle with inner shadows.
// Shadows are rendered to an offscreen target, then a shape mask is applied
// pixel-by-pixel so shadows are clipped to the interior without overpainting
// the background — works correctly regardless of what the background contains.

TEST_F(DrawingTest, BlurNeumorphicDip)
{
    constexpr uint32_t kW = 128, kH = 128;
    // Mask + CpuReadable for the offscreen targets we'll lock.
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
                                | (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();

    // Cream background with light-blue checkerboard tiles to expose alpha blending.
    {
        const Color bgCream = colorFromHex(0xF6E7D7u);
        const Color bgBlue = colorFromHex(0xD9EAF8u);
        const float tile = 16.f;

        bigRT.clear(bgCream);
        auto checkerBrush = bigRT.createSolidColorBrush(bgBlue);
        for(float y = 0.f; y < static_cast<float>(kH); y += tile)
        {
            for(float x = ((static_cast<int>(y / tile)) & 1) ? tile : 0.f; x < static_cast<float>(kW); x += tile * 2.f)
            {
                bigRT.fillRectangle({ x, y, x + tile, y + tile }, checkerBrush);
            }
        }
    }


    {
        const Color bgColor = colorFromHex(0xE0E0E0u);
        bigRT.clear(bgColor);

        const RoundedRect shape{ {24.f, 24.f, 104.f, 104.f}, 16.f, 16.f };
        const gmpi::drawing::Rect bounds{ 0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH) };
        constexpr float offset = 4.f;

        // helper: full-canvas rect with a shifted rounded-rect hole punched out
        auto makeHoleGeom = [&](Graphics& ctx, float dx, float dy) {
            auto geom = ctx.getFactory().createPathGeometry();
            auto sink = geom.open();
            sink.setFillMode(FillMode::Alternate);
            sink.addRect({ 0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH) }, FigureBegin::Filled);
            sink.addRoundedRect({ {shape.rect.left  + dx, shape.rect.top    + dy,
                                   shape.rect.right + dx, shape.rect.bottom + dy},
                                  shape.radiusX, shape.radiusY });
            sink.close();
            return geom;
        };

        // --- 1. Render inner shadow layers to a separate render target ---
        // Use the same format as bigRT (kRenderFlags) so cachedBlur's compositing
        // colour-space matches exactly, producing identical shadow pixels inside
        // the shape to the reference.
        auto shadowRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
        shadowRT.beginDraw();
        shadowRT.clear(Color{0.f, 0.f, 0.f, 0.f}); // transparent
        {
            cachedBlur innerDark;
            innerDark.tint = Color{ 0.f, 0.f, 0.f, 0.5f };
            innerDark.draw(shadowRT, bounds, [&](Graphics& m) {
                auto darkGeom = makeHoleGeom(m, offset, offset);
                auto brush = m.createSolidColorBrush(Colors::White);
                m.fillGeometry(darkGeom, brush);
            });

            cachedBlur innerLight;
            innerLight.tint = Color{ 1.f, 1.f, 1.f, 0.8f };
            innerLight.draw(shadowRT, bounds, [&](Graphics& m) {
                auto lightGeom = makeHoleGeom(m, -offset, -offset);
                auto brush = m.createSolidColorBrush(Colors::White);
                m.fillGeometry(lightGeom, brush);
            });
        }
        shadowRT.endDraw();

        // --- 2. Render shape mask: white inside rounded rect, black outside ---
        // Mask → 8bpp alpha-only (1 byte per pixel).
        auto maskRT = factory.createCpuRenderTarget({kW, kH}, kMask);
        maskRT.beginDraw();
        maskRT.clear(Color{0.f, 0.f, 0.f, 0.f});
        {
            auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
            maskRT.fillRoundedRectangle(shape, whiteBrush);
        }
        maskRT.endDraw();

        // --- 3. Apply mask to shadow image.
        {
            auto shadowBmp = shadowRT.getBitmap();
            auto maskBmp   = maskRT.getBitmap();
            applyMask(shadowBmp, maskBmp);
        }

        // --- 4. Composite the masked shadow overlay onto bigRT ---
        {
            auto shadowBmp = shadowRT.getBitmap();
            bigRT.drawBitmap(shadowBmp, bounds, bounds);
        }
    }
    bigRT.endDraw();

    EXPECT_TRUE(checkBitmap("blurNeumorphicDip", bigRT, 2));
}

// Create an 8-bit (monochrome, single-plane) mask bitmap, draw a circle on it,
// then verify pixel format and spot-check pixel values.
TEST_F(DrawingTest, EightBitBitmapToOutput)
{
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
                                | (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    // Create an 8-bit offscreen render target and draw a filled circle.
    auto offscreen = factory.createCpuRenderTarget({kWidth, kHeight}, kMask);
    offscreen.beginDraw();
    offscreen.clear(Color{0.f, 0.f, 0.f, 0.f});
    auto brush = offscreen.createSolidColorBrush(Colors::White);
    offscreen.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, brush);
    offscreen.endDraw();

    // Lock pixels and verify format.
    auto bmp = offscreen.getBitmap();
    auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
    ASSERT_TRUE(pixels);

    const uint8_t* data = pixels.getAddress();
    const int32_t  bpr  = pixels.getBytesPerRow();
    const auto     size = bmp.getSize();

    // Verify 8-bit monochrome: 1 byte per pixel.
    EXPECT_EQ(bpr, static_cast<int32_t>(size.width))
        << "Expected 1 byte per pixel (8-bit monochrome), got " << bpr << " bytes per row for " << size.width << " pixels wide";

    // Centre of circle (32,32) — should be white (0xFF).
    const uint8_t centre = data[32 * bpr + 32];
    EXPECT_EQ(centre, 0xFF) << "Centre pixel should be white (0xFF)";

    // Corner (0,0) — well outside the circle, should be black (0x00).
    const uint8_t corner = data[0 * bpr + 0];
    EXPECT_EQ(corner, 0x00) << "Corner pixel should be black (0x00)";

    // Just inside the circle (32,9) — should be white.
    const uint8_t inside = data[9 * bpr + 32];
    EXPECT_EQ(inside, 0xFF) << "Pixel just inside circle should be white";

    // Just outside the circle (32,7) — should be black.
    const uint8_t outside = data[7 * bpr + 32];
    EXPECT_EQ(outside, 0x00) << "Pixel just outside circle should be black";
}

// Create an 8-bit mono mask (circle) and a colour image (solid fill),
// apply the mask to the image, then draw the result over a checkerboard.
TEST_F(DrawingTest, EightBitMaskBitmapToOutput)
{
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
                                | (int32_t)BitmapRenderTargetFlags::CpuReadable;
    constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    // 1. Mono mask: circle (white=opaque) on transparent background.
    auto maskRT = factory.createCpuRenderTarget({kWidth, kHeight}, kMask);
    maskRT.beginDraw();
    maskRT.clear(Color{0.f, 0.f, 0.f, 0.f});
    auto maskBrush = maskRT.createSolidColorBrush(Colors::White);
    maskRT.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, maskBrush);
    maskRT.endDraw();

    // 2. Colour image: solid DodgerBlue.
    auto colorRT = factory.createCpuRenderTarget({kWidth, kHeight}, kColorCpu);
    colorRT.beginDraw();
    colorRT.clear(Colors::DodgerBlue);
    colorRT.endDraw();

    // 3. Apply mask to colour image.
    auto maskBmp  = maskRT.getBitmap();
    auto colorBmp = colorRT.getBitmap();
    applyMask(colorBmp, maskBmp);

    // 4. Cream/blue checkerboard background to expose alpha blending.
    {
        const Color bgCream = colorFromHex(0xF6E7D7u);
        const Color bgBlue  = colorFromHex(0xD9EAF8u);
        constexpr float tile = 16.f;

        g.clear(bgCream);
        auto checkerBrush = g.createSolidColorBrush(bgBlue);
        for (float y = 0.f; y < static_cast<float>(kHeight); y += tile)
            for (float x = ((static_cast<int>(y / tile)) & 1) ? tile : 0.f; x < static_cast<float>(kWidth); x += tile * 2.f)
                g.fillRectangle({x, y, x + tile, y + tile}, checkerBrush);
    }

    auto resultBmp = colorRT.getBitmap();
    g.drawBitmap(resultBmp, {0.f, 0.f, 64.f, 64.f}, {0.f, 0.f, 64.f, 64.f},
                 1.0f, BitmapInterpolationMode::NearestNeighbor);

    EXPECT_TRUE(checkResult("eightBitMaskBitmapToOutput", 0, 4.0));
}

// Create an sRGB (32bpp PBGRA) offscreen render target, draw on it,
// then verify pixel format and spot-check pixel values.
TEST_F(DrawingTest, SRGBBitmapPixelFormat)
{
    constexpr int32_t kSRGB = (int32_t)BitmapRenderTargetFlags::SRGBPixels
                            | (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    // Draw a filled rectangle on an sRGB offscreen target.
    auto offscreen = factory.createCpuRenderTarget({kWidth, kHeight}, kSRGB);
    offscreen.beginDraw();
    offscreen.clear(Color{0.f, 0.f, 0.f, 0.f}); // transparent
    auto brush = offscreen.createSolidColorBrush(Colors::Red);
    offscreen.fillRectangle({8.f, 8.f, 56.f, 56.f}, brush);
    offscreen.endDraw();

    // Lock pixels and verify 32bpp format.
    auto bmp = offscreen.getBitmap();
    auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
    ASSERT_TRUE(pixels);

    const uint8_t* data = pixels.getAddress();
    const int32_t  bpr  = pixels.getBytesPerRow();
    const auto     size = bmp.getSize();

    // Verify 32bpp: 4 bytes per pixel → bytesPerRow == width * 4.
    EXPECT_EQ(bpr, static_cast<int32_t>(size.width) * 4)
        << "Expected 4 bytes per pixel (32bpp), got " << bpr
        << " bytes per row for " << size.width << " pixels wide";

    // Channel indices: Windows = BGRA, macOS = RGBA.
#ifdef _WIN32
    constexpr int iR = 2, iG = 1, iB = 0, iA = 3;
#else
    constexpr int iR = 0, iG = 1, iB = 2, iA = 3;
#endif

    // Centre of red rectangle (32,32) — Red should be 255, G and B near 0.
    const uint8_t* centre = data + 32 * bpr + 32 * 4;
    // macOS calibrated RGB may differ slightly from sRGB primaries.
    EXPECT_NEAR(centre[iB], 0x00, 5) << "B channel should be ~0";
    EXPECT_NEAR(centre[iG], 0x00, 40) << "G channel should be ~0";
    EXPECT_NEAR(centre[iR], 0xFF, 5) << "R channel should be ~255";
    EXPECT_EQ(centre[iA], 0xFF) << "A channel should be 255";

    // Corner (0,0) — outside the rectangle, should be fully transparent.
    const uint8_t* corner = data;
    EXPECT_EQ(corner[0], 0x00) << "channel 0 should be 0 (transparent)";
    EXPECT_EQ(corner[1], 0x00) << "channel 1 should be 0 (transparent)";
    EXPECT_EQ(corner[2], 0x00) << "channel 2 should be 0 (transparent)";
    EXPECT_EQ(corner[3], 0x00) << "A should be 0 (transparent)";
}

// Create an sRGB (32bpp) offscreen render target, draw coloured shapes,
// then composite it onto the main output over a checkerboard.
TEST_F(DrawingTest, SRGBBitmapToOutput)
{
    constexpr int32_t kSRGB = (int32_t)BitmapRenderTargetFlags::SRGBPixels
                            | (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    // Draw a red circle and a blue rectangle on an sRGB offscreen target.
    auto offscreen = factory.createCpuRenderTarget({kWidth, kHeight}, kSRGB);
    offscreen.beginDraw();
    offscreen.clear(Color{0.f, 0.f, 0.f, 0.f}); // transparent

    auto redBrush = offscreen.createSolidColorBrush(Colors::Red);
    offscreen.fillEllipse(gmpi::drawing::Ellipse{{32.f, 32.f}, 24.f, 24.f}, redBrush);

    auto blueBrush = offscreen.createSolidColorBrush(Color{0.f, 0.f, 1.f, 0.5f}); // semi-transparent blue
    offscreen.fillRectangle({16.f, 16.f, 48.f, 48.f}, blueBrush);

    offscreen.endDraw();

    // Cream/blue checkerboard background to expose alpha blending.
    {
        const Color bgCream = colorFromHex(0xF6E7D7u);
        const Color bgBlue  = colorFromHex(0xD9EAF8u);
        constexpr float tile = 16.f;

        g.clear(bgCream);
        auto checkerBrush = g.createSolidColorBrush(bgBlue);
        for (float y = 0.f; y < static_cast<float>(kHeight); y += tile)
            for (float x = ((static_cast<int>(y / tile)) & 1) ? tile : 0.f; x < static_cast<float>(kWidth); x += tile * 2.f)
                g.fillRectangle({x, y, x + tile, y + tile}, checkerBrush);
    }

    // Composite the sRGB bitmap onto the output.
    auto resultBmp = offscreen.getBitmap();
    g.drawBitmap(resultBmp, {0.f, 0.f, 64.f, 64.f}, {0.f, 0.f, 64.f, 64.f},
                 1.0f, BitmapInterpolationMode::NearestNeighbor);

    EXPECT_TRUE(checkResult("srgbBitmapToOutput", 0, 4.0));
}

// Neumorphic "dip" over a checkerboard background.
// Inner shadows are rendered to a transparent image, masked to the shape,
// then composited over the checkerboard so the background shows through.
TEST_F(DrawingTest, BlurNeumorphicDipCheckerboard)
{
    constexpr uint32_t kW = 128, kH = 128;
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
                                | (int32_t)BitmapRenderTargetFlags::CpuReadable;
    constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    const RoundedRect shape{ {24.f, 24.f, 104.f, 104.f}, 16.f, 16.f };
    const gmpi::drawing::Rect bounds{ 0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH) };
    constexpr float offset = 4.f;

    // Helper: full-canvas rect with a shifted rounded-rect hole punched out.
    auto makeHoleGeom = [&](Graphics& ctx, float dx, float dy) {
        auto geom = ctx.getFactory().createPathGeometry();
        auto sink = geom.open();
        sink.setFillMode(FillMode::Alternate);
        sink.addRect({0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH)}, FigureBegin::Filled);
        sink.addRoundedRect({{shape.rect.left + dx, shape.rect.top + dy,
                              shape.rect.right + dx, shape.rect.bottom + dy},
                             shape.radiusX, shape.radiusY});
        sink.close();
        return geom;
    };

    // --- 1. Render inner shadows to a transparent colour image ---
    auto shadowRT = factory.createCpuRenderTarget({kW, kH}, kColorCpu);
    shadowRT.beginDraw();
    shadowRT.clear(Color{0.f, 0.f, 0.f, 0.f});
    {
        cachedBlur innerDark;
        innerDark.tint = Color{0.f, 0.f, 0.f, 0.5f};
        innerDark.draw(shadowRT, bounds, [&](Graphics& m) {
            auto darkGeom = makeHoleGeom(m, offset, offset);
            auto brush = m.createSolidColorBrush(Colors::White);
            m.fillGeometry(darkGeom, brush);
        });

        cachedBlur innerLight;
        innerLight.tint = Color{1.f, 1.f, 1.f, 0.8f};
        innerLight.draw(shadowRT, bounds, [&](Graphics& m) {
            auto lightGeom = makeHoleGeom(m, -offset, -offset);
            auto brush = m.createSolidColorBrush(Colors::White);
            m.fillGeometry(lightGeom, brush);
        });
    }
    shadowRT.endDraw();

    // --- 2. Render shape mask: 8bpp mono, white inside rounded rect ---
    auto maskRT = factory.createCpuRenderTarget({kW, kH}, kMask);
    maskRT.beginDraw();
    maskRT.clear(Color{0.f, 0.f, 0.f, 0.f});
    {
        auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
        maskRT.fillRoundedRectangle(shape, whiteBrush);
    }
    maskRT.endDraw();

    // --- 3. Apply mask to shadow image.
    auto shadowBmp = shadowRT.getBitmap();
    auto maskBmp   = maskRT.getBitmap();
    applyMask(shadowBmp, maskBmp);

    // --- 4. Draw checkerboard background then composite masked shadow ---
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    {
        const Color bgCream = colorFromHex(0xF6E7D7u);
        const Color bgBlue  = colorFromHex(0xD9EAF8u);
        constexpr float tile = 16.f;

        bigRT.clear(bgCream);
        auto checkerBrush = bigRT.createSolidColorBrush(bgBlue);
        for (float cy = 0.f; cy < static_cast<float>(kH); cy += tile)
            for (float cx = ((static_cast<int>(cy / tile)) & 1) ? tile : 0.f; cx < static_cast<float>(kW); cx += tile * 2.f)
                bigRT.fillRectangle({cx, cy, cx + tile, cy + tile}, checkerBrush);

        auto finalBmp = shadowRT.getBitmap();
        bigRT.drawBitmap(finalBmp, bounds, bounds);
    }
    bigRT.endDraw();

    EXPECT_TRUE(checkBitmap("blurNeumorphicDipCheckerboard", bigRT, 2, 4.0));
}

// Neumorphic "bump" over a checkerboard background.
// Outer drop shadows are rendered to a transparent image, masked to remove
// the area under the raised shape, then composited over the checkerboard.
TEST_F(DrawingTest, BlurNeumorphicBumpCheckerboard)
{
    constexpr uint32_t kW = 128, kH = 128;
    constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
                                | (int32_t)BitmapRenderTargetFlags::CpuReadable;
    constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto factory = g.getFactory();

    const RoundedRect shape{ {24.f, 24.f, 104.f, 104.f}, 16.f, 16.f };
    const gmpi::drawing::Rect bounds{ 0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH) };
    constexpr float offset = 4.f;

    // --- 1. Render outer drop shadows to a transparent colour image ---
    auto shadowRT = factory.createCpuRenderTarget({kW, kH}, kColorCpu);
    shadowRT.beginDraw();
    shadowRT.clear(Color{0.f, 0.f, 0.f, 0.f});
    {
        // Light shadow (top-left): shape shifted down-right so blur spills up-left.
        cachedBlur lightShadow;
        lightShadow.tint = Color{1.f, 1.f, 1.f, 0.7f};
        lightShadow.draw(shadowRT, bounds, [&](Graphics& m) {
            auto brush = m.createSolidColorBrush(Colors::White);
            RoundedRect shifted = shape;
            shifted.rect = offsetRect(shifted.rect, gmpi::drawing::Size{-offset, -offset});
            m.fillRoundedRectangle(shifted, brush);
        });

        // Dark shadow (bottom-right): shape shifted up-left so blur spills down-right.
        cachedBlur darkShadow;
        darkShadow.tint = Color{0.f, 0.f, 0.f, 0.5f};
        darkShadow.draw(shadowRT, bounds, [&](Graphics& m) {
            auto brush = m.createSolidColorBrush(Colors::White);
            RoundedRect shifted = shape;
            shifted.rect = offsetRect(shifted.rect, gmpi::drawing::Size{offset, offset});
            m.fillRoundedRectangle(shifted, brush);
        });
    }
    shadowRT.endDraw();

    // --- 2. Render inverted shape mask: 8bpp mono, white OUTSIDE the shape ---
    // Clear to transparent, then fill a hole geometry (full rect minus rounded rect)
    // so only the exterior is opaque. D2D source-over can't erase pixels, so we
    // draw the exterior shape rather than trying to punch a hole.
    auto maskRT = factory.createCpuRenderTarget({kW, kH}, kMask);
    maskRT.beginDraw();
    maskRT.clear(Color{0.f, 0.f, 0.f, 0.f}); // transparent everywhere
    {
        auto geom = maskRT.getFactory().createPathGeometry();
        auto sink = geom.open();
        sink.setFillMode(FillMode::Alternate);
        sink.addRect({0.f, 0.f, static_cast<float>(kW), static_cast<float>(kH)}, FigureBegin::Filled);
        sink.addRoundedRect(shape);
        sink.close();
        auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
        maskRT.fillGeometry(geom, whiteBrush);
    }
    maskRT.endDraw();

    // --- 3. Apply mask to shadow image.
    auto shadowBmp = shadowRT.getBitmap();
    auto maskBmp   = maskRT.getBitmap();
    applyMask(shadowBmp, maskBmp);

    // --- 4. Checkerboard background, then composite masked shadows ---
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    {
        const Color bgCream = colorFromHex(0xF6E7D7u);
        const Color bgBlue  = colorFromHex(0xD9EAF8u);
        constexpr float tile = 16.f;

        bigRT.clear(bgCream);
        auto checkerBrush = bigRT.createSolidColorBrush(bgBlue);
        for (float cy = 0.f; cy < static_cast<float>(kH); cy += tile)
            for (float cx = ((static_cast<int>(cy / tile)) & 1) ? tile : 0.f; cx < static_cast<float>(kW); cx += tile * 2.f)
                bigRT.fillRectangle({cx, cy, cx + tile, cy + tile}, checkerBrush);

        auto finalBmp = shadowRT.getBitmap();
        bigRT.drawBitmap(finalBmp, bounds, bounds);
    }
    bigRT.endDraw();

    EXPECT_TRUE(checkBitmap("blurNeumorphicBumpCheckerboard", bigRT, 2, 4.0));
}

TEST_F(DrawingTest, AdditiveBitmap)
{
    constexpr uint32_t kW = 128, kH = 128;
    constexpr uint32_t kBmpSize = 48; // size of the sprite

    auto factory = g.getFactory();

    // 1. Create a CPU-writable bitmap (default = 64bppPRGBAHalf).
    auto sprite = factory.createImage(kBmpSize, kBmpSize);

    // 2. Lock pixels for write and fill with an additive radial gradient.
    {
        auto pixels = sprite.lockPixels(BitmapLockFlags::Write);
        ASSERT_TRUE(pixels);

        uint8_t*      data = const_cast<uint8_t*>(pixels.getAddress());
        const int32_t bpr  = pixels.getBytesPerRow();
        const float   cx   = kBmpSize * 0.5f;
        const float   cy   = kBmpSize * 0.5f;
        const float   rad  = kBmpSize * 0.5f;

		constexpr float centerRadius = 4.f; // inner area of full brightness before radial falloff begins
        constexpr float chan1 = 1.0f; // red
        constexpr float chan2 = 0.8f; // green
        constexpr float chan3 = 0.2f; // blue

        constexpr float taperZoneStart = 0.5f; // linear taper at edge to hide squareness.
        constexpr float taperGradient = 1.0f / (1.0f - taperZoneStart); // linear taper at edge to hide squareness.

        for (uint32_t y = 0; y < kBmpSize; ++y)
        {
            uint16_t* row = reinterpret_cast<uint16_t*>(data + y * bpr);
            for (uint32_t x = 0; x < kBmpSize; ++x)
            {
                const float dx = (x + 0.5f) - cx;
                const float dy = (y + 0.5f) - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                float brightness = 1.0f / (std::max)(1.0f, 2.0f * (dist - centerRadius));

                // taper bightness at the edges to hid the squareness of the image.
				brightness *= (std::clamp)(1.f - (taperGradient * (dist - rad * taperZoneStart) / rad), 0.f, 1.f);

                // Premultiply and write as RGBA half-float.
                row[x * 4 + 0] = gmpi::drawing::detail::floatToHalf(chan1 * brightness);
                row[x * 4 + 1] = gmpi::drawing::detail::floatToHalf(chan2 * brightness);
                row[x * 4 + 2] = gmpi::drawing::detail::floatToHalf(chan3 * brightness);
                row[x * 4 + 3] = gmpi::drawing::detail::floatToHalf(0.0f); // additive premultiplied colors have alpha at zero to retain full brightness of background pixel.
            }
        }
    }

    // 3. Draw onto a larger render target: dark background + 5 overlapping stamps.
    auto bigRT = factory.createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Color{0.05f, 0.05f, 0.1f, 1.0f}); // dark blue-grey

    const gmpi::drawing::Rect srcRect{0.f, 0.f, float(kBmpSize), float(kBmpSize)};
    struct Pos { float x, y; };
    const Pos stamps[] = {
        {10.f,  40.f},
        {30.f,  20.f},
        {40.f,  20.f},
        {50.f,  64.f},
        {70.f,  30.f},
        {45.f,  65.f},
    };
    for (auto [sx, sy] : stamps)
    {
        const gmpi::drawing::Rect dst{sx, sy, sx + float(kBmpSize), sy + float(kBmpSize)};
        bigRT.drawBitmap(sprite, dst, srcRect);
    }

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("AdditiveBitmap", bigRT, 2, 4.0));
}
