// GMPI-UI Text Drawing Tests
// Tests for text rendering, alignment, wrapping, clipping, font metrics,
// and multiline line-height behaviour.

#include "DrawingTestFixture.h"

// ============================================================
// Text drawing tests
// ============================================================

// Single short string, left-aligned, black on white.
TEST_F(DrawingTest, DrawTextSimple)
{
    auto tf    = makeTextFormat(12.f);
    g.drawTextU("Hello", tf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("drawTextSimple", 2));
}

// Text centred horizontally and vertically in the bitmap.
TEST_F(DrawingTest, DrawTextCentred)
{
    auto tf = makeTextFormat(12.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Hi", tf, {0.f, 0.f, 64.f, 64.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextCentred", 2));
}

// Bold weight text.
TEST_F(DrawingTest, DrawTextBold)
{
    auto tf    = makeTextFormat(12.f, "Arial", FontWeight::Bold);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Bold", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextBold", 2));
}

// Larger font size to stress-test glyph outlines.
TEST_F(DrawingTest, DrawTextLarge)
{
    auto tf    = makeTextFormat(24.f);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Ag", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextLarge", 2));
}

// Two lines of text — verifies line spacing.
TEST_F(DrawingTest, DrawTextMultiLine)
{
    auto tf = makeTextFormat(10.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Line one\nLine two", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextMultiLine", 40, 50.0)); // inconsistant smoothing, on same windows system
}

// Coloured text on a coloured background.
TEST_F(DrawingTest, DrawTextColoured)
{
    auto bgBrush   = g.createSolidColorBrush(Colors::DarkBlue);
    auto textBrush = g.createSolidColorBrush(Colors::Yellow);
    g.fillRectangle({0.f, 0.f, 64.f, 64.f}, bgBrush);
    auto tf = makeTextFormat(14.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    g.drawTextU("Test", tf, {0.f, 0.f, 64.f, 64.f}, textBrush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextColoured", 2, 20.0));
}

// An empty string should not affect the bitmap (remains all-white).
TEST_F(DrawingTest, EmptyStringText)
{
    auto tf    = makeTextFormat(12.f);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("emptyStringText", 2));
}

// ============================================================
// Text clipping and wrapping
// ============================================================

// Text clipped by pushAxisAlignedClip — top half of glyphs only.
TEST_F(DrawingTest, TextClippedByClipRect)
{
    auto tf    = makeTextFormat(24.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Only the top half of the bitmap is visible.
    g.pushAxisAlignedClip({0.f, 0.f, 64.f, 32.f});
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush, kTextOptions);
    g.popAxisAlignedClip();
    EXPECT_TRUE(checkResult("textClippedByClipRect", 2));
}

// Without DrawTextOptions::Clip, text overflows the layout rect (D2D default).
TEST_F(DrawingTest, TextOverflowsLayoutRect)
{
    auto tf    = makeTextFormat(14.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Layout rect is narrow — text overflows on the right (no clip flag).
    g.drawTextU("Overflow Right Edge", tf, {2.f, 22.f, 40.f, 42.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("textOverflowsLayoutRect", 34, 15.0));
}

// With DrawTextOptions::Clip, text is clipped to the layout rect.
TEST_F(DrawingTest, TextClippedByLayoutRect)
{
    auto tf    = makeTextFormat(14.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Same narrow rect, but Clip flag is set — text should be cut off.
    g.drawTextU("ClipMe Right Edge", tf, {2.f, 22.f, 40.f, 42.f}, brush, DrawTextOptions::Clip | kTextOptions);
    EXPECT_TRUE(checkResult("textClippedByLayoutRect", 34, 15.0));
}

// Word-wrap ON: a long string breaks across multiple lines within the layout rect.
TEST_F(DrawingTest, TextWrapOn)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("textWrapOn", 34, 25.0));
}

// Word-wrap OFF: same long string runs in one line and is clipped on the right.
TEST_F(DrawingTest, TextWrapOff)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("textWrapOff", 2, 8.0));
}

// Text clipped at the bottom of the layout rect — last line is cut off.
TEST_F(DrawingTest, TextClippedAtBottom)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Layout rect is only tall enough for 2 of the 4 lines.
    g.drawTextU("Line one\nLine two\nLine three\nLine four",
                 tf, {2.f, 2.f, 62.f, 28.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("textClippedAtBottom", 10, 35.0));
}

