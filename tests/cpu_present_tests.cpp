// Tests for the present path's encode step (backends/CpuEncode.h): fp16 linear
// scRGB -> dithered 8-bit sRGB.
//
// This is the one part of milestone 7 that needs no screen, no window system and
// no host, so it is built and proven first and it is what stays behind as a
// permanent regression suite. Everything downstream (the X11 blit, the frame)
// can then be tested by comparing a screen grab against THIS, which is already
// known correct.
//
// No golden images here on purpose. Every property below is either exact
// arithmetic or a measured inequality, so the assertions say what is true rather
// than "matches the picture we captured last time".

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <random>
#include <vector>

#include "Drawing.h"
#include "backends/CpuEncode.h"
#include "helpers/SavePng.h" // linearToSRGB_f, the reference curve

using namespace gmpi::drawing;
using gmpi::cpugfx::DestSurface;
using gmpi::cpugfx::PixelEncoding;
using gmpi::cpugfx::SourceSurface;
using gmpi::cpugfx::encodeDirtyRect;
using gmpi::cpugfx::kDitherTile;

namespace
{
constexpr int kTile = 64;

// An fp16 RGBA surface with the same row padding backends/CpuGfx.h uses, so the
// tests exercise the stride arithmetic rather than a tidy width*4 case.
struct TestSurface
{
    std::vector<uint16_t> storage;
    int32_t width{}, height{}, stridePixels{};

    TestSurface(int32_t w, int32_t h) : width(w), height(h)
    {
        stridePixels = (w + 7) & ~7; // matches Surface::init
        storage.assign(size_t(stridePixels) * h * 4, 0);
    }

    void set(int32_t x, int32_t y, float r, float g, float b, float a = 1.0f)
    {
        uint16_t* p = storage.data() + (size_t(y) * stridePixels + size_t(x)) * 4;
        p[0] = detail::floatToHalf(r);
        p[1] = detail::floatToHalf(g);
        p[2] = detail::floatToHalf(b);
        p[3] = detail::floatToHalf(a);
    }

    void fill(float r, float g, float b, float a = 1.0f)
    {
        for (int32_t y = 0; y < height; ++y)
            for (int32_t x = 0; x < width; ++x)
                set(x, y, r, g, b, a);
    }

    SourceSurface view() const { return { storage.data(), stridePixels, width, height }; }
};

struct TestDest
{
    std::vector<uint8_t> storage;
    int32_t width{}, height{}, bytesPerRow{};

    TestDest(int32_t w, int32_t h, int32_t extraRowBytes = 0, uint8_t fillByte = 0)
        : width(w), height(h), bytesPerRow(w * 4 + extraRowBytes)
    {
        storage.assign(size_t(bytesPerRow) * h, fillByte);
    }

    DestSurface view(PixelEncoding e = PixelEncoding::Bgra8888)
    {
        return { storage.data(), bytesPerRow, width, height, e };
    }

    // Blue channel of a BGRA pixel.
    uint8_t b(int32_t x, int32_t y) const { return storage[size_t(y) * bytesPerRow + size_t(x) * 4 + 0]; }
    uint8_t g(int32_t x, int32_t y) const { return storage[size_t(y) * bytesPerRow + size_t(x) * 4 + 1]; }
    uint8_t r(int32_t x, int32_t y) const { return storage[size_t(y) * bytesPerRow + size_t(x) * 4 + 2]; }
    uint8_t a(int32_t x, int32_t y) const { return storage[size_t(y) * bytesPerRow + size_t(x) * 4 + 3]; }
};

// sRGB code -> linear, so a ramp can be specified in the space a designer thinks
// in while being interpolated in the space the renderer works in.
float srgbCodeToLinear(float code)
{
    const float s = code / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

// Longest run of identical values — the direct measure of a visible band.
int longestRun(const std::vector<int>& v)
{
    int best = 0, run = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        run = (i && v[i] == v[i - 1]) ? run + 1 : 1;
        best = (std::max)(best, run);
    }
    return best;
}
} // namespace

// ---------------------------------------------------------------------------
// The dither tile
// ---------------------------------------------------------------------------

// Everything else rests on this. If the histogram is not exactly uniform, the
// encoder's average is merely approximate and BlockMeanIsExact becomes a lie.
TEST(CpuPresent, DitherTileHistogramIsExactlyUniform)
{
    std::map<int, int> hist;
    for (int i = 0; i < kTile * kTile; ++i)
        ++hist[kDitherTile[i]];

    ASSERT_EQ(hist.size(), 256u) << "tile does not use all 256 threshold values";
    for (const auto& [value, count] : hist)
        EXPECT_EQ(count, 16) << "value " << value << " appears " << count << " times, not 16";
}

