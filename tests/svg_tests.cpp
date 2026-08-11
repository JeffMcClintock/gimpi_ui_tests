// SVG rendering tests (helpers/SvgParser.h).
//
// The headline case is tests/assets/Fade.svg — a real module panel from
// VCVRack's Fundamental plugin. It is deliberately not a toy: one document
// exercises gradients in <defs> referenced by url(#id), rounded rects,
// stroked lines and arcs, hundreds of filled glyph outlines built from
// relative cubics, and a display:none layer that must NOT be drawn.

#include "DrawingTestFixture.h"

#include "helpers/SvgParser.h"

namespace {

std::filesystem::path assetPath(const char* name)
{
    return std::filesystem::path(TEST_ASSETS_DIR) / name;
}

} // namespace

// ============================================================
// Fade.svg — the full document
// ============================================================

// Render the panel at its intrinsic 45x380 size.
TEST_F(DrawingTest, SvgFadePanel)
{
    auto rt = drawingContext.factory().createCpuRenderTarget({45, 380}, kRenderFlags);
    rt.beginDraw();
    rt.clear(Colors::White);

    const auto size = SvgParser::draw(rt, assetPath("Fade.svg").string());

    rt.endDraw();

    EXPECT_EQ(size.width,  45.f);
    EXPECT_EQ(size.height, 380.f);
    EXPECT_TRUE(checkBitmap("svgFadePanel", rt, 1));
}

// The "components" layer carries style="display:none" and holds seven
// saturated red/green/blue circles marking knob and port positions. If any
// of them renders, the panel is wrong in a way that is obvious to the eye
// but easy for a parser to miss — so assert it directly rather than relying
// on the reference image.
TEST_F(DrawingTest, SvgDisplayNoneLayerIsNotDrawn)
{
    // 8-bit sRGB so the pixels can be read as bytes without decoding half-floats.
    constexpr int32_t kSRGB = (int32_t)BitmapRenderTargetFlags::SRGBPixels
                            | (int32_t)BitmapRenderTargetFlags::CpuReadable;

    auto rt = drawingContext.factory().createCpuRenderTarget({45, 380}, kSRGB);
    rt.beginDraw();
    rt.clear(Colors::White);
    SvgParser::draw(rt, assetPath("Fade.svg").string());
    rt.endDraw();

    auto bitmap = rt.getBitmap();
    auto pixels = bitmap.lockPixels(BitmapLockFlags::Read);
    ASSERT_EQ(pixels.getBytesPerPixel(), 4);

    const int32_t layout = pixels.channelLayout();
    const int iR = (layout == 0) ? 2 : 0;
    const int iB = (layout == 0) ? 0 : 2;

    // The marker circles sit at x=22.5 with r=5, centred on these y values.
    const float markerY[] = {73.f, 109.44f, 156.46f, 199.4f, 244.29f, 289.14f, 334.f};

    for (float y : markerY)
    {
        const uint8_t* p = pixels.getAddress()
                         + static_cast<int32_t>(y) * pixels.getBytesPerRow() + 22 * 4;
        const int r = p[iR], g = p[1], b = p[iB];

        // A hidden marker leaves whatever the visible artwork draws there:
        // greys, near-greys, or white. A drawn marker is fully saturated.
        EXPECT_LT(std::max({r, g, b}) - std::min({r, g, b}), 40)
            << "saturated pixel at y=" << y << " (" << r << "," << g << "," << b
            << ") — the display:none 'components' layer was drawn";
    }
}