// Bounds height of only 1 pixel — text should still attempt to draw.
TEST_F(DrawingTest, DrawTextWhenBoundsTooShort)
{
    auto tf    = makeTextFormat(12.f);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Layout rect spans full width but is only 1 pixel tall.
    g.drawTextU("small bounds", tf, {0.f, 32.f, 64.f, 33.f}, brush, kTextOptions);
    EXPECT_TRUE(checkResult("drawTextWhenBoundsTooShort", 2));
}

// ============================================================
// Font metrics visualisation  (256 × 120 render target)
// ============================================================
//
// Draws "Hfgx" in large text and overlays coloured horizontal rules at each
// metric returned by TextFormat::getFontMetrics():
//
//   Blue   — ascender line   (top of em box, ascent above baseline)
//   Green  — cap-height line (top of capital letters like H)
//   Orange — x-height line   (top of lowercase letters like x)
//   Red    — baseline
//   Purple — descender line  (bottom of em box, descent below baseline)
//
// The underline and strikethrough positions are drawn as thin dashed cyan
// and magenta lines respectively so they can also be verified.
TEST_F(DrawingTest, FontMetricsVisual)
{
    constexpr uint32_t kW = 256, kH = 120;

    // Create a dedicated render target for this test.
    auto bigRT = drawingContext.factory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    // ---- font & metrics ----
    constexpr float kFontHeight = 60.f;   // body height (ascent + descent)
    constexpr float kBaselineY  = 86.f;   // y we want the text baseline to land on

    auto tf = makeTextFormat(kFontHeight);
    tf.setWordWrapping(WordWrapping::NoWrap);

    FontMetrics fm = tf.getFontMetrics();

    const float ascenderY    = kBaselineY - fm.ascent;
    const float capY         = kBaselineY - fm.capHeight;
    const float xhY          = kBaselineY - fm.xHeight;
    const float descenderY   = kBaselineY + fm.descent;
    const float underlineY   = kBaselineY - fm.underlinePosition;
    const float strikeY      = kBaselineY - fm.strikethroughPosition;

    // ---- metric lines (drawn first, behind the glyphs) ----
    auto brushAsc    = bigRT.createSolidColorBrush(colorFromHex(0x1565C0u));        // blue
    auto brushCap    = bigRT.createSolidColorBrush(colorFromHex(0x2E7D32u));        // green
    auto brushXH     = bigRT.createSolidColorBrush(colorFromHex(0xE65100u));        // orange
    auto brushBase   = bigRT.createSolidColorBrush(Colors::Red);
    auto brushDesc   = bigRT.createSolidColorBrush(colorFromHex(0x6A1B9Au));        // purple
    auto brushUL     = bigRT.createSolidColorBrush(colorFromHex(0x00838Fu, 0.7f));  // cyan
    auto brushST     = bigRT.createSolidColorBrush(colorFromHex(0xAD1457u, 0.7f));  // magenta

    const float W = static_cast<float>(kW);
    const float labelLeft = W * 0.75f;
    const float lineRight = labelLeft - 3;

    bigRT.drawLine({0.f, ascenderY},  {lineRight, ascenderY},  brushAsc,  1.f);
    bigRT.drawLine({0.f, capY},       {lineRight, capY},        brushCap,  1.f);
    bigRT.drawLine({0.f, xhY},        {lineRight, xhY},         brushXH,   1.f);
    bigRT.drawLine({0.f, kBaselineY}, {lineRight, kBaselineY},  brushBase, 1.f);
    bigRT.drawLine({0.f, descenderY}, {lineRight, descenderY},  brushDesc, 1.f);

    // Underline and strikethrough as thicker dashed strokes.
    StrokeStyleProperties dashProps;
    dashProps.dashStyle = DashStyle::Dash;
    auto dashStyle = bigRT.getFactory().createStrokeStyle(dashProps);
    bigRT.drawLine({0.f, underlineY}, {lineRight, underlineY}, brushUL, 2.f, dashStyle);
    bigRT.drawLine({0.f, strikeY},    {lineRight, strikeY},    brushST, 2.f, dashStyle);

    // ---- glyphs: black text drawn on top of the lines ----
    auto textBrush = bigRT.createSolidColorBrush(Colors::Black);
    // Position layout rect so text baseline lands exactly at kBaselineY.
    const gmpi::drawing::Rect layoutRect{4.f, ascenderY, W, descenderY};
    bigRT.drawTextU("Hfgx", tf, layoutRect, textBrush, kTextOptions);

    // ---- labels (small Arial, right-aligned, matching line colour) ----
    auto labelTF = makeTextFormat(9.f);
    labelTF.setTextAlignment(TextAlignment::Leading);
    FontMetrics labelFm = labelTF.getFontMetrics();
    const float labelW = 80.f;
    const float labelX = labelLeft; // W - labelW;
    auto labelY = [&](float lineY) { return lineY - labelFm.capHeight - 1.f; };

    bigRT.drawTextU("ascender",    labelTF, {labelX, labelY(ascenderY),  W, ascenderY},  brushAsc, kTextOptions);
    bigRT.drawTextU("cap-height",  labelTF, {labelX, labelY(capY),       W, capY},       brushCap, kTextOptions);
    bigRT.drawTextU("x-height",    labelTF, {labelX, labelY(xhY),        W, xhY},        brushXH, kTextOptions);
    bigRT.drawTextU("baseline",    labelTF, {labelX, labelY(kBaselineY), W, kBaselineY}, brushBase, kTextOptions);
    bigRT.drawTextU("descender",   labelTF, {labelX, descenderY + 1.f,   W, descenderY + labelFm.ascent + 1.f}, brushDesc, kTextOptions);
    bigRT.drawTextU("underline",   labelTF, {labelX, labelY(underlineY), W, underlineY}, brushUL, kTextOptions);
    bigRT.drawTextU("strikethrough",labelTF,{labelX, labelY(strikeY),    W, strikeY},    brushST, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("fontMetricsVisual", bigRT, 12, 12.0));
}

