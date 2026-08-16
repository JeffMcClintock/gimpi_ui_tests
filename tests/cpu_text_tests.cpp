// Text tests for the pure-software CPU backend (helpers/CpuTextEngine.h).
//
// Text is compared differently from everything else. Metrics and layout —
// extents, line counts, alignment positions — are asserted, because hosts
// position UI off them and they must agree with the platform backends. Pixels
// are not: HarfBuzz outlines rasterized here and DirectWrite's own rasterizer
// legitimately differ, which is why the golden-image fixture keeps per-platform
// text references.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Drawing.h"
#include "backends/CpuGfx.h"
#include "helpers/BundledFonts.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h"
#include "helpers/FontProvider.h"
#include "helpers/SavePng.h"

#include <hb-ot.h>

#ifdef _WIN32
#include <objbase.h>
#endif

using namespace gmpi::drawing;

#if GMPI_UI_HAVE_FONT_PROVIDER

namespace
{

// A font every platform in CI actually has: Selawik, bundled in fonts/ and
// registered below, rather than relying on whatever the OS happens to ship.
constexpr const char* kTestFont = "Selawik";

struct SelawikFontRegistration
{
    SelawikFontRegistration()
    {
        const std::string fontsDir = FONTS_DIR;
        gmpi::drawing::registerBundledFont("Selawik", gmpi::drawing::FontWeight::Regular,
            gmpi::drawing::FontStyle::Normal, fontsDir + "/Selawik-Regular.ttf");
        gmpi::drawing::registerBundledFont("Selawik", gmpi::drawing::FontWeight::Bold,
            gmpi::drawing::FontStyle::Normal, fontsDir + "/Selawik-Bold.ttf");
    }
} g_selawikFontRegistration;

struct TextContext
{
    gmpi::cpugfx::Factory factoryImpl;
    CpuTextEngine engine{ findFont };
    Factory factory;

    TextContext()
    {
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        engine.imageDecoder = gmpi::drawing::decodeImageMemory; // colour emoji PNGs
        factoryImpl.textEngine = &engine;
        *AccessPtr::put(factory) = &factoryImpl;
    }

    ~TextContext()
    {
#ifdef _WIN32
        CoUninitialize();
#endif
    }

    TextFormat makeFormat(float height, const char* family = kTestFont,
                          FontFlags flags = FontFlags::BodyHeight)
    {
        std::string_view name{ family };
        return factory.createTextFormat(height, { &name, 1 }, FontWeight::Regular,
                                        FontStyle::Normal, FontStretch::Normal, flags);
    }

    BitmapRenderTarget makeTarget(uint32_t w = 96, uint32_t h = 48)
    {
        return factory.createCpuRenderTarget({ w, h }, 0);
    }
};

// Fraction of pixels that differ from the background — a cheap way to assert
// "something was drawn, roughly this much of it".
float inkFraction(Bitmap& bmp)
{
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const auto size = bmp.getSize();
    int ink{};
    for (uint32_t y = 0; y < size.height; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (uint32_t x = 0; x < size.width; ++x)
            if (detail::halfToFloat(row[x * 4]) < 0.9f) // red channel darkened
                ++ink;
    }
    return float(ink) / float(size.width * size.height);
}

// Bounding box of drawn pixels, for asserting alignment moved the text.
struct InkBounds { int left{ 9999 }, top{ 9999 }, right{ -1 }, bottom{ -1 }; bool any() const { return right >= 0; } };

InkBounds inkBounds(Bitmap& bmp)
{
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const auto size = bmp.getSize();
    InkBounds b;
    for (uint32_t y = 0; y < size.height; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (uint32_t x = 0; x < size.width; ++x)
        {
            if (detail::halfToFloat(row[x * 4]) < 0.9f)
            {
                b.left = (std::min)(b.left, int(x));
                b.right = (std::max)(b.right, int(x));
                b.top = (std::min)(b.top, int(y));
                b.bottom = (std::max)(b.bottom, int(y));
            }
        }
    }
    return b;
}

} // namespace

TEST(CpuText, CreatesFormatAndReportsMetrics)
{
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr) << "font not found: " << kTestFont;

    const auto metrics = format.getFontMetrics();
    EXPECT_GT(metrics.ascent, 0.0f);
    EXPECT_GT(metrics.descent, 0.0f);
    EXPECT_GT(metrics.capHeight, 0.0f);
    EXPECT_GT(metrics.xHeight, 0.0f);
    EXPECT_LT(metrics.xHeight, metrics.capHeight) << "x-height must be below cap height";
    EXPECT_LT(metrics.capHeight, metrics.ascent + metrics.descent);

    // The default (FontFlags::BodyHeight) means the requested height is the EM
    // size, not the body height. That reads backwards from the flag's name, and
    // it is deliberate: FontFlags::BodyHeight is 0, so the Direct2D backend's
    // "(flags & BodyHeight) != 0" never fires and it applies no scaling either.
    // Matching the reference backend matters more than matching the name —
    // CpuVsD2D.TextMetricsAgreeWithDirectWrite pins them together exactly.
    // Selawik's body is about 1.33 em (usWinAscent+usWinDescent / unitsPerEm),
    // so 20 gives roughly 26.6.
    EXPECT_GT(metrics.ascent + metrics.descent, 20.0f);
    EXPECT_LT(metrics.ascent + metrics.descent, 28.0f);
}

TEST(CpuText, CapHeightFlagRescales)
{
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f, kTestFont, FontFlags::CapHeight);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const auto metrics = format.getFontMetrics();
    EXPECT_NEAR(metrics.capHeight, 20.0f, 0.1f)
        << "FontFlags::CapHeight must rescale so the cap height matches the request";
}

TEST(CpuText, ExtentGrowsWithTextAndSize)
{
    TextContext ctx;
    auto smallFormat = ctx.makeFormat(12.0f);
    auto largeFormat = ctx.makeFormat(24.0f);
    ASSERT_NE(AccessPtr::get(smallFormat), nullptr);

    const auto shortExtent = smallFormat.getTextExtentU("Hi");
    const auto longExtent = smallFormat.getTextExtentU("Hello, world");
    EXPECT_GT(longExtent.width, shortExtent.width);
    EXPECT_NEAR(longExtent.height, shortExtent.height, 0.01f) << "one line either way";

    const auto largeExtent = largeFormat.getTextExtentU("Hello, world");
    EXPECT_GT(largeExtent.width, longExtent.width);
    EXPECT_GT(largeExtent.height, longExtent.height);

    // Empty text has no width.
    EXPECT_NEAR(smallFormat.getTextExtentU("").width, 0.0f, 0.01f);
}

TEST(CpuText, ExplicitNewlinesMakeLines)
{
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const auto metrics = format.getFontMetrics();
    const auto one = format.getTextExtentU("one");
    const auto three = format.getTextExtentU("one\ntwo\nthree");

    // Each extra line adds a full advance (which includes lineGap), but there
    // is no trailing gap after the last one — the same shape DirectWrite has.
    const float advance = metrics.ascent + metrics.descent + metrics.lineGap;
    EXPECT_NEAR(one.height, metrics.ascent + metrics.descent, 0.05f);
    EXPECT_NEAR(three.height, one.height + 2.0f * advance, 0.05f);
    EXPECT_GT(three.width, one.width) << "widest line wins";
}