// Blue noise, not just a shuffled histogram: its energy must sit at high spatial
// frequencies. Measured as the variance of 8x8 block means against a white-noise
// shuffle of the identical histogram, so the comparison isolates the SPATIAL
// arrangement and nothing else.
TEST(CpuPresent, DitherTileIsBlueNotWhite)
{
    auto blockMeanVariance = [](const std::vector<int>& v)
    {
        std::vector<double> means;
        for (int by = 0; by < kTile; by += 8)
            for (int bx = 0; bx < kTile; bx += 8)
            {
                double s = 0;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        s += v[size_t(by + y) * kTile + size_t(bx + x)];
                means.push_back(s / 64.0);
            }
        const double m = std::accumulate(means.begin(), means.end(), 0.0) / means.size();
        double var = 0;
        for (double v2 : means) var += (v2 - m) * (v2 - m);
        return var / means.size();
    };

    std::vector<int> blue(kDitherTile, kDitherTile + kTile * kTile);
    std::vector<int> white = blue;
    std::mt19937 rng(12345);
    std::shuffle(white.begin(), white.end(), rng);

    const double blueVar = blockMeanVariance(blue);
    const double whiteVar = blockMeanVariance(white);
    std::cout << "  [DITHER] 8x8 block-mean variance: blue " << blueVar
              << " white " << whiteVar << " ratio " << (blueVar / whiteVar) << "\n";

    EXPECT_LT(blueVar / whiteVar, 0.25)
        << "tile has too much low-frequency energy to be blue noise";
}

// ---------------------------------------------------------------------------
// The sRGB table
// ---------------------------------------------------------------------------

// Exhaustive over every in-range half pattern, not a sample: the table either IS
// the curve in 8.8 fixed point or it is not.
//
// Note what is NOT asserted: that rounding the table to 8 bits always equals
// helpers/SavePng.h's rounded byte. It cannot, and should not. The table holds
// 8.8, so a value like half pattern 0x8f5 (linear 1.513e-4, i.e. 0.4984 codes)
// stores 128 — an exact half — and rounding THAT gives 1 where rounding the
// original 0.4984 gives 0. That is double rounding, and it is harmless here
// precisely because the screen path never rounds: it adds a dither threshold
// and shifts, so 0.4984 codes comes out as 1 in half the pixels and 0 in the
// other half, whose mean (0.5) is nearer the truth than the PNG path's 0.
// The bound below is the property that actually matters.
TEST(CpuPresent, EncodeTableMatchesTheReferenceCurve)
{
    const uint16_t* table = gmpi::cpugfx::srgbTable();
    int checked = 0, differsFromPng = 0;
    double worstError = 0.0;

    for (uint32_t bits = 0; bits < gmpi::cpugfx::kSrgbTableSize; ++bits)
    {
        const float linear = detail::halfToFloat(uint16_t(bits));
        const double exact = linearToSRGB01(linear) * 255.0;

        // The table IS the curve, to the last bit of 8.8 fixed point.
        ASSERT_EQ(table[bits], uint16_t(exact * 256.0 + 0.5))
            << "half pattern 0x" << std::hex << bits << std::dec << " linear " << linear;

        worstError = (std::max)(worstError, std::abs(table[bits] / 256.0 - exact));

        // Never more than one code from what the PNG encoder would write, and
        // only ever at an exact-half boundary.
        const int pngCode = detail::linearToSRGB_f(linear);
        const int rounded = int((table[bits] + 128) >> 8);
        ASSERT_LE(std::abs(rounded - pngCode), 1)
            << "screen and PNG encoders disagree by more than a code at 0x"
            << std::hex << bits;
        if (rounded != pngCode)
            ++differsFromPng;
        ++checked;
    }

    std::cout << "  [TABLE] " << checked << " entries, worst fixed-point error "
              << worstError << " codes, " << differsFromPng
              << " differ from the PNG rounding by 1\n";

    EXPECT_EQ(checked, 15361) << "expected every half pattern in [0,1]";
    EXPECT_LE(worstError, 0.5 / 256.0 + 1e-9) << "table is not the curve to 8.8 precision";
    EXPECT_LT(differsFromPng, checked / 100) << "far more half-boundary cases than expected";
}