// ============================================================
// Multiline text: line height and paragraph alignment
// ============================================================
// Uses doubled text height (24px) and larger bitmaps to reduce
// anti-aliasing noise when comparing Windows vs macOS rendering.

static constexpr const char* kMultilineText = "Line one\nLine two\nLine three";

// --- Default line height, generous rect ---

TEST_F(DrawingTest, MultilineDefaultSpacing)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineDefaultSpacing", bigRT, 2));
}

// --- Explicit line heights: tight, default-ish, loose, extra loose ---

TEST_F(DrawingTest, MultilineLineHeight20)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(20.f, 16.f);   // tighter than default
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineLineHeight20", bigRT, 2));
}

TEST_F(DrawingTest, MultilineLineHeight30)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(30.f, 24.f);   // roughly default
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineLineHeight30", bigRT, 2));
}

TEST_F(DrawingTest, MultilineLineHeight40)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(40.f, 32.f);   // generous
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineLineHeight40", bigRT, 2));
}

TEST_F(DrawingTest, MultilineLineHeight60)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(60.f, 48.f);   // extra loose
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineLineHeight60", bigRT, 2));
}

// --- Paragraph alignment (vertical) with default line height ---

TEST_F(DrawingTest, MultilineParagraphAlignNear)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setParagraphAlignment(ParagraphAlignment::Near);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineParagraphNear", bigRT, 2));
}

TEST_F(DrawingTest, MultilineParagraphAlignCenter)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineParagraphCenter", bigRT, 2));
}

TEST_F(DrawingTest, MultilineParagraphAlignFar)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setParagraphAlignment(ParagraphAlignment::Far);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineParagraphFar", bigRT, 2));
}

// --- Paragraph alignment combined with explicit line height ---

TEST_F(DrawingTest, MultilineParagraphCenterLineHeight40)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(40.f, 32.f);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineParagraphCenterLH40", bigRT, 2));
}

TEST_F(DrawingTest, MultilineParagraphFarLineHeight40)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(40.f, 32.f);
    tf.setParagraphAlignment(ParagraphAlignment::Far);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineParagraphFarLH40", bigRT, 2));
}

// --- Tight bounding rect: not enough height for all lines ---

TEST_F(DrawingTest, MultilineTightRectDefaultSpacing)
{
    constexpr uint32_t kW = 256, kH = 128;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    // Only ~50px tall — not enough for 3 lines at 24px body height.
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineTightDefault", bigRT, 2));
}

TEST_F(DrawingTest, MultilineTightRectLineHeight40)
{
    constexpr uint32_t kW = 256, kH = 128;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setLineSpacing(40.f, 32.f);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    // Only ~76px tall — not enough for 3 lines at 40px line height.
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 80.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineTightLH40", bigRT, 2));
}

// --- Tight rect with paragraph alignment ---

