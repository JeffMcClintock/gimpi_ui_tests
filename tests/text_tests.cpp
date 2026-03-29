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
    g.drawTextU("Hello", tf, {2.f, 2.f, 62.f, 62.f}, g.createSolidColorBrush(Colors::Black));
    EXPECT_TRUE(checkResult("drawTextSimple", 2));
}

// Text centred horizontally and vertically in the bitmap.
TEST_F(DrawingTest, DrawTextCentred)
{
    auto tf = makeTextFormat(12.f);
    tf.setTextAlignment(TextAlignment::Center);
    tf.setParagraphAlignment(ParagraphAlignment::Center);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Hi", tf, {0.f, 0.f, 64.f, 64.f}, brush);
    EXPECT_TRUE(checkResult("drawTextCentred", 2));
}

// Bold weight text.
TEST_F(DrawingTest, DrawTextBold)
{
    auto tf    = makeTextFormat(12.f, "Arial", FontWeight::Bold);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Bold", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("drawTextBold", 2));
}

// Larger font size to stress-test glyph outlines.
TEST_F(DrawingTest, DrawTextLarge)
{
    auto tf    = makeTextFormat(24.f);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Ag", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("drawTextLarge", 2));
}

// Two lines of text — verifies line spacing.
TEST_F(DrawingTest, DrawTextMultiLine)
{
    auto tf = makeTextFormat(10.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("Line one\nLine two", tf, {2.f, 2.f, 62.f, 62.f}, brush);
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
    g.drawTextU("Test", tf, {0.f, 0.f, 64.f, 64.f}, textBrush);
    EXPECT_TRUE(checkResult("drawTextColoured", 2, 20.0));
}

// An empty string should not affect the bitmap (remains all-white).
TEST_F(DrawingTest, EmptyStringText)
{
    auto tf    = makeTextFormat(12.f);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("", tf, {2.f, 2.f, 62.f, 62.f}, brush);
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
    g.drawTextU("Ag", tf, {0.f, 0.f, 64.f, 64.f}, brush);
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
    g.drawTextU("Overflow Right Edge", tf, {2.f, 22.f, 40.f, 42.f}, brush);
    EXPECT_TRUE(checkResult("textOverflowsLayoutRect", 34, 15.0));
}

// With DrawTextOptions::Clip, text is clipped to the layout rect.
TEST_F(DrawingTest, TextClippedByLayoutRect)
{
    auto tf    = makeTextFormat(14.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    // Same narrow rect, but Clip flag is set — text should be cut off.
    g.drawTextU("ClipMe Right Edge", tf, {2.f, 22.f, 40.f, 42.f}, brush, DrawTextOptions::Clip);
    EXPECT_TRUE(checkResult("textClippedByLayoutRect", 34, 15.0));
}

// Word-wrap ON: a long string breaks across multiple lines within the layout rect.
TEST_F(DrawingTest, TextWrapOn)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::Wrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush);
    EXPECT_TRUE(checkResult("textWrapOn", 34, 25.0));
}

// Word-wrap OFF: same long string runs in one line and is clipped on the right.
TEST_F(DrawingTest, TextWrapOff)
{
    auto tf = makeTextFormat(11.f);
    tf.setWordWrapping(WordWrapping::NoWrap);
    auto brush = g.createSolidColorBrush(Colors::Black);
    g.drawTextU("The quick brown fox jumps", tf, {2.f, 2.f, 62.f, 62.f}, brush);
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
                 tf, {2.f, 2.f, 62.f, 28.f}, brush);
    EXPECT_TRUE(checkResult("textClippedAtBottom", 10, 35.0));
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

    bigRT.drawLine({0.f, ascenderY},  {W, ascenderY},  brushAsc,  1.f);
    bigRT.drawLine({0.f, capY},       {W, capY},        brushCap,  1.f);
    bigRT.drawLine({0.f, xhY},        {W, xhY},         brushXH,   1.f);
    bigRT.drawLine({0.f, kBaselineY}, {W, kBaselineY},  brushBase, 1.f);
    bigRT.drawLine({0.f, descenderY}, {W, descenderY},  brushDesc, 1.f);

    // Underline and strikethrough as thicker dashed strokes.
    StrokeStyleProperties dashProps;
    dashProps.dashStyle = DashStyle::Dash;
    auto dashStyle = bigRT.getFactory().createStrokeStyle(dashProps);
    bigRT.drawLine({0.f, underlineY}, {W, underlineY}, brushUL, 2.f, dashStyle);
    bigRT.drawLine({0.f, strikeY},    {W, strikeY},    brushST, 2.f, dashStyle);

    // ---- glyphs: black text drawn on top of the lines ----
    auto textBrush = bigRT.createSolidColorBrush(Colors::Black);
    // Position layout rect so text baseline lands exactly at kBaselineY.
    const gmpi::drawing::Rect layoutRect{4.f, ascenderY, W, descenderY};
    bigRT.drawTextU("Hfgx", tf, layoutRect, textBrush);

    // ---- labels (small Arial, right-aligned, matching line colour) ----
    auto labelTF = makeTextFormat(9.f);
    labelTF.setTextAlignment(TextAlignment::Trailing);
    FontMetrics labelFm = labelTF.getFontMetrics();
    const float labelW = 80.f;
    const float labelX = W - labelW;
    auto labelY = [&](float lineY) { return lineY - labelFm.capHeight - 1.f; };

    bigRT.drawTextU("ascender",    labelTF, {labelX, labelY(ascenderY),  W, ascenderY},  brushAsc);
    bigRT.drawTextU("cap-height",  labelTF, {labelX, labelY(capY),       W, capY},       brushCap);
    bigRT.drawTextU("x-height",    labelTF, {labelX, labelY(xhY),        W, xhY},        brushXH);
    bigRT.drawTextU("baseline",    labelTF, {labelX, labelY(kBaselineY), W, kBaselineY}, brushBase);
    bigRT.drawTextU("descender",   labelTF, {labelX, descenderY + 1.f,   W, descenderY + labelFm.ascent + 1.f}, brushDesc);
    bigRT.drawTextU("underline",   labelTF, {labelX, labelY(underlineY), W, underlineY}, brushUL);
    bigRT.drawTextU("strikethrough",labelTF,{labelX, labelY(strikeY),    W, strikeY},    brushST);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 80.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 252.f, 54.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 100.f, 252.f}, brush);

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
    bigRT.drawTextU(kMultilineText, tf, {4.f, 4.f, 100.f, 252.f}, brush);

    bigRT.endDraw();
    EXPECT_TRUE(checkBitmap("multilineNarrowWrapLH40", bigRT, 2));
}
