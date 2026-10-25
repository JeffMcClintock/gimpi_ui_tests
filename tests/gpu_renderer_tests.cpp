/* Copyright Vital Audio, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// Pinning tests for gmpi_ui's GPU renderer (backends/gpu), ported from the
// Visage fork's Catch2 suite (visage_graphics/tests/precision_tests.cpp at
// 4eeb51c) when the renderer was absorbed into gmpi_ui. Each pins a bug or
// a property found while building the GPU backend; see VISAGE_BACKEND_PLAN.md,
// "Measured". They drive the renderer's Canvas directly, below the GMPI
// adaptor.


// Proves layers are RGBA16F, not silently 8-bit: these would fail on RGBA8.

#include "DrawingTestContext.h"

#if GMPI_UI_TESTS_BACKEND_GPU

#include "backends/gpu/canvas.h"
#include "backends/gpu/color.h"
#include "backends/gpu/renderer.h"
#include "backends/gpu/screenshot.h"

#include <bgfx/bgfx.h>
#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

using namespace gmpi::gpu;

// What Catch2's WARN / UNSCOPED_INFO did: a line in the test log.
#define GPU_TEST_LOG(x) do { std::cerr << x << std::endl; } while (0)

static constexpr int kSize = 16;

TEST(GpuRenderer, HalfFloatLayersAreTheDefault) {
  Canvas canvas;
  canvas.setWindowless(kSize, kSize);
  EXPECT_TRUE(Renderer::instance().halfFloatFrameBuffers());

  canvas.setColor(0xff000000);
  canvas.fill(0, 0, kSize, kSize);
  canvas.submit();
  const Screenshot& screenshot = canvas.takeScreenshot();
  EXPECT_TRUE(screenshot.halfFloat());
}

TEST(GpuRenderer, HalfFloatLayersKeepSub8BitAlphaAccumulation) {
  Canvas canvas;
  canvas.setWindowless(kSize, kSize);

  canvas.setColor(0xff000000);
  canvas.fill(0, 0, kSize, kSize);

  // White at alpha 1/512 contributes under half an 8-bit code value per pass,
  // so an RGBA8 layer rounds every pass away and stays black. In fp16 the
  // passes accumulate: 1 - (1 - 1/512)^300 = 0.44 -> ~112/255.
  static constexpr int kPasses = 300;
  canvas.setColor(Color(1.0f / 512.0f, 1.0f, 1.0f, 1.0f));
  for (int i = 0; i < kPasses; ++i)
    canvas.fill(0, 0, kSize, kSize);

  canvas.submit();
  const Screenshot& screenshot = canvas.takeScreenshot();
  Color center = screenshot.sample(kSize / 2, kSize / 2);
  EXPECT_TRUE(center.hexRed() > 90);
  EXPECT_TRUE(center.hexRed() < 135);
}

TEST(GpuRenderer, HalfFloatLayersHoldValuesAbove10) {
  Canvas canvas;
  canvas.setWindowless(kSize, kSize);

  static constexpr float kHdrMultiplier = 2.5f;
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f, kHdrMultiplier));
  canvas.fill(0, 0, kSize, kSize);

  canvas.submit();
  const Screenshot& screenshot = canvas.takeScreenshot();
  EXPECT_TRUE(screenshot.halfFloat());

  int index = ((kSize / 2) * kSize + kSize / 2) * 4;
  float red = Screenshot::halfToFloat(screenshot.halfData()[index]);
  float alpha = Screenshot::halfToFloat(screenshot.halfData()[index + 3]);
  EXPECT_NEAR(red, kHdrMultiplier, 0.01f);
  EXPECT_NEAR(alpha, 1.0f, 0.001f);

  // The 8-bit view clamps rather than wraps.
  EXPECT_TRUE(screenshot.sample(kSize / 2, kSize / 2).hexRed() == 0xff);
}

// A path fill must not disturb pixels outside the path. Seen from the GMPI
// backend: two 3x2 black blocks at (7..9, 6..7) and (39..41, 6..7) on a white
// 64x64 target after filling a 40x40 rectangle path at (10,10).
TEST(GpuRenderer, PathFillLeavesTheBackgroundUntouched) {
  for (const int kDim : { 64, 128, 96 }) {
    SCOPED_TRACE(kDim);
    Canvas canvas;
    canvas.setWindowless(kDim, kDim);
    canvas.setClampBounds(0, 0, kDim, kDim);

    canvas.setBlendMode(BlendMode::Opaque);
    canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
    canvas.fill(0, 0, kDim, kDim);
    canvas.setBlendMode(BlendMode::Alpha);

    Path path;
    path.moveTo(10.0f, 10.0f);
    path.lineTo(50.0f, 10.0f);
    path.lineTo(50.0f, 50.0f);
    path.lineTo(10.0f, 50.0f);
    path.close();
    canvas.setColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
    canvas.fill(path, 0, 0);

    canvas.submit();
    const Screenshot& screenshot = canvas.takeScreenshot();

    EXPECT_TRUE(screenshot.sample(30, 30).hexBlue() == 0xff);
    EXPECT_TRUE(screenshot.sample(30, 30).hexRed() == 0x00);

    int wrong = 0;
    for (int y = 0; y < kDim; ++y) {
      for (int x = 0; x < kDim; ++x) {
        bool inside = x >= 11 && x < 49 && y >= 11 && y < 49;
        bool edge = (x >= 9 && x <= 50 && y >= 9 && y <= 50) && !inside;
        if (edge)
          continue;
        Color c = screenshot.sample(x, y);
        bool white = c.hexRed() == 0xff && c.hexGreen() == 0xff && c.hexBlue() == 0xff;
        bool blue = c.hexRed() == 0x00 && c.hexGreen() == 0x00 && c.hexBlue() == 0xff;
        if (inside ? !blue : !white) {
          if (wrong < 8)
            GPU_TEST_LOG("pixel (" << x << "," << y << ") = " << std::hex << c.toARGB());
          ++wrong;
        }
      }
    }
    EXPECT_TRUE(wrong == 0);
  }
}

// Not an assertion: records what this device offers, for reading test logs.
TEST(GpuRenderer, ReportRendererCapabilities) {
  Canvas canvas;
  canvas.setWindowless(8, 8);
  const bgfx::Caps* caps = bgfx::getCaps();
  GPU_TEST_LOG("renderer: " << bgfx::getRendererName(caps->rendererType)
       << ", conservative raster: " << ((caps->supported & BGFX_CAPS_CONSERVATIVE_RASTER) ? "yes" : "no")
       << ", texture read back: " << ((caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) ? "yes" : "no")
       << ", RGBA16F framebuffer: " << ((caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ? "yes" : "no")
       << ", R32F framebuffer: " << ((caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ? "yes" : "no")
       << ", half-float layers: " << (Renderer::instance().halfFloatFrameBuffers() ? "on" : "off"));
}

// Canvas::halfImage draws raw RGBA16F pixels through the half-float atlas.
TEST(GpuRenderer, HalfFloatImageDrawsPremultipliedPixels) {
  static constexpr int kDim = 32;
  static constexpr int kImg = 8;
  Canvas canvas;
  canvas.setWindowless(kDim, kDim);
  canvas.setClampBounds(0, 0, kDim, kDim);

  canvas.setBlendMode(BlendMode::Opaque);
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
  canvas.fill(0, 0, kDim, kDim);

  // Opaque red, premultiplied: (1, 0, 0, 1) as halves.
  std::vector<uint16_t> pixels(kImg * kImg * 4);
  for (int i = 0; i < kImg * kImg; ++i) {
    pixels[i * 4 + 0] = 0x3c00;
    pixels[i * 4 + 1] = 0;
    pixels[i * 4 + 2] = 0;
    pixels[i * 4 + 3] = 0x3c00;
  }
  canvas.setBlendMode(BlendMode::Composite);
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
  canvas.halfImage(reinterpret_cast<const unsigned char*>(pixels.data()), kImg, kImg, 4.0f, 4.0f,
                   float(kImg), float(kImg));
  canvas.setBlendMode(BlendMode::Alpha);

  canvas.submit();
  const Screenshot& screenshot = canvas.takeScreenshot();
  Color inside = screenshot.sample(8, 8);
  Color outside = screenshot.sample(20, 20);
  EXPECT_TRUE(inside.hexRed() == 0xff);
  EXPECT_TRUE(inside.hexGreen() == 0x00);
  EXPECT_TRUE(inside.hexBlue() == 0x00);
  EXPECT_TRUE(outside.hexGreen() == 0xff);
}

// Path coverage against the analytic value: a rectangle offset by half a
// pixel must give exactly 50% on its edge columns and rows, and a 45-degree
// edge 50% on the pixels it bisects.
static float redCoverage(const Screenshot& s, int x, int y) {
  // white background, black path: coverage = 1 - red
  return 1.0f - s.sample(x, y).hexRed() / 255.0f;
}

TEST(GpuRenderer, PathCoverageIsAnalyticOnEdges) {
  static constexpr int kDim = 32;
  Canvas canvas;
  canvas.setWindowless(kDim, kDim);
  canvas.setClampBounds(0, 0, kDim, kDim);
  canvas.setBlendMode(BlendMode::Opaque);
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
  canvas.fill(0, 0, kDim, kDim);
  canvas.setBlendMode(BlendMode::Alpha);
  canvas.setColor(Color(1.0f, 0.0f, 0.0f, 0.0f));

  Path rect;
  rect.moveTo(4.5f, 4.5f);
  rect.lineTo(12.5f, 4.5f);
  rect.lineTo(12.5f, 12.5f);
  rect.lineTo(4.5f, 12.5f);
  rect.close();
  canvas.fill(rect, 0, 0);

  Path tri;  // right triangle, hypotenuse at 45 degrees through pixel centres
  tri.moveTo(16.0f, 16.0f);
  tri.lineTo(28.0f, 16.0f);
  tri.lineTo(16.0f, 28.0f);
  tri.close();
  canvas.fill(tri, 0, 0);

  canvas.submit();
  const Screenshot& s = canvas.takeScreenshot();

  float inside = redCoverage(s, 8, 8);
  float left = redCoverage(s, 4, 8);
  float right = redCoverage(s, 12, 8);
  float top = redCoverage(s, 8, 4);
  float bottom = redCoverage(s, 8, 12);
  float corner = redCoverage(s, 4, 4);
  float diag1 = redCoverage(s, 18, 25);  // centre (18.5, 25.5): x + y = 44 = exactly on the edge
  float diag2 = redCoverage(s, 22, 21);
  float diagIn = redCoverage(s, 18, 20); // fully inside
  float diagOut = redCoverage(s, 26, 25); // fully outside
  GPU_TEST_LOG("coverage inside " << inside << " left " << left << " right " << right << " top " << top
       << " bottom " << bottom << " corner " << corner << " diag " << diag1 << "/" << diag2
       << " diagIn " << diagIn << " diagOut " << diagOut);

  EXPECT_NEAR(inside, 1.0f, 0.01f);
  EXPECT_NEAR(left, 0.5f, 0.03f);
  EXPECT_NEAR(right, 0.5f, 0.03f);
  EXPECT_NEAR(top, 0.5f, 0.03f);
  EXPECT_NEAR(bottom, 0.5f, 0.03f);
  EXPECT_NEAR(corner, 0.25f, 0.03f);
  EXPECT_NEAR(diag1, 0.5f, 0.05f);
  EXPECT_NEAR(diag2, 0.5f, 0.05f);
  EXPECT_NEAR(diagIn, 1.0f, 0.01f);
  EXPECT_NEAR(diagOut, 0.0f, 0.01f);
}

// A graph-shaped fill (hundreds of points, several peaks) is a fan of thin
// slivers from its first point. Every exterior pixel the fan sweeps must come
// back to exactly 0 and every interior pixel to exactly 1 (the R16F atlas
// left ~1e-3 residues the display showed as fan lines), and the oriented
// bounding box the vertex shader draws for each sliver must not miss a pixel.
TEST(GpuRenderer, FanAccumulationLeavesAGraphFillClean) {
  static constexpr int kDim = 128;
  Canvas canvas;
  canvas.setWindowless(kDim, kDim);
  canvas.setClampBounds(0, 0, kDim, kDim);
  canvas.setBlendMode(BlendMode::Opaque);
  canvas.setColor(Color(1.0f, 0.0f, 0.0f, 0.0f));
  canvas.fill(0, 0, kDim, kDim);
  canvas.setBlendMode(BlendMode::Alpha);
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));

  auto graphY = [](float x) {
    float y = 100.0f - 0.15f * x;
    y -= 85.0f * std::exp(-((x - 50.0f) * (x - 50.0f)) / 18.0f);
    y -= 70.0f * std::exp(-((x - 72.0f) * (x - 72.0f)) / 8.0f);
    y -= 60.0f * std::exp(-((x - 88.0f) * (x - 88.0f)) / 5.0f);
    y -= 45.0f * std::exp(-((x - 100.0f) * (x - 100.0f)) / 3.0f);
    y -= 30.0f * std::exp(-((x - 110.0f) * (x - 110.0f)) / 2.0f);
    return y;
  };

  static constexpr float kLeft = 3.0f;
  static constexpr float kRight = 122.0f;
  static constexpr float kBottom = 121.0f;
  static constexpr float kStep = 0.37f;  // points off the pixel grid, as a real graph is
  Path graph;
  graph.moveTo(kLeft, graphY(kLeft));
  for (float x = kLeft + kStep; x < kRight; x += kStep)
    graph.lineTo(x, graphY(x));
  graph.lineTo(kRight, graphY(kRight));
  graph.lineTo(kRight, kBottom);
  graph.lineTo(kLeft, kBottom);
  graph.close();
  EXPECT_TRUE(graph.numPoints() > 300);
  canvas.fill(graph, 0, 0);

  canvas.submit();
  const Screenshot& s = canvas.takeScreenshot();

  float worst_outside = 0.0f;
  float worst_inside = 1.0f;
  int outside_x = -1, outside_y = -1, inside_x = -1, inside_y = -1;
  for (int y = 0; y < kDim; ++y) {
    for (int x = 0; x < kDim; ++x) {
      float cx = x + 0.5f;
      // Two pixels clear of every edge: the curve's extremes over the
      // neighbouring columns, sampled densely (the narrow peaks move
      // ten pixels per column).
      float top = 1000.0f, bottom = -1000.0f;
      for (float sx = cx - 1.5f; sx <= cx + 1.5f; sx += 0.125f) {
        top = std::min(top, graphY(sx));
        bottom = std::max(bottom, graphY(sx));
      }
      bool outside = cx < kLeft - 2.0f || cx > kRight + 2.0f || y + 1.0f < top - 2.0f ||
                     y > kBottom + 2.0f;
      bool inside = cx > kLeft + 2.0f && cx < kRight - 2.0f && y > bottom + 2.0f &&
                    y + 1.0f < kBottom - 2.0f;
      float value = s.sample(x, y).hexRed() / 255.0f;  // black background, white fill
      if (outside && value > worst_outside) {
        worst_outside = value;
        outside_x = x;
        outside_y = y;
      }
      if (inside && value < worst_inside) {
        worst_inside = value;
        inside_x = x;
        inside_y = y;
      }
    }
  }
  GPU_TEST_LOG("worst exterior residue " << worst_outside << " at (" << outside_x << "," << outside_y
       << "), worst interior coverage " << worst_inside << " at (" << inside_x << "," << inside_y << ")");
  // 1/255 would already be a visible fan line on an sRGB display.
  EXPECT_TRUE(worst_outside < 0.0005f);
  EXPECT_TRUE(worst_inside > 0.9995f);
}

// A headless canvas holds premultiplied pixels: a 50% alpha fill over a
// transparent target must read back as (0.5c, 0.5), not (0.25c, 0.5). That is
// what happens when the premultiplied intermediate layer is composited with a
// straight-alpha blend (colour multiplied by alpha a second time).
TEST(GpuRenderer, SemiTransparentContentReadsBackPremultipliedOnce) {
  static constexpr int kDim = 16;
  Canvas canvas;
  canvas.setWindowless(kDim, kDim);
  canvas.setClampBounds(0, 0, kDim, kDim);

  canvas.setColor(Color(0.5f, 1.0f, 0.0f, 0.0f));
  canvas.fill(0, 0, kDim, kDim);

  canvas.submit();
  const Screenshot& s = canvas.takeScreenshot();
  EXPECT_TRUE(s.halfFloat());
  int index = ((kDim / 2) * kDim + kDim / 2) * 4;
  float red = Screenshot::halfToFloat(s.halfData()[index]);
  float alpha = Screenshot::halfToFloat(s.halfData()[index + 3]);
  EXPECT_NEAR(alpha, 0.5f, 0.01f);
  EXPECT_NEAR(red, 0.5f, 0.01f);
}

// Render-target mode: pixels persist across submits, each shape composites
// exactly once, and a screenshot without redraw does not draw them again.
TEST(GpuRenderer, RetainedPixelsCompositeEachShapeOnce) {
  static constexpr int kDim = 16;
  Canvas canvas;
  canvas.setWindowless(kDim, kDim);
  canvas.setRetainPixels(true);
  canvas.setClampBounds(0, 0, kDim, kDim);

  canvas.setBlendMode(BlendMode::Opaque);
  canvas.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
  canvas.fill(0, 0, kDim, kDim);
  canvas.setBlendMode(BlendMode::Alpha);
  canvas.invalidateContent();
  canvas.submit();
  canvas.clearShapesKeepPixels();

  // 50% black over white -> 50% grey, drawn in its own submit.
  canvas.setColor(Color(0.5f, 0.0f, 0.0f, 0.0f));
  canvas.fill(0, 0, kDim, kDim);
  canvas.invalidateContent();
  canvas.submit();
  canvas.clearShapesKeepPixels();

  const Screenshot& first = canvas.takeScreenshot(false);
  int grey1 = first.sample(8, 8).hexRed();

  // Another frame with an unrelated shape elsewhere: the grey must not darken.
  canvas.setColor(Color(1.0f, 1.0f, 0.0f, 0.0f));
  canvas.fill(0, 0, 2, 2);
  canvas.invalidateContent();
  canvas.submit();
  canvas.clearShapesKeepPixels();
  const Screenshot& second = canvas.takeScreenshot(false);
  int grey2 = second.sample(8, 8).hexRed();
  EXPECT_NEAR(grey1, 128, 2);
  EXPECT_TRUE(grey2 == grey1);
  EXPECT_TRUE(second.sample(1, 1).hexRed() == 0xff);
  EXPECT_TRUE(second.sample(1, 1).hexGreen() == 0x00);
}


// A short-lived second canvas, read back and destroyed between another
// canvas's frames, must not change what later canvases draw. Seen from the
// GMPI suite: after such a sequence every later canvas lost its blue
// channel (white read back as yellow), one run in three.
static Color fillAndRead(Canvas& canvas, const Color& color, int dim) {
  canvas.setClampBounds(0, 0, dim, dim);
  canvas.setBlendMode(BlendMode::Opaque);
  canvas.setColor(color);
  canvas.fill(0, 0, dim, dim);
  canvas.setBlendMode(BlendMode::Alpha);
  canvas.invalidateContent();
  canvas.submit();
  canvas.clearShapesKeepPixels();
  return canvas.takeScreenshot(false).sample(dim / 2, dim / 2);
}

TEST(GpuRenderer, DestroyingACanvasBetweenFramesLeavesOtherCanvasesIntact) {
  int bad = 0;
  for (int round = 0; round < 60 && bad == 0; ++round) {
    Canvas main;
    main.setWindowless(64, 64);
    main.setRetainPixels(true);
    Color c0 = fillAndRead(main, Color(1.0f, 1.0f, 1.0f, 1.0f), 64);
    if (c0.hexBlue() != 0xff)
      ++bad;

    {
      Canvas pattern;
      pattern.setWindowless(16, 16);
      pattern.setRetainPixels(true);
      pattern.setClampBounds(0, 0, 16, 16);
      pattern.setBlendMode(BlendMode::Opaque);
      pattern.setColor(Color(1.0f, 1.0f, 0.0f, 0.0f)); pattern.fill(0, 0, 8, 8);
      pattern.setColor(Color(1.0f, 0.0f, 1.0f, 0.0f)); pattern.fill(8, 0, 8, 8);
      pattern.setColor(Color(1.0f, 0.0f, 0.0f, 1.0f)); pattern.fill(0, 8, 8, 8);
      pattern.setColor(Color(1.0f, 1.0f, 1.0f, 0.0f)); pattern.fill(8, 8, 8, 8);
      pattern.invalidateContent();
      pattern.submit();
      pattern.clearShapesKeepPixels();
      Color q = pattern.takeScreenshot(false).sample(4, 12);
      if (q.hexBlue() != 0xff)
        ++bad;

      // What the GMPI backend does next: draw the read-back pixels into main
      // as a half-float image (the RGBA16F atlas), twice with fresh buffers.
      for (int k = 0; k < 2; ++k) {
        const Screenshot& shot = pattern.takeScreenshot(false);
        std::vector<uint16_t> pixels(shot.halfData(), shot.halfData() + 16 * 16 * 4);
        main.setClampBounds(0, 0, 64, 64);
        main.setBlendMode(BlendMode::Composite);
        main.setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        main.halfImage(reinterpret_cast<const unsigned char*>(pixels.data()), 16, 16, 4.0f, 4.0f, 56.0f, 56.0f,
                       static_cast<uint64_t>(round * 2 + k + 1));
        main.setBlendMode(BlendMode::Alpha);
        main.invalidateContent();
        main.submit();
        main.clearShapesKeepPixels();
        const Screenshot& ms = main.takeScreenshot(false);
        Color yellow = ms.sample(48, 48);
        Color red = ms.sample(16, 16);
        if (yellow.hexRed() != 0xff || yellow.hexGreen() != 0xff || yellow.hexBlue() != 0x00 ||
            red.hexRed() != 0xff || red.hexGreen() != 0x00 || red.hexBlue() != 0x00) {
          GPU_TEST_LOG("half image round " << round << " k " << k << ": " << std::hex << yellow.toARGB() << " "
                                            << red.toARGB());
          ++bad;
        }
      }
    }

    // Draw into main again after the pattern canvas died.
    Color c1 = fillAndRead(main, Color(1.0f, 0.27f, 0.51f, 0.71f), 64); // SteelBlue
    if (c1.hexBlue() < 0xa0)
      ++bad;

    Canvas next;
    next.setWindowless(64, 64);
    next.setRetainPixels(true);
    Color c2 = fillAndRead(next, Color(1.0f, 1.0f, 1.0f, 1.0f), 64);
    if (c2.hexBlue() != 0xff)
      ++bad;
    if (bad)
      GPU_TEST_LOG("round " << round << ": " << std::hex << c0.toARGB() << " " << c1.toARGB() << " " << c2.toARGB());
  }
  EXPECT_TRUE(bad == 0);
}

// A windowless canvas resets the renderer; that reset must keep the flags
// windows depend on. It used to pass FLUSH | FLIP only, dropping
// SRGB_BACKBUFFER (Linux swap chains are 8-bit sRGB) and VSYNC, so any
// window created after an offscreen canvas presented linear values.
TEST(GpuRenderer, WindowlessCanvasKeepsTheWindowResetFlags) {
  Canvas canvas;
  canvas.setWindowless(8, 8);
  auto& renderer = Renderer::instance();
  const uint32_t flags = renderer.lastResetFlags();
  EXPECT_EQ(flags & renderer.resetFlags(), renderer.resetFlags()) << std::hex << flags;
  EXPECT_TRUE(flags & BGFX_RESET_FLIP_AFTER_RENDER);
#if GMPI_GPU_LINUX
  EXPECT_TRUE(flags & BGFX_RESET_SRGB_BACKBUFFER) << "window swap chains would come up linear";
#endif
  EXPECT_FALSE(renderer.vsync()) << "an offscreen canvas made first would impose vsync on later windows";
}

#endif // GMPI_UI_TESTS_BACKEND_GPU