TEST_F(DrawingTest, MultilineTightParagraphCenter)
{
    constexpr uint32_t kW = 256, kH = 128;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineTightParagraphCenter", bigRT, 2));
}

TEST_F(DrawingTest, MultilineTightParagraphFar)
{
    constexpr uint32_t kW = 256, kH = 128;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setParagraphAlignment(ParagraphAlignment::Far);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineTightParagraphFar", bigRT, 2));
}

// --- Narrow rect forcing word wrap ---

TEST_F(DrawingTest, MultilineNarrowWrap)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    // Narrow width forces wrapping within each explicit line.
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 100.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineNarrowWrap", bigRT, 2));
}

TEST_F(DrawingTest, MultilineNarrowWrapLineHeight40)
{
    constexpr uint32_t kW = 256, kH = 256;
    auto bigRT = g.getFactory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    auto tf = makeTextFormat(24.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    tf.setLineSpacing(40.f, 32.f);
    auto brush = bigRT.createSolidColorBrush(Colors::Black);
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 100.f, 252.f}, brush, kTextOptions);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineNarrowWrapLH40", bigRT, 2));
}

// ============================================================
// Text baseline alignment (commented out — superseded by TextBaselineLowestPixel)
// ============================================================
#if 0
TEST_F(DrawingTest, TextBaselines)
{
    constexpr uint32_t kW = 512, kH = 512;

    auto predictBaseLine = [](float bodyHeight, float y) {
        // Predict baseline position based on body height and vertical offset.
        // This is a heuristic that matches the observed baseline positions on Windows.
        // The offset is added to account for sub-pixel positioning.
        const float offset = -0.25f;
        const float scale = 0.5f;
        return std::floor((y + offset) / scale) * scale;
		};

    auto bigRT = drawingContext.factory().createCpuRenderTarget({kW, kH}, kRenderFlags);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    const auto str = "L";
    auto brush = bigRT.createSolidColorBrush(Colors::White);

    constexpr float left = 16.f;
    constexpr float top = 16.f;
    constexpr float yinc = 0.01f;

    float lineStartY = top;
    for (float bodyHeight = 6.0f; bodyHeight < 24.0f; bodyHeight += 0.5f)
    {
        float x = left;
        float y = lineStartY;

        auto tf = makeTextFormat(bodyHeight);
        tf.setWordWrapping(WordWrapping::NoWrap);

        auto textSize = tf.getTextExtentU(str);
		tf.setParagraphAlignment(ParagraphAlignment::Top);
        FontMetrics fm = tf.getFontMetrics();

        for (int i = 0; i < 100; ++i)
        {
            // clip the letter so only the bottom of the L draws
			Rect clipRect{ x - 1, y - 2, x + 1, y + 4 };
			bigRT.pushAxisAlignedClip(clipRect);

            // allow extra room at bottom to prevent edge affecting alignment
            const auto left = std::round(x - textSize.width * 0.5f);
            auto right = std::ceil(left + textSize.width);
            Rect textRect{left, y - fm.ascent, right, y + fm.descent + 10.f};

            brush.setColor(Colors::Black);
            bigRT.drawTextU(str, tf, textRect, brush, DrawTextOptions::NoSnap | kTextOptions);

			bigRT.popAxisAlignedClip();

            // Draw the glyph.
            Rect snappedRect = textRect;

            // Green tick at the predicted baseline.
			const auto predictedBaseLine = predictBaseLine(bodyHeight, y);
            brush.setColor(Colors::Lime);
            bigRT.drawLine({x + 1.0f, predictedBaseLine + 0.25f},
                           {x + 3.0f, predictedBaseLine + 0.25f}, brush, 0.5f);

            x += 5.f;
            y += yinc;
        }

        lineStartY += 6.0f;
    }

    bigRT.endDraw();

    {
        _RPT0(0, "\n----------------------------------\nBody Height, baseline, error\n");


        // Lock pixels and printout baselines.
        auto bmp = bigRT.getBitmap();
        auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
        ASSERT_TRUE(pixels);

        const uint8_t* data = pixels.getAddress();
        const int32_t  bpr = pixels.getBytesPerRow();
        const auto     size = bmp.getSize();

// 8-bytes per pixel (float or int depending on platform), easier to just render onto a sRGB8 bitmap.
// even better a 1 pixel wide 30 pixel high bitmap.
		const auto bytesPerPixel = bpr / size.width; // assumes stride aint too big.

        float lineStartY = top;
        for(float bodyHeight = 6.0f; bodyHeight < 24.0f; bodyHeight += 0.5f)
        {
            int x = left;
            float y = lineStartY;

            for(int i = 0; i < 100; ++i)
            {
                Rect clipRect{ x - 1, y - 2, x + 1, y + 4 };

                auto scanCenterY = static_cast<int>(y);
                int actualBaseline = scanCenterY - 99; // total failure prints -99
                for(int yoff = -2; yoff < 3; ++yoff)
                {
					const int scanY = scanCenterY + yoff;
                    const uint8_t* pixel = data + scanY * bpr + x * 4;
                    if(pixel[2] > 4)
                    {
                        actualBaseline = scanY;
                        break;
                    }
                }

                const auto predictedBaseLine = predictBaseLine(bodyHeight, y);
                _RPTN(0, "%.1f, %.2f %.2f\n", bodyHeight, y, predictedBaseLine - actualBaseline);

                x += 5;
                y += yinc;
            }

            lineStartY += 8.0f;
        }
    }

    EXPECT_TRUE(checkBitmap("textBaselines", bigRT, 2, 15.0));
}