// ============================================================
// Feature-level tests
// ============================================================
//
// Each of these isolates one thing Fade.svg depends on, so a failure in the
// panel test can be traced to a cause instead of eyeballed.
//
// These were all cross-checked against headless Chrome rendering the same
// markup. Shapes, arcs, smooth curves, stroke styles, fill rules, transforms
// and colour spellings agree with it to antialiasing noise (mean per-pixel
// difference 0.0-3.6 / 255).
//
// Gradients and opacity deliberately do NOT agree, and the reason is not in
// this parser: gmpi composites and interpolates in linear light, whereas
// SVG and CSS mandate sRGB space. The endpoints match Chrome exactly; only
// the ramp between them differs.
//
//     black -> white gradient, midpoint      Chrome 131    here 190
//     #c00000 at 50% over white              Chrome 223,127,127
//                                            here   226,188,188
//
// 190 is sRGB_encode(0.5) and (226,188,188) is the linear-light blend, so
// both are exactly what a linear-light renderer should produce. This is the
// same framework-wide policy that DrawingTest.LinearGradientFill and
// DrawingTest.AlphaEquivalentGrey already absorb with widened tolerances.
// Changing it is a decision about the drawing API's colour space, not about
// SVG parsing — so these tests pin OUR output rather than Chrome's.

// Helper: render a small inline SVG document into the fixture's 64x64 target.
namespace {

void drawSvg(gmpi::drawing::Graphics& g, const char* xml)
{
    SvgParser::drawFromMemory(g, xml);
}

} // namespace

// A <defs> linear gradient referenced by fill="url(#id)", in userSpaceOnUse.
TEST_F(DrawingTest, SvgLinearGradientFill)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <defs>
        <linearGradient id="grad" x1="0" y1="0" x2="64" y2="0" gradientUnits="userSpaceOnUse">
          <stop offset="0" stop-color="#ff0000"/>
          <stop offset="1" stop-color="#0000ff"/>
        </linearGradient>
      </defs>
      <rect width="64" height="64" fill="url(#grad)"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgLinearGradientFill", 0, 20.0));
}

// The default gradientUnits is objectBoundingBox: the gradient must span the
// shape's own box, not the canvas.
TEST_F(DrawingTest, SvgGradientObjectBoundingBox)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <linearGradient id="g">
        <stop offset="0" stop-color="#000000"/>
        <stop offset="1" stop-color="#ffffff"/>
      </linearGradient>
      <rect x="16" y="16" width="32" height="32" fill="url(#g)"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgGradientObjectBoundingBox", 0, 20.0));
}

// Elliptical arcs — 'A' and 'a', both flag combinations.
TEST_F(DrawingTest, SvgPathArcs)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <path d="M8,32 A20,12 0 0 1 56,32" fill="none" stroke="#c00000" stroke-width="3"/>
      <path d="M8,44 a20,12 0 1 0 48,0" fill="none" stroke="#0000c0" stroke-width="3"/>
      <path d="M8,20 A12,12 0 0 0 32,20 Z" fill="#008000"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgPathArcs", 0, 12.0));
}

// Smooth curve shorthands: S/s reflect the previous cubic control point,
// T/t the previous quadratic one.
TEST_F(DrawingTest, SvgPathSmoothCurves)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <path d="M4,20 C12,4 20,4 28,20 S44,36 60,20" fill="none" stroke="#202020" stroke-width="3"/>
      <path d="M4,48 Q16,32 28,48 T52,48" fill="none" stroke="#a00060" stroke-width="3"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgPathSmoothCurves", 0, 12.0));
}

// The primitive shape elements, each of which SvgParser used to ignore.
TEST_F(DrawingTest, SvgBasicShapes)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <rect x="2" y="2" width="26" height="18" rx="6" fill="#3070b0"/>
      <circle cx="46" cy="12" r="10" fill="#b03030"/>
      <ellipse cx="16" cy="40" rx="13" ry="8" fill="#309050"/>
      <line x1="34" y1="30" x2="62" y2="50" stroke="#000000" stroke-width="3"/>
      <polyline points="34,56 42,48 50,58 60,52" fill="none" stroke="#804000" stroke-width="2"/>
      <polygon points="4,62 14,52 24,62" fill="#6040a0"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgBasicShapes", 0, 12.0));
}