TEST(CpuPresent, EncodeFoldsOutOfRangeInputs)
{
    const uint16_t* t = gmpi::cpugfx::srgbTable();
    EXPECT_EQ(gmpi::cpugfx::srgbFixed(t, detail::floatToHalf(-1.0f)), 0u) << "negative must clamp to black";
    EXPECT_EQ(gmpi::cpugfx::srgbFixed(t, 0x8000u), 0u) << "-0.0 must clamp to black";
    EXPECT_EQ(gmpi::cpugfx::srgbFixed(t, detail::floatToHalf(2.0f)), 255u << 8) << "above 1.0 must clamp to white";
    EXPECT_EQ(gmpi::cpugfx::srgbFixed(t, 0x7C00u), 255u << 8) << "+inf must be bounded, not read past the table";
    EXPECT_EQ(gmpi::cpugfx::srgbFixed(t, 0x7E00u), 255u << 8) << "NaN must be bounded, not read past the table";
}

// ---------------------------------------------------------------------------
// The property that justifies the whole design
// ---------------------------------------------------------------------------

// For a CONSTANT input, the mean over an aligned 64x64 block is the true value
// with ZERO error — not small error. It follows from the tile histogram: writing
// the fixed-point value as 256q + r, exactly r of the 256 thresholds push a
// pixel up to q+1, so the block sum is exactly 16*(256q + r).
//
// This is what makes dithering an unbiased operation rather than a smudge, and
// it is why a flat fill cannot shift colour.
TEST(CpuPresent, BlockMeanIsExactForConstantInput)
{
    const uint16_t* table = gmpi::cpugfx::srgbTable();

    // Sweep values that land at, near and exactly halfway between output codes.
    for (const float value : { 0.0f, 0.001f, 0.02f, 0.05f, 0.1f, 0.25f, 0.4f, 0.5f, 0.73f, 0.9f, 1.0f })
    {
        TestSurface src(kTile, kTile);
        src.fill(value, value, value);
        TestDest dst(kTile, kTile);

        encodeDirtyRect(src.view(), dst.view(), { 0, 0, kTile, kTile });

        int64_t sum = 0;
        for (int y = 0; y < kTile; ++y)
            for (int x = 0; x < kTile; ++x)
                sum += dst.r(x, y);

        const uint32_t fixed = gmpi::cpugfx::srgbFixed(table, detail::floatToHalf(value));
        EXPECT_EQ(sum, int64_t(16) * int64_t(fixed))
            << "block sum is not exact for constant " << value
            << " (fixed " << fixed << ")";
    }
}

// ---------------------------------------------------------------------------
// Banding, both sides of the claim
// ---------------------------------------------------------------------------

// First half: prove the measurement can SEE banding, by measuring the
// undithered encode. Without this, the dithered assertion below could pass
// against a rig that cannot detect the problem at all.
TEST(CpuPresent, UnditheredSubtleRampBands)
{
    constexpr int W = 400;
    const float lo = srgbCodeToLinear(40.0f), hi = srgbCodeToLinear(60.0f);

    std::vector<int> codes;
    codes.reserve(W);
    for (int x = 0; x < W; ++x)
        codes.push_back(detail::linearToSRGB_f(lo + (hi - lo) * x / (W - 1)));

    const int distinct = int(std::set<int>(codes.begin(), codes.end()).size());
    const int run = longestRun(codes);
    std::cout << "  [RAMP] undithered: " << distinct << " distinct codes, longest run " << run << " px\n";

    EXPECT_LE(distinct, 40) << "expected a coarsely quantised ramp";
    EXPECT_GE(run, 12) << "expected visible flat bands; if this fails the rig cannot see banding "
                          "and DitheredRampIsFlat proves nothing";
}