TEST_F(DrawingTest, TextBaselines200)
{
    constexpr uint32_t kW = 1024, kH = 1024;
    constexpr float kDpi = 192.0f;

    auto bigRT = drawingContext.factory().createCpuRenderTarget({kW, kH}, kRenderFlags, kDpi);
    bigRT.beginDraw();
    bigRT.clear(Colors::White);

    const auto str = "E";
    auto brush = bigRT.createSolidColorBrush(Colors::White);

    constexpr float startx = 20.f;
    constexpr float starty = 20.f;
    constexpr float yinc = 0.01f;

	float lineStartY = starty;
    for (float bodyHeight = 6.0f; bodyHeight < 24.0f; bodyHeight += 0.5f)
    {
        auto tf = makeTextFormat(bodyHeight);
        tf.setWordWrapping(WordWrapping::NoWrap);

        auto textSize = tf.getTextExtentU(str);
        FontMetrics fm = tf.getFontMetrics();

        float x = startx;
        float y = lineStartY;

        for (int i = 0; i < 100; ++i)
        {
            Rect textRect{x, y, std::ceil(x + textSize.width), y + textSize.height};

            // Light-grey tick at the row's y origin.
            brush.setColor(Colors::LightGray);
            bigRT.drawLine({x + 1.f, y}, {x + 3.f, y}, brush, 0.5f);

            // Draw the glyph.
            brush.setColor(Colors::Black);
            Rect snappedRect = textRect;

            if ((i % 10) == 9)
                bigRT.drawTextU("L", tf, snappedRect, brush, DrawTextOptions::NoSnap | kTextOptions);
            else
                bigRT.drawTextU(str, tf, snappedRect, brush, DrawTextOptions::NoSnap | kTextOptions);

            // Green tick at the predicted baseline.
            float predictedBaseLine = textRect.top + fm.ascent;
            const float offset = -0.25f;
            predictedBaseLine += offset;
            const float scale = 0.5f;
            predictedBaseLine = std::floor(predictedBaseLine / scale) * scale;

            brush.setColor(Colors::Lime);
            bigRT.drawLine({x, predictedBaseLine + 0.25f},
                           {x + 1.f, predictedBaseLine + 0.25f}, brush, 0.5f);

            x += 4.f;
            y += yinc;
        }

        lineStartY += 8.0f; // std::floor(bodyHeight * 0.8f) + 2.0f;
    }

    bigRT.endDraw();

    {
        // Lock pixels and printout baselines.
        auto bmp = bigRT.getBitmap();
        auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
        ASSERT_TRUE(pixels);

        const uint8_t* data = pixels.getAddress();
        const int32_t  bpr = pixels.getBytesPerRow();
        const auto     size = bmp.getSize();

        float lineStartY = starty;
        for(float bodyHeight = 6.0f; bodyHeight < 24.0f; bodyHeight += 0.5f)
        {
            float x = startx;
            float y = lineStartY;

            for(int i = 0; i < 100; ++i)
            {

            }

            lineStartY += 8.0f; // std::floor(bodyHeight * 0.8f) + 2.0f;
        }
    }
    EXPECT_TRUE(checkBitmap("textBaselines200", bigRT, 2, 15.0));
}
#endif