// Stroke presentation attributes, and the style="" attribute overriding them.
TEST_F(DrawingTest, SvgStrokeStyles)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <path d="M8,12 L32,4 L56,12" fill="none" stroke="#101010" stroke-width="7" stroke-linejoin="round" stroke-linecap="round"/>
      <path d="M8,32 L32,24 L56,32" fill="none" stroke="#101010" stroke-width="7" stroke-linejoin="bevel"/>
      <path d="M8,52 L32,44 L56,52" fill="none" stroke="#ff0000" stroke-width="1"
            style="stroke:#101010; stroke-width:7; stroke-linejoin:miter"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgStrokeStyles", 0, 12.0));
}

// Nested transforms compose, and rotate() takes DEGREES (the old
// parseTransform passed the value straight to makeRotation, which is radians).
TEST_F(DrawingTest, SvgNestedTransforms)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <g transform="translate(32,32)">
        <g transform="rotate(45)">
          <rect x="-16" y="-16" width="32" height="32" fill="#2060a0"/>
        </g>
        <rect x="-3" y="-3" width="6" height="6" fill="#ffd000"/>
      </g>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgNestedTransforms", 0, 12.0));
}

// Fill rules: the same self-intersecting star under nonzero and evenodd.
TEST_F(DrawingTest, SvgFillRule)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <path d="M16,4 L26,28 L4,13 L28,13 L6,28 Z" fill="#204080" fill-rule="nonzero"/>
      <path d="M48,36 L58,60 L36,45 L60,45 L38,60 Z" fill="#804020" fill-rule="evenodd"/>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgFillRule", 0, 12.0));
}

// Colour spellings: named, #rgb shorthand, #rrggbb, and rgb().
TEST_F(DrawingTest, SvgColorFormats)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <rect x="0"  y="0" width="16" height="64" fill="steelblue"/>
      <rect x="16" y="0" width="16" height="64" fill="#4682B4"/>
      <rect x="32" y="0" width="16" height="64" fill="rgb(70,130,180)"/>
      <rect x="48" y="0" width="16" height="64" fill="#48b"/>
    </svg>)SVG");

    // All four columns name the same colour except the last, whose #48b
    // shorthand expands to #4488bb.
    EXPECT_TRUE(checkResult("svgColorFormats"));
}

// ============================================================
// Geometry-only interface
// ============================================================

// parseToGeometry() flattens a document into one sink, discarding paint. It is
// what SynthEditLib's SvgGeometry module calls, so it has to keep working
// independently of the renderer above.
TEST_F(DrawingTest, SvgParseToGeometry)
{
    auto rt = drawingContext.factory().createCpuRenderTarget({45, 380}, kRenderFlags);
    rt.beginDraw();
    rt.clear(Colors::White);

    auto geometry = rt.getFactory().createPathGeometry();
    auto sink = geometry.open();

    const auto size = SvgParser::parseToGeometry(
        assetPath("Fade.svg").string(), gmpi::drawing::AccessPtr::get(sink));

    EXPECT_EQ(size.width,  45.f);
    EXPECT_EQ(size.height, 380.f);

    // Stroke the flattened outline at 1:1 — every shape in the document as a
    // single hairline path, which is what the SvgGeometry module wants.
    auto brush = rt.createSolidColorBrush(Colors::Black);
    rt.drawGeometry(geometry, brush, 0.5f);

    rt.endDraw();
    EXPECT_TRUE(checkBitmap("svgParseToGeometry", rt, 0, 12.0));
}

// fill-opacity, stroke-opacity and the group opacity multiplier.
TEST_F(DrawingTest, SvgOpacity)
{
    drawSvg(g, R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
      <rect width="64" height="64" fill="#ffffff"/>
      <rect x="4"  y="4"  width="24" height="24" fill="#c00000" fill-opacity="0.5"/>
      <rect x="36" y="4"  width="24" height="24" fill="none" stroke="#0000c0" stroke-width="6" stroke-opacity="0.5"/>
      <g opacity="0.5">
        <rect x="4" y="36" width="24" height="24" fill="#008000"/>
      </g>
    </svg>)SVG");

    EXPECT_TRUE(checkResult("svgOpacity", 0, 20.0));
}
