// Text tests for the pure-software CPU backend (helpers/CpuTextEngine.h).
//
// Text is compared differently from everything else. Metrics and layout —
// extents, line counts, alignment positions — are asserted, because hosts
// position UI off them and they must agree with the platform backends. Pixels
// are not: HarfBuzz outlines rasterized here and DirectWrite's own rasterizer
// legitimately differ, which is why the golden-image fixture keeps per-platform
// text references.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "Drawing.h"
#include "backends/CpuGfx.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/FontProvider.h"
#include "helpers/SavePng.h"

#ifdef _WIN32
#include <objbase.h>
#endif

using namespace gmpi::drawing;

#if GMPI_UI_HAVE_FONT_PROVIDER

namespace
{

// A font every platform in CI actually has. Arial on Windows/macOS;
// fontconfig substitutes Liberation Sans on most Linux boxes, whose metrics
// differ — which is exactly why pixel comparisons are off the table.
constexpr const char* kTestFont = "Arial";

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

    // BodyHeight is the default: the requested height IS ascent+descent.
    EXPECT_NEAR(metrics.ascent + metrics.descent, 20.0f, 0.1f)
        << "FontFlags::BodyHeight must rescale so the body height matches the request";
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

    const auto one = format.getTextExtentU("one");
    const auto three = format.getTextExtentU("one\ntwo\nthree");
    EXPECT_NEAR(three.height, one.height * 3.0f, 0.1f);
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
    const Rect layout{ 0.f, 0.f, 96.f, 48.f };

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
    // Arial has no CJK. Without fallback these become .notdef boxes or nothing;
    // with it, a covering font is found automatically.
    TextContext ctx;
    auto format = ctx.makeFormat(24.0f, "Arial");
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
    auto format = ctx.makeFormat(20.0f, "Arial");
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

TEST(CpuText, MissingFontFailsAtTheInterface)
{
    // The native interface reports failure for a family that does not exist...
    TextContext ctx;
    gmpi::drawing::api::ITextFormat* raw{};
    const auto r = ctx.factoryImpl.createTextFormat("NoSuchFontExistsAnywhere_ZZZ", FontWeight::Regular,
                                                    FontStyle::Normal, FontStretch::Normal, 12.0f, 0, &raw);
    EXPECT_NE(r, gmpi::ReturnCode::Ok);
    EXPECT_EQ(raw, nullptr);
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
    EXPECT_NEAR(metrics.ascent + metrics.descent, 20.0f, 0.1f);
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

#endif // GMPI_UI_HAVE_FONT_PROVIDER