// ============================================================
// Baseline analysis: lowest lit pixel of "L"
// ============================================================
//
// For each combination of font height (6–23.5 in 0.5 steps) and sub-pixel
// y-offset (0.0–0.9 in 0.1 steps), renders "L" on a 1×32 sRGB bitmap
// with its text baseline at y = 16 + sub-pixel offset.  Scans downward
// to find the lowest (largest y) non-white pixel, and prints a table
// comparing the measured value against the predicted baseline pixel.

// Helper: runs the lowest-lit-pixel scan at a given DPI and prints
// a results table.  Returns the number of prediction errors.
static int runBaselineLowestPixelTest(DrawingTestContext& drawingContext,
    std::function<TextFormat(float)> makeTextFormat,
    float dpi,
    const char* fontName = "Arial")
{
    const float dpiScale = dpi / 96.0f;

    // Bitmap dimensions in physical pixels; DIP coordinate space is kW/kH.
    constexpr int kW = 1;
    constexpr int kH = 32;
    const int pixW = static_cast<int>(kW * dpiScale);
    const int pixH = static_cast<int>(kH * dpiScale);
    constexpr float kBaselineCenter = 16.0f; // in DIPs

    const int32_t srgbFlags = static_cast<int32_t>(BitmapRenderTargetFlags::SRGBPixels);
    auto rt = drawingContext.factory().createCpuRenderTarget(
        {static_cast<uint32_t>(pixW), static_cast<uint32_t>(pixH)}, srgbFlags, dpi);

    struct Row {
        float fontHeight;
        float subPixel;
        int   lowestLitY;        // measured pixel row (-1 = none)
        int   predictedPixel;    // expected pixel row
    };
    std::vector<Row> table;

    for (float fontHeight = 6.0f; fontHeight < 24.0f; fontHeight += 0.5f)
    {
        auto tf = makeTextFormat(fontHeight);
        tf.setTextAlignment(TextAlignment::Center);
        tf.setWordWrapping(WordWrapping::NoWrap);

        FontMetrics fm = tf.getFontMetrics();

        // Helper lambda: render "L" at a given sub-pixel offset and scan
        // for the lowest non-white pixel row.
        auto measureLowest = [&](float subPx) -> int {
            const float bl     = kBaselineCenter + subPx;
            const float t      = bl - fm.ascent;
            const float b      = t + fm.ascent + fm.descent;

            rt.beginDraw();
            rt.clear(Colors::White);

            auto brush = rt.createSolidColorBrush(Colors::Black);
            gmpi::drawing::Rect layoutRect{0.f, t, static_cast<float>(kW), b};
            rt.drawTextU("L", tf, layoutRect, brush, DrawTextOptions::NoSnap | kTextOptions);
            rt.endDraw();

            auto bmp    = rt.getBitmap();
            auto pixels = bmp.lockPixels(BitmapLockFlags::Read);
            const uint8_t* addr = pixels.getAddress();
            const int32_t  bpr  = pixels.getBytesPerRow();

            int lowest = -1;
            for (int y = 0; y < pixH; ++y)
            {
                const uint8_t* p = addr + y * bpr;
                if (p[0] != 255 || p[1] != 255 || p[2] != 255)
                    lowest = y;
            }
            return lowest;
        };

#ifndef _WIN32
        // macOS calibration: CoreText rasterisation places the glyph
        // bottom at a font-size-dependent sub-pixel offset that cannot
        // be derived from metrics alone.  We binary-search for the DIP
        // offset at which the lowest lit pixel first advances, giving
        // us B0 (the pixel position of the glyph bottom at subPx=0)
        // to high precision (~0.01 pixel).
        const int cal0 = measureLowest(0.0f);
        const int calMax = measureLowest(0.9f);

        float B0;
        if (calMax == cal0)
        {
            // No pixel transition in [0, 0.9] DIPs → frac(B0) is tiny.
            B0 = static_cast<float>(cal0) + 0.02f;
        }
        else
        {
            // Binary search for the DIP offset where the pixel first changes.
            float lo = 0.0f, hi = 0.9f;
            for (int i = 0; i < 10; ++i)
            {
                const float mid = (lo + hi) * 0.5f;
                if (measureLowest(mid) > cal0)
                    hi = mid;
                else
                    lo = mid;
            }
            // At the transition offset T, B0 + T ≈ cal0 + 1.
            const float T = (lo + hi) * 0.5f;
            B0 = static_cast<float>(cal0 + 1) - T;
        }
#endif

        for (int si = 0; si < 10; ++si)
        {
            const float subPx    = si * 0.1f;
            const int   lowestY  = measureLowest(subPx);

#ifdef _WIN32
            // Predict the lowest lit pixel row.
            // D2D snaps the text baseline to the nearest half-DIP, then
            // the lowest lit pixel is one above the snapped value in
            // physical pixels: ceil(snappedPx) - 1.
            const float baseline  = kBaselineCenter + subPx;
            const float snappedDIP = std::round(baseline * 2.0f) / 2.0f;
            const float snappedPx  = snappedDIP * dpiScale;
            const int   predicted  = static_cast<int>(std::ceil(snappedPx)) - 1;
#else
            // macOS: predict from calibrated glyph-bottom position.
            // The glyph shifts linearly with subPx in DIP space.
            const int   predicted  = static_cast<int>(std::floor(B0 + subPx));
#endif
            table.push_back({fontHeight, subPx, lowestY, predicted});
        }
    }

    // ---- Print the table ----
    printf("\n");
    printf("Lowest lit pixel Y for 'L' — %s, DPI %.0f (%d%%), bitmap %dx%d px\n",
           fontName, dpi, static_cast<int>(dpiScale * 100.0f), pixW, pixH);
    printf("Baseline placed at DIP Y = %.1f + sub-pixel offset\n\n", kBaselineCenter);

    printf("         |");
    for (int si = 0; si < 10; ++si)
        printf("  sp=%.1f  ", si * 0.1f);
    printf("\n");

    printf("  FontHt |");
    for (int si = 0; si < 10; ++si)
        printf(" low prd  e ");
    printf("\n");

    printf("---------+");
    for (int si = 0; si < 10; ++si)
        printf("-----------");
    printf("\n");

    int idx = 0;
    int errorCount = 0;
    for (float fh = 6.0f; fh < 24.0f; fh += 0.5f)
    {
        printf("  %5.1f  |", fh);
        for (int si = 0; si < 10; ++si)
        {
            const auto& r = table[idx++];
            const int error = r.lowestLitY - r.predictedPixel;
            const bool bad = (r.lowestLitY < 0) || (error != 0);
            if (bad) ++errorCount;
            printf(" %3d %3d %+2d%s", r.lowestLitY, r.predictedPixel, error, bad ? "*" : " ");
        }
        printf("\n");
    }
    printf("\n");
    printf("  * = ERROR (no lit pixel or prediction mismatch)\n");
    printf("  Total cells: %d,  Errors: %d\n\n", static_cast<int>(table.size()), errorCount);

    return errorCount;
}