// Second half: the dithered encode of the same ramp has no flat bands left, and
// — the part that matters — its local mean still tracks the true value. Dither
// that removed banding by shifting the colour would be worse than banding.
TEST(CpuPresent, DitheredRampIsFlatAndUnbiased)
{
    constexpr int W = 384; // 6 whole tiles
    constexpr int H = kTile;
    const float lo = srgbCodeToLinear(40.0f), hi = srgbCodeToLinear(60.0f);

    TestSurface src(W, H);
    std::vector<float> truth(W);
    for (int x = 0; x < W; ++x)
    {
        const float v = lo + (hi - lo) * x / (W - 1);
        truth[x] = v;
        for (int y = 0; y < H; ++y)
            src.set(x, y, v, v, v);
    }

    TestDest dst(W, H);
    encodeDirtyRect(src.view(), dst.view(), { 0, 0, W, H });

    // A band is a 2D artifact: a region where every row changes code at the same
    // x. So the measure is not one row's longest run — it is whether the code
    // boundaries line up VERTICALLY. Undithered, every row is identical and the
    // edges are perfectly aligned columns; dithered, each row breaks at
    // different places.
    //
    // Quantified as: for each column, how many of the H rows differ from their
    // left neighbour. Undithered that is 0 or H (an edge column). Dithered it
    // should be spread, so almost no column is unanimous.
    int unanimousColumns = 0, edgeColumns = 0;
    for (int x = 1; x < W; ++x)
    {
        int changed = 0;
        for (int y = 0; y < H; ++y)
            changed += (dst.r(x, y) != dst.r(x - 1, y)) ? 1 : 0;
        if (changed == H) ++unanimousColumns; // a hard edge across the full height
        if (changed > 0) ++edgeColumns;
    }
    std::cout << "  [RAMP] dithered:   " << unanimousColumns << " full-height edges, "
              << edgeColumns << "/" << (W - 1) << " columns with any change\n";

    EXPECT_EQ(unanimousColumns, 0)
        << "a code boundary runs the full height — that is a visible band edge";
    EXPECT_GT(edgeColumns, (W - 1) * 3 / 4)
        << "code changes are concentrated in a few columns rather than scattered";

    // Unbiased: per aligned 64x64 block, the mean must match the mean of the
    // exact (unquantised) values.
    double worst = 0.0;
    for (int bx = 0; bx < W; bx += kTile)
    {
        double got = 0.0, want = 0.0;
        for (int y = 0; y < kTile; ++y)
            for (int x = bx; x < bx + kTile; ++x)
            {
                got += dst.r(x, y);
                want += linearToSRGB01(truth[x]) * 255.0;
            }
        got /= (kTile * kTile);
        want /= (kTile * kTile);
        worst = (std::max)(worst, std::abs(got - want));
    }
    std::cout << "  [RAMP] worst block-mean error: " << worst << " codes\n";
    EXPECT_LT(worst, 0.05) << "dither is biased — it shifted the colour, not just the noise";
}

// ---------------------------------------------------------------------------
// The headline invariant
// ---------------------------------------------------------------------------