TEST(CpuText, WordWrapRespectsMaxWidth)
{
    TextContext ctx;
    auto format = ctx.makeFormat(14.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    const char* text = "the quick brown fox jumps over the lazy dog";
    const auto unwrapped = format.getTextExtentU(text, 100000.0f);
    const auto wrapped = format.getTextExtentU(text, 80.0f);

    EXPECT_LE(wrapped.width, 80.0f + 0.5f) << "wrapped text must fit the limit";
    EXPECT_GT(wrapped.height, unwrapped.height) << "wrapping adds lines";

    // NoWrap ignores the limit.
    AccessPtr::get(format)->setWordWrapping(WordWrapping::NoWrap);
    const auto noWrap = format.getTextExtentU(text, 80.0f);
    EXPECT_NEAR(noWrap.height, unwrapped.height, 0.01f);
    EXPECT_GT(noWrap.width, 80.0f);
}

TEST(CpuText, LineSpacingOverridesContentHeight)
{
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const auto natural = format.getTextExtentU("a\nb");
    AccessPtr::get(format)->setLineSpacing(30.0f, 24.0f);
    const auto forced = format.getTextExtentU("a\nb");
    EXPECT_NEAR(forced.height, 60.0f, 0.01f) << "uniform spacing overrides the content";
    EXPECT_NE(natural.height, forced.height);
}

TEST(CpuText, DrawsInkAndScalesWithSize)
{
    TextContext ctx;
    auto rt = ctx.makeTarget();
    auto smallFormat = ctx.makeFormat(12.0f);
    ASSERT_NE(AccessPtr::get(smallFormat), nullptr);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("Hello", smallFormat, { 2.f, 2.f, 94.f, 46.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    const float smallInk = inkFraction(bmp);
    EXPECT_GT(smallInk, 0.001f) << "nothing was drawn";
    EXPECT_LT(smallInk, 0.5f) << "far too much was drawn";

    auto rt2 = ctx.makeTarget();
    auto largeFormat = ctx.makeFormat(28.0f);
    rt2.beginDraw();
    rt2.clear(Colors::White);
    auto black2 = rt2.createSolidColorBrush(Colors::Black);
    rt2.drawTextU("Hello", largeFormat, { 2.f, 2.f, 94.f, 46.f }, black2);
    rt2.endDraw();

    auto bmp2 = rt2.getBitmap();
    EXPECT_GT(inkFraction(bmp2), smallInk) << "bigger text should mark more pixels";

    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_hello.png", bmp2);
}

TEST(CpuText, GlyphCountersAreHoles)
{
    // 'o' must have a hole. Glyph outlines wind counters opposite to the
    // exterior, so this fails if the fill mode is wrong.
    TextContext ctx;
    auto rt = ctx.makeTarget(64, 64);
    auto format = ctx.makeFormat(56.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("o", format, { 2.f, 2.f, 62.f, 62.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    const auto bounds = inkBounds(bmp);
    ASSERT_TRUE(bounds.any()) << "nothing drawn";

    // The centre of the glyph's bounding box should be background.
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    const int cx = (bounds.left + bounds.right) / 2;
    const int cy = (bounds.top + bounds.bottom) / 2;
    const uint16_t* p = reinterpret_cast<const uint16_t*>(
        px.getAddress() + size_t(cy) * px.getBytesPerRow() + size_t(cx) * 8);
    EXPECT_GT(detail::halfToFloat(p[0]), 0.9f)
        << "the counter of 'o' is filled — glyph outlines need nonzero winding";
}

TEST(CpuText, AlignmentMovesInk)
{
    TextContext ctx;
    const gmpi::drawing::Rect layout{ 0.f, 0.f, 96.f, 48.f };

    const auto render = [&](TextAlignment align, ParagraphAlignment para) {
        auto rt = ctx.makeTarget();
        auto format = ctx.makeFormat(14.0f);
        AccessPtr::get(format)->setTextAlignment(align);
        AccessPtr::get(format)->setParagraphAlignment(para);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto black = rt.createSolidColorBrush(Colors::Black);
        rt.drawTextU("Hi", format, layout, black);
        rt.endDraw();
        auto bmp = rt.getBitmap();
        return inkBounds(bmp);
    };

    const auto leading = render(TextAlignment::Leading, ParagraphAlignment::Near);
    const auto trailing = render(TextAlignment::Trailing, ParagraphAlignment::Near);
    const auto centre = render(TextAlignment::Center, ParagraphAlignment::Near);
    ASSERT_TRUE(leading.any() && trailing.any() && centre.any());

    EXPECT_LT(leading.left, centre.left);
    EXPECT_LT(centre.left, trailing.left);
    EXPECT_GT(trailing.right, centre.right);

    const auto nearBounds = render(TextAlignment::Leading, ParagraphAlignment::Near);
    const auto farBounds = render(TextAlignment::Leading, ParagraphAlignment::Far);
    EXPECT_LT(nearBounds.top, farBounds.top) << "Far alignment pushes text down the layout rect";
}

// CJK is the reason shaping went in. Stage A has no font fallback, so name a
// font that actually covers the script; stage B adds automatic fallback.
TEST(CpuText, RendersCjkWithACoveringFont)
{
    TextContext ctx;
    // First CJK-capable family the platform has.
    const char* candidates[] = { "MS Gothic", "Yu Gothic", "SimSun", "Microsoft YaHei",
                                 "Hiragino Sans", "Noto Sans CJK JP", "Noto Sans CJK SC" };
    TextFormat format;
    const char* chosen{};
    for (const char* family : candidates)
    {
        auto candidate = ctx.makeFormat(28.0f, family);
        // The wrapper falls back to Arial, so confirm the family really exists
        // rather than trusting a non-null result.
        gmpi::drawing::api::ITextFormat* probe{};
        if (ctx.factoryImpl.createTextFormat(family, FontWeight::Regular, FontStyle::Normal,
                                             FontStretch::Normal, 28.0f, 0, &probe) == gmpi::ReturnCode::Ok && probe)
        {
            probe->release();
            format = std::move(candidate);
            chosen = family;
            break;
        }
    }
    if (!chosen)
        GTEST_SKIP() << "no CJK font installed on this machine";

    // UTF-8 bytes for 你好世界, written explicitly so the test does not depend
    // on the compiler's source-encoding assumptions.
    const char* cjk = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";

    // Shaping must produce real advances, not zeros or notdef boxes.
    const auto extent = format.getTextExtentU(cjk);
    EXPECT_GT(extent.width, 28.0f) << "four CJK glyphs should be wider than one em";
    EXPECT_GT(extent.height, 0.0f);

    auto rt = ctx.makeTarget(160, 48);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(cjk, format, { 2.f, 2.f, 158.f, 46.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    const float ink = inkFraction(bmp);
    EXPECT_GT(ink, 0.02f) << "CJK glyphs did not render (font " << chosen << ")";
    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_cjk.png", bmp);
}

// --- Stage B: fallback, line breaking, grapheme clusters -------------------

namespace
{
// UTF-8 literals written as explicit bytes, so these tests do not depend on
// the compiler's source-encoding assumptions.
constexpr const char* kNiHao = "\xE4\xBD\xA0\xE5\xA5\xBD";                     // 你好
constexpr const char* kCjkSentence = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
                                     "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"; // 你好世界你好世界
constexpr const char* kAccented = "e\xCC\x81";                                 // e + combining acute
} // namespace

TEST(CpuText, FallsBackForUncoveredScript)
{
    // Selawik has no CJK. Without fallback these become .notdef boxes or nothing;
    // with it, a covering font is found automatically.
    TextContext ctx;
    auto format = ctx.makeFormat(24.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const auto latin = format.getTextExtentU("ab");
    const auto cjk = format.getTextExtentU(kNiHao);
    EXPECT_GT(cjk.width, 0.0f) << "CJK produced no advances at all";

    auto rt = ctx.makeTarget(120, 48);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    // Mixed script in one string: this is what fallback is for.
    rt.drawTextU("Hi \xE4\xBD\xA0\xE5\xA5\xBD", format, { 2.f, 2.f, 118.f, 46.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.02f) << "mixed Latin/CJK text did not render";
    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_fallback_mixed.png", bmp);

    // A CJK glyph is around an em wide, far wider than two Latin letters at
    // the same size — a cheap check that real glyphs were found, not blanks.
    EXPECT_GT(cjk.width, latin.width * 0.8f);
}

TEST(CpuText, CjkWrapsBetweenCharacters)
{
    // CJK has no spaces, so a space-only line breaker cannot wrap it at all.
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    const auto unwrapped = format.getTextExtentU(kCjkSentence, 100000.0f);
    ASSERT_GT(unwrapped.width, 0.0f);

    const float limit = unwrapped.width * 0.4f;
    const auto wrapped = format.getTextExtentU(kCjkSentence, limit);

    EXPECT_GT(wrapped.height, unwrapped.height) << "CJK did not wrap — it has no spaces to break at";
    EXPECT_LE(wrapped.width, limit + 0.5f) << "wrapped CJK exceeded the width limit";
}

TEST(CpuText, LongUnbreakableWordStillProgresses)
{
    // A single word wider than the limit has no break opportunity. It must
    // overflow rather than loop forever or produce zero-length lines.
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    const auto extent = format.getTextExtentU("Supercalifragilistic", 10.0f);
    EXPECT_GT(extent.width, 0.0f);
    EXPECT_GT(extent.height, 0.0f);
    EXPECT_LT(extent.height, 16.0f * 40.0f) << "one line per glyph suggests a runaway break loop";
}

TEST(CpuText, CombiningMarkStaysWithItsBase)
{
    // "e" + combining acute is ONE grapheme cluster: it must never be split
    // across a line break, and must not be itemised into a different font.
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    const auto combined = format.getTextExtentU(kAccented, 100000.0f);
    const auto plain = format.getTextExtentU("e", 100000.0f);

    // The mark composes onto the base, so the pair is no wider than a letter
    // or two — certainly not two full advances plus a separate mark glyph.
    EXPECT_GT(combined.width, 0.0f);
    EXPECT_LT(combined.width, plain.width * 2.5f);

    // Squeezing the width must not break between base and mark: with only one
    // cluster there is nowhere to break, so it stays a single line.
    const auto squeezed = format.getTextExtentU(kAccented, 1.0f);
    EXPECT_NEAR(squeezed.height, combined.height, 0.01f) << "a grapheme cluster was split";
}

TEST(CpuText, SegmentationClassifiesClustersAndBreaks)
{
    using namespace gmpi::drawing::text;

    // Grapheme clusters: base + combining mark is one cluster.
    {
        const auto cps = decodeUtf8(kAccented);
        ASSERT_EQ(cps.size(), 2u);
        const auto boundaries = graphemeBoundaries(cps);
        EXPECT_EQ(boundaries[0], 1);
        EXPECT_EQ(boundaries[1], 0) << "a combining mark must not start a cluster";
    }

    // A ZWJ emoji sequence is one cluster: man + ZWJ + computer.
    {
        const auto cps = decodeUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB");
        ASSERT_EQ(cps.size(), 3u);
        const auto boundaries = graphemeBoundaries(cps);
        EXPECT_EQ(boundaries[0], 1);
        EXPECT_EQ(boundaries[1], 0) << "ZWJ joins";
        EXPECT_EQ(boundaries[2], 0) << "the glyph after a ZWJ joins too";
    }

    // A flag is a pair of regional indicators: one cluster, and a third
    // indicator starts a new one.
    {
        const auto cps = decodeUtf8("\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xAF");
        ASSERT_EQ(cps.size(), 3u);
        const auto boundaries = graphemeBoundaries(cps);
        EXPECT_EQ(boundaries[1], 0) << "the second regional indicator completes the flag";
        EXPECT_EQ(boundaries[2], 1) << "a third starts a new flag";
    }

    // Skin tone modifiers attach to the preceding emoji.
    {
        const auto cps = decodeUtf8("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");
        ASSERT_EQ(cps.size(), 2u);
        EXPECT_EQ(graphemeBoundaries(cps)[1], 0);
    }

    // Line breaks: allowed between ideographs, forbidden before closing
    // punctuation (kinsoku).
    {
        const auto cps = decodeUtf8("\xE4\xBD\xA0\xE5\xA5\xBD\xE3\x80\x82"); // 你好。
        ASSERT_EQ(cps.size(), 3u);
        const auto breaks = lineBreakOpportunities(cps);
        EXPECT_EQ(breaks[1], 1) << "a break is allowed between two ideographs";
        EXPECT_EQ(breaks[2], 0) << "a full stop must not start a line";
    }

    // Latin breaks after a space, not mid-word.
    {
        const auto cps = decodeUtf8("ab cd");
        const auto breaks = lineBreakOpportunities(cps);
        EXPECT_EQ(breaks[1], 0);
        EXPECT_EQ(breaks[3], 1) << "break after the space";
        EXPECT_EQ(breaks[4], 0);
    }

    // Malformed UTF-8 degrades to a replacement character rather than vanishing.
    {
        const auto cps = decodeUtf8("a\xFF\x62");
        ASSERT_EQ(cps.size(), 3u);
        EXPECT_EQ(cps[0].value, uint32_t('a'));
        EXPECT_EQ(cps[1].value, 0xFFFDu);
        EXPECT_EQ(cps[2].value, uint32_t('b'));
    }
}

// --- Stage C: bitmap colour glyphs (CBDT/sbix) -----------------------------

namespace
{
constexpr const char* kGrinningFace = "\xF0\x9F\x98\x80";  // U+1F600
constexpr const char* kThumbsUpTone = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"; // 👍 + medium skin tone

// Which colour-glyph tables the machine's emoji font actually carries. This
// decides what is testable here: Apple Color Emoji is sbix and Noto Color
// Emoji has historically been CBDT (both PNG, stage C), while Segoe UI Emoji
// on Windows is COLR/CPAL vector (stage D).
struct ColorFontReport { bool found{}, png{}, layers{}, paint{}; std::string family; };

ColorFontReport probeEmojiFont()
{
    const char* candidates[] = { "Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji",
                                 "Twemoji Mozilla", "Segoe UI Symbol" };
    for (const char* family : candidates)
    {
        FontRequest request;
        request.familyName = family;
        request.mustCoverCodepoint = 0x1F600;
        FontData data;
        if (!findFont(request, data) || !data)
            continue;

        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data.bytes.data()),
                                         unsigned(data.bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        hb_face_t* face = hb_face_create(blob, data.faceIndex);
        ColorFontReport report;
        report.found = true;
        report.family = family;
        report.png = hb_ot_color_has_png(face) != 0;
        report.layers = hb_ot_color_has_layers(face) != 0;
        report.paint = hb_ot_color_has_paint(face) != 0;
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        return report;
    }
    return {};
}
} // namespace

TEST(CpuText, ReportsEmojiFontColourFormat)
{
    // Informational, and it decides which stage covers emoji on this platform.
    const auto report = probeEmojiFont();
    if (!report.found)
        GTEST_SKIP() << "no emoji font found";

    std::cout << "  [EMOJI] " << report.family
              << ": PNG(CBDT/sbix)=" << report.png
              << " COLRv0 layers=" << report.layers
              << " COLRv1 paint=" << report.paint << "\n";

    EXPECT_TRUE(report.png || report.layers || report.paint)
        << report.family << " has no colour glyph table at all";
}

TEST(CpuText, DecodesImagesFromMemory)
{
    // The decode-from-memory path is what colour glyphs use, since a font
    // carries its PNGs as blobs rather than files. Verify it independently of
    // whether this machine's emoji font happens to use PNG glyphs.
    TextContext ctx; // for COM on Windows
    const auto path = std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_memdecode.png";

    {
        auto rt = ctx.makeTarget(4, 2);
        rt.beginDraw();
        rt.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
        auto red = rt.createSolidColorBrush(Colors::Red);
        rt.fillRectangle({ 0.f, 0.f, 2.f, 2.f }, red);
        auto halfBlue = rt.createSolidColorBrush(Color{ 0.0f, 0.0f, 1.0f, 0.5f });
        rt.fillRectangle({ 2.f, 0.f, 4.f, 2.f }, halfBlue);
        rt.endDraw();
        auto bmp = rt.getBitmap();
        ASSERT_TRUE(savePng(path, bmp));
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file);
    const auto size = file.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size)); // not size_t(size): most vexing parse
    file.seekg(0);
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(bytes.data()), size));

    gmpi::drawing::DecodedImage fromMemory;
    ASSERT_TRUE(gmpi::drawing::decodeImageMemory(bytes.data(), bytes.size(), fromMemory));
    ASSERT_TRUE(static_cast<bool>(fromMemory));
    EXPECT_EQ(fromMemory.width, 4u);
    EXPECT_EQ(fromMemory.height, 2u);

    // Must agree with the file decoder, byte for byte.
    gmpi::drawing::DecodedImage fromFile;
    ASSERT_TRUE(gmpi::drawing::decodeImageFile(path, fromFile));
    ASSERT_EQ(fromMemory.pixels.size(), fromFile.pixels.size());
    EXPECT_EQ(fromMemory.pixels, fromFile.pixels) << "memory and file decoders disagree";

    // Straight alpha, as the contract requires: opaque red, then blue at half alpha.
    EXPECT_EQ(fromMemory.pixels[0], 255); // R
    EXPECT_EQ(fromMemory.pixels[3], 255); // A
    EXPECT_NEAR(fromMemory.pixels[8 + 2], 255, 2); // B of the translucent half
    EXPECT_NEAR(fromMemory.pixels[8 + 3], 128, 2); // its alpha

    // Garbage in must fail rather than crash.
    gmpi::drawing::DecodedImage junk;
    const uint8_t rubbish[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    EXPECT_FALSE(gmpi::drawing::decodeImageMemory(rubbish, sizeof(rubbish), junk));
    EXPECT_FALSE(gmpi::drawing::decodeImageMemory(nullptr, 0, junk));
}

TEST(CpuText, RendersEmojiSequenceAsOneCluster)
{
    // Regardless of colour format, an emoji must shape as a single cluster:
    // 👍 plus a skin-tone modifier is one glyph, not two.
    TextContext ctx;
    auto format = ctx.makeFormat(28.0f, kTestFont); // fallback finds the emoji font
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const auto single = format.getTextExtentU(kGrinningFace);
    const auto toned = format.getTextExtentU(kThumbsUpTone);
    EXPECT_GT(single.width, 0.0f) << "emoji produced no advance";
    EXPECT_LT(toned.width, single.width * 1.9f)
        << "a skin-tone modifier should compose, not add a second full glyph";
}

TEST(CpuText, DrawsBitmapColourEmoji)
{
    const auto report = probeEmojiFont();
    if (!report.found)
        GTEST_SKIP() << "no emoji font found";
    if (!report.png)
        GTEST_SKIP() << report.family << " uses COLR vector glyphs, not PNG — that is stage D";

    TextContext ctx;
    auto format = ctx.makeFormat(32.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto rt = ctx.makeTarget(64, 64);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(kGrinningFace, format, { 2.f, 2.f, 62.f, 62.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.02f) << "no emoji pixels";

    // Colour glyphs carry their own colour: the result must not be monochrome.
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    bool sawColour = false;
    for (int y = 0; y < 64 && !sawColour; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int x = 0; x < 64; ++x)
        {
            const float r = detail::halfToFloat(row[x * 4 + 0]);
            const float g = detail::halfToFloat(row[x * 4 + 1]);
            const float b = detail::halfToFloat(row[x * 4 + 2]);
            if (std::fabs(r - g) > 0.1f || std::fabs(g - b) > 0.1f) { sawColour = true; break; }
        }
    }
    EXPECT_TRUE(sawColour) << "emoji rendered without colour — the brush was used instead of the bitmap";
    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_emoji.png", bmp);
}

// --- Stage D: COLRv1 vector colour glyphs ----------------------------------

TEST(CpuText, DrawsColrColourEmoji)
{
    const auto report = probeEmojiFont();
    if (!report.found)
        GTEST_SKIP() << "no emoji font found";
    if (!report.paint)
        GTEST_SKIP() << report.family << " has no COLRv1 paint table";

    TextContext ctx;
    auto format = ctx.makeFormat(40.0f, kTestFont); // fallback finds the emoji font
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto rt = ctx.makeTarget(64, 64);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(kGrinningFace, format, { 2.f, 2.f, 62.f, 62.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.05f) << "COLRv1 emoji produced no pixels";

    // The point of COLR is colour: the glyph must not come out monochrome,
    // which is what would happen if the brush were used instead of the paint.
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    int colouredPixels = 0;
    for (int y = 0; y < 64; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int x = 0; x < 64; ++x)
        {
            const float r = detail::halfToFloat(row[x * 4 + 0]);
            const float g = detail::halfToFloat(row[x * 4 + 1]);
            const float b = detail::halfToFloat(row[x * 4 + 2]);
            if (std::fabs(r - g) > 0.15f || std::fabs(g - b) > 0.15f)
                ++colouredPixels;
        }
    }
    EXPECT_GT(colouredPixels, 50) << "emoji rendered monochrome — paint callbacks did not run";
    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_emoji_colr.png", bmp);
}

TEST(CpuText, MixedLatinCjkAndEmojiInOneString)
{
    // The whole text stack in one line: Latin from the requested font, CJK and
    // emoji found by fallback, the emoji in full colour, laid out together.
    TextContext ctx;
    auto format = ctx.makeFormat(30.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    const char* mixed = "Hi \xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x98\x80"; // Hi 你好 😀

    auto rt = ctx.makeTarget(200, 48);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(mixed, format, { 4.f, 4.f, 196.f, 44.f }, black);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.03f) << "mixed-script line did not render";
    savePng(std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview" / "text_mixed_all.png", bmp);
}

TEST(CpuText, ColourEmojiDoesNotLeakClipState)
{
    // Painting pushes clips and transforms. If any leaked, later drawing would
    // be clipped away or displaced.
    const auto report = probeEmojiFont();
    if (!report.found || !report.paint)
        GTEST_SKIP() << "no COLRv1 emoji font";

    TextContext ctx;
    auto format = ctx.makeFormat(24.0f, kTestFont);
    auto rt = ctx.makeTarget(64, 64);

    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(kGrinningFace, format, { 2.f, 2.f, 40.f, 40.f }, black);

    // A full-surface fill after the emoji must cover everything.
    auto blue = rt.createSolidColorBrush(Colors::Blue);
    rt.fillRectangle({ 0.f, 0.f, 64.f, 64.f }, blue);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    auto px = bmp.lockPixels(BitmapLockFlags::Read);
    for (int y = 0; y < 64; y += 7)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
        for (int x = 0; x < 64; x += 7)
        {
            EXPECT_NEAR(detail::halfToFloat(row[x * 4 + 2]), 1.0f, 0.02f)
                << "pixel (" << x << "," << y << ") was not covered: a clip leaked out of glyph painting";
            EXPECT_NEAR(detail::halfToFloat(row[x * 4 + 0]), 0.0f, 0.02f);
        }
    }

    // And the transform must be back where the caller left it.
    Matrix3x2 transform{};
    AccessPtr::get(rt)->getTransform(&transform);
    EXPECT_NEAR(transform._11, 1.0f, 1e-5f);
    EXPECT_NEAR(transform._31, 0.0f, 1e-5f);
    EXPECT_NEAR(transform._32, 0.0f, 1e-5f);
}

// --- Regressions from the text-engine review -------------------------------

TEST(CpuText, FallbackDoesNotStickToLaterLatin)
{
    // Once a fallback face was adopted it used to keep every following
    // character it happened to cover -- and fallback faces cover ASCII (emoji
    // fonts carry 0-9 and # for keycaps; CJK fonts carry half-width Latin). A
    // single leading symbol therefore dragged the whole string into the wrong
    // font: measured before the fix, "<emoji> Hello 2024" itemised as ONE
    // Segoe UI Emoji run.
    //
    // Assert FONT IDENTITY, not width. Width is a useless detector here:
    // fallback faces like Yu Gothic UI or Segoe UI Emoji can measure "Hello
    // 2024" close enough to the primary font that the wrong font reads as
    // almost the right width while the letterforms are completely different.
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto* fmt = dynamic_cast<gmpi::drawing::CpuTextFormat*>(AccessPtr::get(format));
    ASSERT_NE(fmt, nullptr);
    const auto primary = fmt->face;
    ASSERT_TRUE(primary != nullptr);

    const auto describe = [&](const char* sample) {
        const auto spans = fmt->itemize(sample);
        std::cout << "  [SPANS] \"" << sample << "\":";
        for (const auto& span : spans)
            std::cout << " [" << span.beginByte << "," << span.endByte << ")="
                      << (span.face ? span.face->name : std::string("null"));
        std::cout << std::endl;
        return spans;
    };

    {
        const auto spans = describe("Hello 2024");
        ASSERT_FALSE(spans.empty());
        for (const auto& span : spans)
            EXPECT_EQ(span.face, primary) << "plain Latin left the primary font";
    }

    // After CJK, and after an emoji, the trailing Latin must return to the
    // primary font -- which is what DirectWrite's MapCharacters does, since it
    // is handed the base family at every unmapped position.
    for (const char* sample : { "\xE4\xBD\xA0 Hello 2024", "\xF0\x9F\x98\x80 Hello 2024" })
    {
        const auto spans = describe(sample);
        ASSERT_GE(spans.size(), 2u) << "expected a fallback run and a return to the primary font";
        EXPECT_EQ(spans.back().face, primary)
            << "trailing Latin stayed in the fallback font ("
            << (spans.back().face ? spans.back().face->name : std::string("null")) << ")";
    }
}

TEST(CpuText, FallbackSharesOneCopyPerFontFile)
{
    // Fallback is resolved per CODEPOINT. A page of CJK asks for a font
    // hundreds of times, and each answer used to keep its own private copy of a
    // multi-megabyte font file. Rendering a lot of distinct CJK must not blow
    // memory up; this would previously allocate ~40 copies of the CJK font.
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f, kTestFont);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    // Say what the platform fallback resolves CJK to — when this test fails
    // with real advances but zero ink, the answer is usually "nothing", and
    // this line is the difference between a diagnosis and a guess (it was,
    // on the macOS CI runner).
    {
        gmpi::drawing::FontRequest req;
        req.familyName = kTestFont;
        req.mustCoverCodepoint = 0x4E00;
        gmpi::drawing::FontData data;
        const bool ok = gmpi::drawing::findFont(req, data);
        printf("[Fallback] findFont(cover U+4E00) -> %s, file '%s', %zu bytes, faceIndex %u\n",
               ok ? "ok" : "FAILED", data.resolvedName.c_str(), data.bytes.size(), data.faceIndex);

        // And what HarfBuzz sees in that blob — .ttc collections hold many
        // faces, and a provider that cannot say WHICH face was matched
        // (CoreText has no public API for the index) may hand the engine the
        // wrong one. Print every face's glyph count and whether it maps the
        // probe codepoint, so a wrong index is visible as data.
        if (ok && !data.bytes.empty())
        {
            hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data.bytes.data()),
                unsigned(data.bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
            const unsigned faces = hb_face_count(blob);
            printf("[Fallback] blob holds %u face(s); chosen index %u\n", faces, data.faceIndex);
            for (unsigned i = 0; i < faces && i < 4; ++i)
            {
                hb_face_t* face = hb_face_create(blob, i);
                hb_font_t* font = hb_font_create(face);
                hb_codepoint_t gid{};
                const bool mapped = hb_font_get_nominal_glyph(font, 0x4E00, &gid);

                // Which outline flavour the face carries, and whether HarfBuzz
                // can actually DRAW the mapped glyph — a face can map a
                // codepoint yet produce an empty outline (that is the mapped-
                // advances-but-zero-ink signature this diagnostic exists for).
                auto tableSize = [face](hb_tag_t tag) {
                    hb_blob_t* t = hb_face_reference_table(face, tag);
                    const unsigned n = hb_blob_get_length(t);
                    hb_blob_destroy(t);
                    return n;
                };
                unsigned segments = 0;
                hb_draw_funcs_t* funcs = hb_draw_funcs_create();
                hb_draw_funcs_set_move_to_func(funcs,
                    [](hb_draw_funcs_t*, void* c, hb_draw_state_t*, float, float, void*) { ++*static_cast<unsigned*>(c); }, nullptr, nullptr);
                hb_draw_funcs_set_line_to_func(funcs,
                    [](hb_draw_funcs_t*, void* c, hb_draw_state_t*, float, float, void*) { ++*static_cast<unsigned*>(c); }, nullptr, nullptr);
                hb_draw_funcs_set_quadratic_to_func(funcs,
                    [](hb_draw_funcs_t*, void* c, hb_draw_state_t*, float, float, float, float, void*) { ++*static_cast<unsigned*>(c); }, nullptr, nullptr);
                hb_draw_funcs_set_cubic_to_func(funcs,
                    [](hb_draw_funcs_t*, void* c, hb_draw_state_t*, float, float, float, float, float, float, void*) { ++*static_cast<unsigned*>(c); }, nullptr, nullptr);
                hb_font_draw_glyph(font, gid, funcs, &segments);
                hb_draw_funcs_destroy(funcs);

                printf("[Fallback]   face[%u]: %u glyphs, U+4E00 -> %s (gid %u); "
                       "glyf %u, loca %u, CFF %u, CFF2 %u; draw segments %u\n",
                       i, hb_face_get_glyph_count(face), mapped ? "mapped" : "UNMAPPED", gid,
                       tableSize(HB_TAG('g','l','y','f')), tableSize(HB_TAG('l','o','c','a')),
                       tableSize(HB_TAG('C','F','F',' ')), tableSize(HB_TAG('C','F','F','2')),
                       segments);
                hb_font_destroy(font);
                hb_face_destroy(face);
            }
            hb_blob_destroy(blob);
        }
    }

    // 40 distinct CJK codepoints.
    std::string many;
    for (uint32_t cp = 0x4E00; cp < 0x4E00 + 40; ++cp)
    {
        many += char(0xE0 | (cp >> 12));
        many += char(0x80 | ((cp >> 6) & 0x3F));
        many += char(0x80 | (cp & 0x3F));
    }

    const auto extent = format.getTextExtentU(many);
    EXPECT_GT(extent.width, 0.0f) << "no CJK advances";

    auto rt = ctx.makeTarget(700, 40);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU(many, format, { 2.f, 2.f, 698.f, 38.f }, black);
    rt.endDraw();
    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.01f);
}

TEST(CpuText, WrapNeverSplitsAMultiGlyphCluster)
{
    // A cluster can shape to several glyphs (base plus mark), and they all
    // carry the same source byte. Testing the byte alone let a break land
    // between them, splitting a grapheme.
    TextContext ctx;
    auto format = ctx.makeFormat(18.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    // Several accented characters, each a base plus a combining mark.
    const char* accented = "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81";
    const auto natural = format.getTextExtentU(accented, 100000.0f);
    ASSERT_GT(natural.width, 0.0f);

    // Squeeze hard. There are no spaces, so there is nowhere legal to break:
    // the text must stay on one line rather than splitting mid-cluster.
    const auto squeezed = format.getTextExtentU(accented, natural.width * 0.3f);
    EXPECT_NEAR(squeezed.height, natural.height, 0.01f)
        << "a grapheme cluster was split across lines";
}

TEST(CpuText, TextUnderTransformAndClip)
{
    // Text had no coverage at all under a transform or a clip.
    TextContext ctx;
    auto format = ctx.makeFormat(18.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto rt = ctx.makeTarget(96, 96);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);

    const auto m = Matrix3x2{ 1.4f, 0.0f, 0.0f, 1.4f, 6.0f, 6.0f };
    AccessPtr::get(rt)->setTransform(&m);
    rt.drawTextU("Wg", format, { 0.f, 0.f, 60.f, 30.f }, black);
    const Matrix3x2 identity;
    AccessPtr::get(rt)->setTransform(&identity);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    const auto scaled = inkBounds(bmp);
    ASSERT_TRUE(scaled.any()) << "transformed text did not render";
    EXPECT_GE(scaled.left, 5) << "text ignored the transform's translation";

    // Same text, clipped to a band that must cut it.
    auto rt2 = ctx.makeTarget(96, 96);
    rt2.beginDraw();
    rt2.clear(Colors::White);
    auto black2 = rt2.createSolidColorBrush(Colors::Black);
    rt2.pushAxisAlignedClip({ 0.f, 0.f, 96.f, 10.f });
    rt2.drawTextU("Wg", format, { 2.f, 2.f, 90.f, 40.f }, black2);
    rt2.popAxisAlignedClip();
    rt2.endDraw();

    auto bmp2 = rt2.getBitmap();
    const auto clipped = inkBounds(bmp2);
    if (clipped.any())
        EXPECT_LT(clipped.bottom, 10) << "text drew outside the clip";
}

TEST(CpuText, DrawTextOptionsClipConfinesToLayoutRect)
{
    // DrawTextOptions::Clip was accepted and ignored, so overlong text spilled
    // out of its layout rectangle on this backend but not on Direct2D.
    TextContext ctx;
    auto format = ctx.makeFormat(22.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::NoWrap);

    const gmpi::drawing::Rect layout{ 4.f, 4.f, 40.f, 40.f };
    const char* overflowing = "MMMMMMMMMMMM";

    const auto render = [&](int32_t options) {
        auto rt = ctx.makeTarget(96, 48);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto black = rt.createSolidColorBrush(Colors::Black);
        AccessPtr::get(rt)->drawTextU(overflowing, uint32_t(std::char_traits<char>::length(overflowing)),
                                      AccessPtr::get(format), &layout, AccessPtr::get(black), options);
        rt.endDraw();
        auto bmp = rt.getBitmap();
        return inkBounds(bmp);
    };

    const auto unclipped = render(DrawTextOptions::None);
    ASSERT_TRUE(unclipped.any());
    EXPECT_GT(unclipped.right, int(layout.right))
        << "the sample text must overflow, or this test proves nothing";

    const auto clipped = render(DrawTextOptions::Clip);
    ASSERT_TRUE(clipped.any()) << "clipping removed everything";
    EXPECT_LE(clipped.right, int(layout.right))
        << "text drew past the right edge of its layout rect despite Clip";
}

TEST(CpuText, DegenerateLayoutInputs)
{
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    AccessPtr::get(format)->setWordWrapping(WordWrapping::Wrap);

    // None of these may hang, crash, or report nonsense.
    EXPECT_NEAR(format.getTextExtentU("").width, 0.0f, 0.01f);
    EXPECT_GE(format.getTextExtentU("   ").width, 0.0f);
    EXPECT_GT(format.getTextExtentU("\n\n\n").height, 0.0f);
    EXPECT_GT(format.getTextExtentU("word", 0.0f).width, 0.0f);      // zero limit
    EXPECT_GT(format.getTextExtentU("word", -5.0f).width, 0.0f);     // negative limit
    EXPECT_GT(format.getTextExtentU("word", 0.001f).width, 0.0f);    // absurdly tight

    auto rt = ctx.makeTarget(32, 32);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("", format, { 0.f, 0.f, 32.f, 32.f }, black);       // empty draw

    // A null brush can only be reached through the native interface, since the
    // wrapper takes a reference. It must decline rather than crash.
    const gmpi::drawing::Rect layout{ 0.f, 0.f, 32.f, 32.f };
    AccessPtr::get(rt)->drawTextU("x", 1, AccessPtr::get(format), &layout, nullptr, 0);
    rt.endDraw();
    SUCCEED();
}

// --- Glyph atlas -----------------------------------------------------------

TEST(CpuText, GlyphAtlasMatchesTheGeometryPath)
{
    // The atlas must be an optimisation, not a rendering change. Same scene
    // both ways, compared pixel by pixel.
    TextContext ctx;
    const char* sample = "Hamburgefonstiv 0123";

    const auto render = [&](bool atlas) {
        ctx.engine.useGlyphAtlas = atlas;
        auto format = ctx.makeFormat(17.0f);
        auto rt = ctx.makeTarget(220, 40);
        rt.beginDraw();
        rt.clear(Colors::White);
        auto black = rt.createSolidColorBrush(Colors::Black);
        // Fractional origin, so sub-pixel positioning is exercised.
        rt.drawTextU(sample, format, { 3.4f, 2.7f, 218.f, 38.f }, black);
        rt.endDraw();
        auto bmp = rt.getBitmap();
        auto px = bmp.lockPixels(BitmapLockFlags::Read);
        std::vector<float> out(220 * 40);
        for (int y = 0; y < 40; ++y)
        {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(px.getAddress() + size_t(y) * px.getBytesPerRow());
            for (int x = 0; x < 220; ++x)
                out[size_t(y) * 220 + x] = detail::halfToFloat(row[x * 4]);
        }
        return out;
    };

    const auto viaGeometry = render(false);
    const auto viaAtlas = render(true);
    ASSERT_EQ(viaGeometry.size(), viaAtlas.size());

    double worst = 0.0, total = 0.0;
    int differing = 0;
    for (size_t i = 0; i < viaAtlas.size(); ++i)
    {
        const double d = std::fabs(double(viaAtlas[i]) - double(viaGeometry[i]));
        worst = (std::max)(worst, d);
        if (d > 0.004) { ++differing; total += d; }
    }
    std::cout << "  [ATLAS] differing=" << differing << "/" << viaAtlas.size()
              << " worst=" << worst
              << " mean(differing)=" << (differing ? total / differing : 0.0) << "\n";

    // Not bit-identical by construction: the atlas quantises the glyph origin
    // to quarter pixels (as FreeType and Skia do) while the geometry path uses
    // the exact fraction, so origins can differ by up to an eighth of a pixel.
    // What matters is that the difference stays at antialiasing level and is
    // confined to glyph edges.
    //
    // Measured: worst 0.12, mean over differing pixels 0.05, about 9% of the
    // image differing (roughly the proportion that is glyph edge). A whole-
    // pixel positioning error — which an earlier version of the sub-pixel
    // split produced by dropping the rounding carry — showed up here as worst
    // 0.95 and mean 0.13, so these limits do catch it.
    // The atlas now merges every glyph of the call into ONE coverage buffer
    // and blends once - the same one-pass compositing the geometry path gets
    // from its winding fill - so the old per-glyph double-blend at
    // overlapping edges is gone (that flaw once pushed this limit to 0.28).
    //
    // What remains is sub-pixel QUANTISATION: the atlas snaps positions to a
    // 1/4-pixel grid (kSubPixelSteps), the geometry path rasterises at the
    // exact position, and a 1/8-pixel edge shift becomes a coverage change
    // that the gamma-space blend amplifies at mid tones. Measured worst
    // 0.224, mean over differing pixels 0.043 - the worst limit sits just
    // above the quantisation floor, and the mean is the tight one: the
    // per-glyph double-blend raised the MEAN, a whole-pixel positioning bug
    // raises it to 0.13, and both must stay caught.
    EXPECT_LT(worst, 0.24) << "a pixel changed far more than sub-pixel quantisation explains";
    EXPECT_LT(total / (differing ? differing : 1), 0.05) << "differing pixels are too wrong";
    EXPECT_LT(double(differing) / double(viaAtlas.size()), 0.15);
}

TEST(CpuText, GlyphAtlasCachesAndIsReused)
{
    TextContext ctx;
    auto format = ctx.makeFormat(15.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto rt = ctx.makeTarget(200, 40);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("aaaa", format, { 2.f, 2.f, 198.f, 38.f }, black);
    rt.endDraw();

    const auto afterFirst = ctx.engine.glyphAtlasSize();
    EXPECT_GT(afterFirst, 0u) << "nothing was cached";
    // Four identical glyphs at the same size: at most one mask per sub-pixel
    // position, not one per glyph occurrence.
    EXPECT_LE(afterFirst, 4u);

    // Redrawing the same string must not grow the cache.
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black2 = rt.createSolidColorBrush(Colors::Black);
    rt.drawTextU("aaaa", format, { 2.f, 2.f, 198.f, 38.f }, black2);
    rt.endDraw();
    EXPECT_EQ(ctx.engine.glyphAtlasSize(), afterFirst) << "cache missed on a repeat draw";
}

TEST(CpuText, GlyphAtlasIsFaster)
{
    // The whole point of the atlas. Timing is reported rather than asserted
    // tightly, since a Debug build and a shared machine make exact ratios
    // meaningless — but a cache that made text SLOWER would be worth knowing.
    TextContext ctx;
    const char* line = "The quick brown fox jumps over the lazy dog 0123456789";

    const auto timeRedraws = [&](bool atlas) {
        ctx.engine.useGlyphAtlas = atlas;
        auto format = ctx.makeFormat(14.0f);
        auto rt = ctx.makeTarget(420, 30);
        // Warm up, so font loading and (for the atlas) mask building are not
        // counted as part of the steady-state cost.
        rt.beginDraw();
        auto warm = rt.createSolidColorBrush(Colors::Black);
        rt.drawTextU(line, format, { 2.f, 2.f, 418.f, 28.f }, warm);
        rt.endDraw();

        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 40;
        for (int i = 0; i < kFrames; ++i)
        {
            rt.beginDraw();
            rt.clear(Colors::White);
            auto black = rt.createSolidColorBrush(Colors::Black);
            rt.drawTextU(line, format, { 2.f, 2.f, 418.f, 28.f }, black);
            rt.endDraw();
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
    };

    const double geometryMs = timeRedraws(false);
    const double atlasMs = timeRedraws(true);
    std::cout << "  [ATLAS PERF] geometry " << geometryMs << " ms/frame, atlas "
              << atlasMs << " ms/frame, speedup " << (geometryMs / atlasMs) << "x\n";

    EXPECT_LT(atlasMs, geometryMs * 1.2) << "the glyph cache made text slower";
}

TEST(CpuText, GlyphAtlasHandlesRotationByFallingBack)
{
    // A rotated transform cannot use cached masks; it must still draw.
    TextContext ctx;
    auto format = ctx.makeFormat(20.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    auto rt = ctx.makeTarget(96, 96);
    rt.beginDraw();
    rt.clear(Colors::White);
    auto black = rt.createSolidColorBrush(Colors::Black);
    const float c = std::cos(0.4f), s = std::sin(0.4f);
    const Matrix3x2 rotate{ c, s, -s, c, 20.0f, 20.0f };
    AccessPtr::get(rt)->setTransform(&rotate);
    rt.drawTextU("Rotated", format, { 0.f, 0.f, 90.f, 30.f }, black);
    const Matrix3x2 identity;
    AccessPtr::get(rt)->setTransform(&identity);
    rt.endDraw();

    auto bmp = rt.getBitmap();
    EXPECT_GT(inkFraction(bmp), 0.005f) << "rotated text did not render";
}

TEST(CpuText, MissingFontFailsAtTheInterface)
{
    TextContext ctx;
    gmpi::drawing::api::ITextFormat* raw{};
    const auto r = ctx.factoryImpl.createTextFormat("NoSuchFontExistsAnywhere_ZZZ", FontWeight::Regular,
                                                    FontStyle::Normal, FontStretch::Normal, 12.0f, 0, &raw);

#if GMPI_UI_FONT_PROVIDER_FONTCONFIG
    // fontconfig never fails a family lookup: an unknown name is substituted
    // with the best available match, by design. So the platform contract here
    // genuinely differs — what must hold is that the caller gets a USABLE
    // format rather than a half-built one.
    EXPECT_EQ(r, gmpi::ReturnCode::Ok);
    ASSERT_NE(raw, nullptr);
    raw->release();
#else
    // DirectWrite and CoreText both report failure for a family that does not
    // exist, which is what the wrapper's fallback chain is built on.
    EXPECT_NE(r, gmpi::ReturnCode::Ok);
    EXPECT_EQ(raw, nullptr);
#endif
}

TEST(CpuText, WrapperFallsBackToArial)
{
    // ...but the C++ wrapper deliberately walks its family list and then falls
    // back to Arial, so callers always get a usable format. Both behaviours
    // matter: the first is what the fallback is built on.
    TextContext ctx;
    std::string_view name{ "NoSuchFontExistsAnywhere_ZZZ" };
    auto format = ctx.factory.createTextFormat(20.0f, { &name, 1 });
    ASSERT_NE(AccessPtr::get(format), nullptr) << "wrapper should have fallen back";

    const auto metrics = format.getFontMetrics();
    EXPECT_GT(metrics.ascent, 0.0f);
    EXPECT_GT(metrics.descent, 0.0f);
    EXPECT_GT(format.getTextExtentU("Hi").width, 0.0f) << "the fallback format must be usable";
}

TEST(CpuText, WithoutTextEngineReportsNoSupport)
{
    // The backend has no text of its own; unwired it must decline, not crash.
    gmpi::cpugfx::Factory bare;
    gmpi::drawing::api::ITextFormat* raw{};
    EXPECT_EQ(bare.createTextFormat("Arial", FontWeight::Regular, FontStyle::Normal,
                                    FontStretch::Normal, 12.0f, 0, &raw),
              gmpi::ReturnCode::NoSupport);
    EXPECT_EQ(raw, nullptr);
}


// ---------------------------------------------------------------------------
// Retained text layouts (ITextLayout) on the CPU backend.
//
// drawTextU re-runs itemisation, line breaking and HarfBuzz shaping on every
// call; a retained layout does that once and keeps the result. Parity is
// structural — drawText is "lay out, then renderLaidOut", and the layout hands
// the same LaidOut to the same renderLaidOut — so these compare bytes and
// expect an exact match, which would catch any drift in that reasoning.
// ---------------------------------------------------------------------------
namespace
{
// Byte-for-byte comparison of two same-size render targets.
::testing::AssertionResult cpuPixelsIdentical(BitmapRenderTarget& a, BitmapRenderTarget& b)
{
    auto bitmapA = a.getBitmap();
    auto bitmapB = b.getBitmap();
    auto pixelsA = bitmapA.lockPixels(BitmapLockFlags::Read);
    auto pixelsB = bitmapB.lockPixels(BitmapLockFlags::Read);

    const auto size = bitmapA.getSize();
    const int32_t bpr = pixelsA.getBytesPerRow();
    if (bpr != pixelsB.getBytesPerRow())
        return ::testing::AssertionFailure() << "row-stride mismatch";

    const size_t total = size_t(bpr) * size_t(size.height);
    size_t differing = 0;
    for (size_t i = 0; i < total; ++i)
        if (pixelsA.getAddress()[i] != pixelsB.getAddress()[i])
            ++differing;

    if (differing)
        return ::testing::AssertionFailure() << differing << " of " << total << " bytes differ";

    return ::testing::AssertionSuccess();
}
} // namespace

// The parity contract: a run-free retained layout draws exactly what drawTextU
// draws for the same format over {point, point + box}.
TEST(CpuText, TextLayoutMatchesDrawText)
{
    TextContext ctx;
    auto format = ctx.makeFormat(14.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);

    // Qualified: macOS drags in the classic QuickDraw `Point` from MacTypes.h,
    // which is ambiguous with gmpi::drawing::Point under the using-directive.
    constexpr gmpi::drawing::Point origin{ 2.f, 2.f };
    constexpr float boxW = 92.f, boxH = 44.f;
    const std::string_view text = "Hello layout";

    auto layout = ctx.factory.createTextLayout(text, format, boxW, boxH);
    ASSERT_TRUE(layout) << "CPU backend should support retained layouts";

    auto rtA = ctx.makeTarget();
    rtA.beginDraw();
    rtA.clear(Colors::White);
    auto brushA = rtA.createSolidColorBrush(Colors::Black);
    rtA.drawTextU(text, format, { origin.x, origin.y, origin.x + boxW, origin.y + boxH }, brushA);
    rtA.endDraw();

    auto rtB = ctx.makeTarget();
    rtB.beginDraw();
    rtB.clear(Colors::White);
    auto brushB = rtB.createSolidColorBrush(Colors::Black);
    rtB.drawTextLayout(layout, origin, brushB);
    rtB.endDraw();

    EXPECT_TRUE(cpuPixelsIdentical(rtA, rtB));
}

// The same, with the settings SynthEdit's pin columns use: trailing alignment
// and uniform line spacing over a multi-line string including a blank line.
TEST(CpuText, TextLayoutMatchesDrawTextTrailingMultiline)
{
    TextContext ctx;
    auto format = ctx.makeFormat(10.0f);
    ASSERT_NE(AccessPtr::get(format), nullptr);
    format.setTextAlignment(TextAlignment::Trailing);
    format.setLineSpacing(12.f, 10.f);

    constexpr gmpi::drawing::Point origin{ 3.f, 1.f };
    constexpr float boxW = 90.f, boxH = 46.f;
    const std::string_view text = "Pitch\nGate\n\nLevel";

    auto layout = ctx.factory.createTextLayout(text, format, boxW, boxH);
    ASSERT_TRUE(layout);

    auto rtA = ctx.makeTarget();
    rtA.beginDraw();
    rtA.clear(Colors::White);
    auto brushA = rtA.createSolidColorBrush(Colors::Black);
    rtA.drawTextU(text, format, { origin.x, origin.y, origin.x + boxW, origin.y + boxH }, brushA);
    rtA.endDraw();

    auto rtB = ctx.makeTarget();
    rtB.beginDraw();
    rtB.clear(Colors::White);
    auto brushB = rtB.createSolidColorBrush(Colors::Black);
    rtB.drawTextLayout(layout, origin, brushB);
    rtB.endDraw();

    EXPECT_TRUE(cpuPixelsIdentical(rtA, rtB));
}

// The extent is measured once at creation and agrees with the format.
TEST(CpuText, TextLayoutExtentMatchesFormat)
{
    TextContext ctx;
    auto format = ctx.makeFormat(16.0f);
    const std::string_view text = "Extent";
    constexpr float boxW = 200.f;

    auto layout = ctx.factory.createTextLayout(text, format, boxW, 100.f);
    ASSERT_TRUE(layout);

    const auto fromFormat = format.getTextExtentU(text, boxW);
    const auto fromLayout = layout.getTextExtentU();

    EXPECT_FLOAT_EQ(fromFormat.width, fromLayout.width);
    EXPECT_FLOAT_EQ(fromFormat.height, fromLayout.height);
}

// Styling runs are declined for now (no per-run colour in the rasteriser), and
// declining must be a clean null rather than a broken layout.
TEST(CpuText, TextLayoutDeclinesStyleRuns)
{
    TextContext ctx;
    auto format = ctx.makeFormat(14.0f);

    TextStyleRun run{};
    run.begin = 0;
    run.length = 3;
    run.flags = TextStyleFlags::HasFontWeight;
    run.fontWeight = FontWeight::Bold;

    EXPECT_FALSE(ctx.factory.createTextLayout("Styled", format, 90.f, 40.f, { &run, 1 }));
}

// ---------------------------------------------------------------------------
// Text-weight sample sheet: SE-realistic text at several sizes, dark-on-light
// and light-on-dark, saved as a preview PNG. Its companion
// DrawingTest.TextWeightSampleSheetReference renders the identical sheet with
// the platform backend, so the two stack pixel-for-pixel to eyeball how the
// linear blend + coverage warp tracks Direct2D across sizes and polarities.
// Not a correctness test — it asserts nothing beyond "it drew".
// ---------------------------------------------------------------------------
TEST(CpuText, TextWeightSampleSheet)
{
    TextContext ctx;

    constexpr uint32_t W = 600, H = 176;
    auto rt = ctx.factory.createCpuRenderTarget({ W, H }, 0); // fp16, like the swapchain
    rt.beginDraw();

    // Left half: SE module-body light grey. Right half: a dark UI panel.
    auto lightBg = rt.createSolidColorBrush(colorFromHex(0xE0E0E0u));
    auto darkBg = rt.createSolidColorBrush(colorFromHex(0x252525u));
    rt.fillRectangle({ 0.f, 0.f, W / 2.f, float(H) }, lightBg);
    rt.fillRectangle({ W / 2.f, 0.f, float(W), float(H) }, darkBg);

    auto black = rt.createSolidColorBrush(Colors::Black);
    auto white = rt.createSolidColorBrush(Colors::White);

    const float sizes[] = { 10.f, 12.f, 16.f, 24.f };
    const char* sample = "Pitch Gate 1 Pole LP mg";

    float y = 6.f;
    for (const float size : sizes)
    {
        auto tf = ctx.makeFormat(size);
        AccessPtr::get(tf)->setWordWrapping(WordWrapping::NoWrap);
        rt.drawTextU(sample, tf, { 8.f, y, W / 2.f - 4.f, y + size * 2.f }, black);
        rt.drawTextU(sample, tf, { W / 2.f + 8.f, y, float(W), y + size * 2.f }, white);
        y += size * 1.6f + 8.f;
    }

    {
        auto tf = ctx.makeFormat(12.f);
        AccessPtr::get(tf)->setWordWrapping(WordWrapping::NoWrap);
        const char* label = "CPU linear + luminance-keyed coverage warp";
        rt.drawTextU(label, tf, { 8.f, y, W / 2.f - 4.f, y + 24.f }, black);
        rt.drawTextU(label, tf, { W / 2.f + 8.f, y, float(W), y + 24.f }, white);
    }

    rt.endDraw();

    auto bmp = rt.getBitmap();
    const auto outPath = std::filesystem::path(REFERENCE_IMAGES_DIR) / "cpu_backend_preview"
        / "text_weight_cpu.png";
    savePng(outPath, bmp);
    printf("[TextWeightSheet] wrote %s\n", outPath.string().c_str());

    EXPECT_GT(inkFraction(bmp), 0.001f);
}

#endif // GMPI_UI_HAVE_FONT_PROVIDER