// 96 DPI (100%)
TEST_F(DrawingTest, TextBaselineLowestPixel)
{
    auto makeTF = [this](float h) { return makeTextFormat(h); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 96.0f);
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong at 96 DPI";
}

// 120 DPI (125%)
TEST_F(DrawingTest, TextBaselineLowestPixel125)
{
    auto makeTF = [this](float h) { return makeTextFormat(h); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 120.0f);

#ifdef _WIN32
    constexpr int expectedErrors = 28; // 28 errors at 120 DPI due to rounding edge cases in D2D's snapping logic
#else
	constexpr int expectedErrors = 0; // macOS predictions are exact after calibration
#endif

    EXPECT_EQ(errors, expectedErrors) << errors << " baseline prediction(s) were wrong at 120 DPI";
}

// 168 DPI (175%)
TEST_F(DrawingTest, TextBaselineLowestPixel175)
{
    auto makeTF = [this](float h) { return makeTextFormat(h); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 168.0f);
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong at 168 DPI";
}

// 192 DPI (200%)
TEST_F(DrawingTest, TextBaselineLowestPixel200)
{
    auto makeTF = [this](float h) { return makeTextFormat(h); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 192.0f);
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong at 192 DPI";
}

// ---- Times New Roman (serif) ----

TEST_F(DrawingTest, TextBaselineLowestPixel_TimesNewRoman)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Times New Roman"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 96.0f, "Times New Roman");
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Times New Roman, 96 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_TimesNewRoman125)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Times New Roman"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 120.0f, "Times New Roman");
#ifdef _WIN32
    constexpr int expectedErrors = 28; // 28 errors at 120 DPI due to rounding edge cases in D2D's snapping logic
#else
    constexpr int expectedErrors = 0; // macOS predictions are exact after calibration
