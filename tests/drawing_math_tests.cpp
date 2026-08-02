// Tests for the pure maths helpers in Drawing.h. No render target and no
// backend, so these run on every platform.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "Drawing.h"

using namespace gmpi::drawing;

namespace
{
void expectMatrixNear(const Matrix3x2& got, const Matrix3x2& want, float tol = 1e-5f)
{
    EXPECT_NEAR(got._11, want._11, tol);
    EXPECT_NEAR(got._12, want._12, tol);
    EXPECT_NEAR(got._21, want._21, tol);
    EXPECT_NEAR(got._22, want._22, tol);
    EXPECT_NEAR(got._31, want._31, tol);
    EXPECT_NEAR(got._32, want._32, tol);
}

constexpr Matrix3x2 kIdentity{};
} // namespace

TEST(DrawingMath, InvertRoundTrips)
{
    const Matrix3x2 m{ 2.0f, 0.5f, -0.25f, 3.0f, 12.0f, -7.0f };
    const auto inv = invert(m);

    // m * inv should be the identity, and inverting twice returns the original.
    expectMatrixNear(m * inv, kIdentity, 1e-4f);
    expectMatrixNear(invert(inv), m, 1e-3f);

    // A point round-trips through the pair.
    const Point p{ 13.5f, -4.25f };
    const auto there = transformPoint(m, p);
    const auto back = transformPoint(inv, there);
    EXPECT_NEAR(back.x, p.x, 1e-3f);
    EXPECT_NEAR(back.y, p.y, 1e-3f);
}

TEST(DrawingMath, InvertIdentityAndTranslation)
{
    expectMatrixNear(invert(kIdentity), kIdentity);

    const Matrix3x2 translate{ 1.0f, 0.0f, 0.0f, 1.0f, 10.0f, -5.0f };
    expectMatrixNear(invert(translate), Matrix3x2{ 1.0f, 0.0f, 0.0f, 1.0f, -10.0f, 5.0f });
}

// A singular transform has no inverse. Returning the infinities that fall out
// of 1/0 used to propagate NaN into everything downstream (a gradient brush
// painted NaN over whole spans, permanently — NaN * 0 is still NaN).
TEST(DrawingMath, InvertRejectsSingularTransforms)
{
    const Matrix3x2 singular[] = {
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },       // fully collapsed
        { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f },       // flattened onto a horizontal line
        { 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f },       // flattened onto a vertical line
        { 2.0f, 1.0f, 4.0f, 2.0f, 0.0f, 0.0f },       // rows linearly dependent
        { 1e-25f, 0.0f, 0.0f, 1e-25f, 0.0f, 0.0f },   // invertible on paper: det underflows
    };

    for (const auto& m : singular)
    {
        constexpr Matrix3x2 sentinel{ 42.0f, 42.0f, 42.0f, 42.0f, 42.0f, 42.0f };
        Matrix3x2 out = sentinel;
        EXPECT_FALSE(tryInvert(m, out)) << "tryInvert should report failure";
        // tryInvert must leave its output alone on failure.
        expectMatrixNear(out, sentinel);

        const auto fallback = invert(m);
        EXPECT_TRUE(isFinite(fallback)) << "invert() must never return non-finite values";
        expectMatrixNear(fallback, kIdentity);
    }
}

TEST(DrawingMath, InvertRejectsNonFiniteTransforms)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const Matrix3x2 bad[] = {
        { nan, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f, inf, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f, nan, 0.0f },  // finite basis, non-finite translation
        { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, inf },
    };

    for (const auto& m : bad)
    {
        EXPECT_FALSE(isFinite(m));
        Matrix3x2 out;
        EXPECT_FALSE(tryInvert(m, out));
        EXPECT_TRUE(isFinite(invert(m)));
    }
}

TEST(DrawingMath, TryInvertSucceedsForUsableTransforms)
{
    const Matrix3x2 fine[] = {
        kIdentity,
        { 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f },      // 90 degree rotation
        { -1.0f, 0.0f, 0.0f, 1.0f, 64.0f, 0.0f },     // mirror
        { 1e-3f, 0.0f, 0.0f, 1e-3f, 0.0f, 0.0f },     // small but representable
    };

    for (const auto& m : fine)
    {
        Matrix3x2 out;
        ASSERT_TRUE(tryInvert(m, out)) << "should be invertible";
        EXPECT_TRUE(isFinite(out));
        expectMatrixNear(m * out, kIdentity, 1e-3f);
    }
}

TEST(DrawingMath, IsFiniteMatrix)
{
    EXPECT_TRUE(isFinite(kIdentity));
    EXPECT_FALSE(isFinite(Matrix3x2{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                     std::numeric_limits<float>::quiet_NaN() }));
}