// Repainting in pieces must produce byte-identical output to repainting whole.
// The dither tile is therefore indexed by ABSOLUTE destination coordinates; index
// it relative to the rectangle and every partial redraw leaves a seam where the
// pattern jumps.
//
// Deliberately awkward geometry: width 61 is not a multiple of 8, so the source
// row padding differs from the width, and the sub-rectangles are shuffled and
// aligned to nothing.
TEST(CpuPresent, DirtyRectInvariance)
{
    constexpr int W = 61, H = 37;

    TestSurface src(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            src.set(x, y, 0.03f + 0.004f * x, 0.05f + 0.003f * y, 0.2f);

    TestDest whole(W, H, /*extraRowBytes*/ 12, 0xCD);
    encodeDirtyRect(src.view(), whole.view(), { 0, 0, W, H });

    // Same surface, encoded as a shuffled partition of column strips.
    TestDest pieces(W, H, 12, 0xCD);
    std::vector<std::pair<int, int>> strips{ { 0, 1 }, { 1, 6 }, { 6, 56 }, { 56, 60 }, { 60, 61 } };
    std::mt19937 rng(7);
    std::shuffle(strips.begin(), strips.end(), rng);
    for (const auto& [x0, x1] : strips)
        encodeDirtyRect(src.view(), pieces.view(), { x0, 0, x1, H });

    ASSERT_EQ(whole.storage.size(), pieces.storage.size());
    EXPECT_EQ(std::memcmp(whole.storage.data(), pieces.storage.data(), whole.storage.size()), 0)
        << "piecewise encode differs from whole-surface encode — the dither is "
           "indexed relative to the rectangle, so partial redraws will seam";

    // Re-encoding an overlapping region must also be idempotent.
    encodeDirtyRect(src.view(), pieces.view(), { 3, 2, 40, 30 });
    EXPECT_EQ(std::memcmp(whole.storage.data(), pieces.storage.data(), whole.storage.size()), 0)
        << "re-encoding an overlapping rect changed the pixels";
}

// ---------------------------------------------------------------------------
// The two easy things to get backwards
// ---------------------------------------------------------------------------

// The destination is opaque, so a premultiplied source is composited over black:
// the stored value is used as-is. helpers/SavePng.h un-premultiplies because PNG
// carries alpha; copying that here would brighten every translucent pixel.
TEST(CpuPresent, AlphaIsCompositedNotUnpremultiplied)
{
    TestSurface src(kTile, kTile);
    src.fill(0.25f, 0.25f, 0.25f, 0.25f); // premultiplied: 25%-alpha white
    TestDest dst(kTile, kTile);
    encodeDirtyRect(src.view(), dst.view(), { 0, 0, kTile, kTile });

    const uint8_t want = detail::linearToSRGB_f(0.25f);
    std::cout << "  [ALPHA] got " << int(dst.r(0, 0)) << " expected ~" << int(want)
              << " (un-premultiplied would be 255)\n";
    for (int i = 0; i < 8; ++i)
        EXPECT_NEAR(dst.r(i, i), want, 1) << "25%-alpha white must stay dark, not become white";

    EXPECT_EQ(dst.a(0, 0), 0xFF) << "destination alpha byte must be defined and opaque";
}

// Source stride is padded to a multiple of 8 pixels; destination stride is
// whatever the window system says. At width 61 they differ, and confusing them
// shears the image.
TEST(CpuPresent, DestinationStrideIsIndependentOfSourceStride)
{
    constexpr int W = 61, H = 20;

    TestSurface src(W, H);
    ASSERT_NE(src.stridePixels, W) << "test needs a padded source to be meaningful";
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            src.set(x, y, (x == y) ? 1.0f : 0.0f, 0.0f, 0.0f);

    // Padded destination rows too, with a distinct fill so overruns are visible.
    TestDest dst(W, H, /*extraRowBytes*/ 37, 0xAB);
    encodeDirtyRect(src.view(), dst.view(), { 0, 0, W, H });

    for (int y = 0; y < H; ++y)
    {
        EXPECT_GT(dst.r(y, y), 250) << "diagonal lost at row " << y << " — strides confused";
        if (y + 1 < W)
            EXPECT_LT(dst.r(y + 1, y), 8) << "diagonal smeared at row " << y;

        // The row padding must be untouched.
        for (int i = W * 4; i < dst.bytesPerRow; ++i)
            ASSERT_EQ(dst.storage[size_t(y) * dst.bytesPerRow + i], 0xAB)
                << "wrote into destination row padding at row " << y;
    }
}

TEST(CpuPresent, ChannelOrderFollowsTheEncoding)
{
    TestSurface src(kTile, kTile);
    src.fill(1.0f, 0.0f, 0.0f); // pure red, premultiplied opaque

    TestDest bgra(kTile, kTile);
    encodeDirtyRect(src.view(), bgra.view(PixelEncoding::Bgra8888), { 0, 0, kTile, kTile });
    EXPECT_EQ(bgra.storage[2], 255) << "BGRA: red belongs in byte 2";
    EXPECT_EQ(bgra.storage[0], 0) << "BGRA: byte 0 is blue";

    TestDest rgba(kTile, kTile);
    encodeDirtyRect(src.view(), rgba.view(PixelEncoding::Rgba8888), { 0, 0, kTile, kTile });
    EXPECT_EQ(rgba.storage[0], 255) << "RGBA: red belongs in byte 0";
    EXPECT_EQ(rgba.storage[2], 0) << "RGBA: byte 2 is blue";
}

TEST(CpuPresent, DegenerateRectsAreIgnored)
{
    TestSurface src(kTile, kTile);
    src.fill(1.0f, 1.0f, 1.0f);
    TestDest dst(kTile, kTile, 0, 0x11);
    const auto pristine = dst.storage;

    for (const auto& r : { RectL{ 0, 0, 0, 0 }, RectL{ 10, 10, 5, 20 }, RectL{ 10, 10, 20, 5 },
                           RectL{ 1000, 1000, 2000, 2000 }, RectL{ -50, -50, -10, -10 } })
    {
        encodeDirtyRect(src.view(), dst.view(), r);
        EXPECT_EQ(dst.storage, pristine) << "a degenerate or off-surface rect wrote pixels";
    }

    // A rect that straddles the edge must clip, not overrun.
    encodeDirtyRect(src.view(), dst.view(), { -10, -10, kTile + 10, kTile + 10 });
    EXPECT_EQ(dst.r(0, 0), 255);
    EXPECT_EQ(dst.r(kTile - 1, kTile - 1), 255);
}