#endif
    EXPECT_EQ(errors, expectedErrors) << errors << " baseline prediction(s) were wrong (Times New Roman, 120 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_TimesNewRoman175)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Times New Roman"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 168.0f, "Times New Roman");
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Times New Roman, 168 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_TimesNewRoman200)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Times New Roman"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 192.0f, "Times New Roman");
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Times New Roman, 192 DPI)";
}

// ---- Courier New (monospace) ----

TEST_F(DrawingTest, TextBaselineLowestPixel_CourierNew)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Courier New"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 96.0f, "Courier New");

    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Courier New, 96 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_CourierNew125)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Courier New"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 120.0f, "Courier New");
#ifdef _WIN32
    constexpr int expectedErrors = 42; // 28 errors at 120 DPI due to rounding edge cases in D2D's snapping logic
#else
    constexpr int expectedErrors = 0; // macOS predictions are exact after calibration
#endif
    EXPECT_EQ(errors, expectedErrors) << errors << " baseline prediction(s) were wrong (Courier New, 120 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_CourierNew175)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Courier New"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 168.0f, "Courier New");
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Courier New, 168 DPI)";
}

TEST_F(DrawingTest, TextBaselineLowestPixel_CourierNew200)
{
    auto makeTF = [this](float h) { return makeTextFormat(h, "Courier New"); };
    int errors = runBaselineLowestPixelTest(drawingContext, makeTF, 192.0f, "Courier New");
    EXPECT_EQ(errors, 0) << errors << " baseline prediction(s) were wrong (Courier New, 192 DPI)";
}

// ============================================================
// Rich text (markdown) tests
// ============================================================

// Simple rich text with no markdown — should render identically to plain text.
TEST_F(DrawingTest, RichTextPlain)
{
    auto rtf = makeRichTextFormat("Hello", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextPlain", 2));
}

// Bold text via **markdown**.
TEST_F(DrawingTest, RichTextBold)
{
    auto rtf = makeRichTextFormat("**Bold**", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextBold", 2));
}

// Italic text via *markdown*.
TEST_F(DrawingTest, RichTextItalic)
{
    auto rtf = makeRichTextFormat("*Italic*", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextItalic", 40, 50.0));
}

// Bold italic text via ***markdown***.
TEST_F(DrawingTest, RichTextBoldItalic)
{
    auto rtf = makeRichTextFormat("***BdIt***", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextBoldItalic", 40, 50.0));
}

// Mixed plain and bold in one string.
TEST_F(DrawingTest, RichTextMixed)
{
    auto rtf = makeRichTextFormat("Hi **bold** end", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextMixed", 2));
}

// Centre-aligned rich text — alignment is set at creation time.
TEST_F(DrawingTest, RichTextCentred)
{
    auto rtf = makeRichTextFormat("*Hi*", 12.f, "Arial",
        TextAlignment::Center, ParagraphAlignment::Center);
    g.drawRichTextU(rtf, {0.f, 0.f, 64.f, 64.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextCentred", 40, 50.0));
}

// Verify getTextExtentU returns a non-zero size for rich text.
TEST_F(DrawingTest, RichTextExtent)
{
    auto rtf = makeRichTextFormat("**Test**", 14.f);
    auto size = rtf.getTextExtentU();
    EXPECT_GT(size.width, 0.f);
    EXPECT_GT(size.height, 0.f);
}

// Heading renders larger and bold.
TEST_F(DrawingTest, RichTextHeading)
{
    auto rtf = makeRichTextFormat("# Hi", 10.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextHeading", 2));
}

// Bullet list renders with bullet character.
TEST_F(DrawingTest, RichTextBulletList)
{
    auto rtf = makeRichTextFormat("- A\n- B", 10.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextBulletList", 2));
}

// Inline code renders in monospace font.
TEST_F(DrawingTest, RichTextInlineCode)
{
    auto rtf = makeRichTextFormat("`code`", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextInlineCode", 2));
}

// Strikethrough text.
TEST_F(DrawingTest, RichTextStrikethrough)
{
    auto rtf = makeRichTextFormat("~~no~~", 12.f);
    g.drawRichTextU(rtf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextStrikethrough", 2));
}

// Multi-block: heading + list.
TEST_F(DrawingTest, RichTextMultiBlock)
{
    auto rtf = makeRichTextFormat("## Hi\n\n- A\n- B", 7.f);
    g.drawRichTextU(rtf, {1.f, 1.f, 63.f, 63.f}, g.createSolidColorBrush(Colors::Black), kTextOptions);
    EXPECT_TRUE(checkResult("richTextMultiBlock", 2));
}
