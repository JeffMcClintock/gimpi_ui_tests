// The seam between drawing_c_projection_tests.cpp (C++, gtest) and
// c_projection_calls.c (plain C).
//
// The .c side includes projections/plain_c/gmpi_drawing.h and makes every
// call through the C method tables, compiled by a real C compiler; the .cpp
// side owns the backend objects and the assertions. Interface pointers cross
// this seam as the projection's own struct types, produced on the C++ side by
// reinterpret_cast from the gmpi::drawing::api interface pointers - which is
// precisely the cast a shipping C module performs, and precisely what these
// tests exist to keep honest.

#ifndef GMPI_C_PROJECTION_CALLS_H_INCLUDED
#define GMPI_C_PROJECTION_CALLS_H_INCLUDED

#include <stdint.h>

typedef struct GMPI_IFactory GMPI_IFactory;
typedef struct GMPI_IBitmapRenderTarget GMPI_IBitmapRenderTarget;

#ifdef __cplusplus
extern "C" {
#endif

// GMPI_IFactory.createCpuRenderTarget - tail-end factory slot (index 11).
int32_t cProjection_createCpuRenderTarget(
    GMPI_IFactory* factory, uint32_t width, uint32_t height, int32_t flags,
    float dpi, GMPI_IBitmapRenderTarget** returnRenderTarget);

// GMPI_IResource.getFactory - slot 3, the first non-IUnknown slot of every
// resource. The historical divergence this suite pins against started here.
int32_t cProjection_getFactory(
    GMPI_IBitmapRenderTarget* renderTarget, GMPI_IFactory** returnFactory);

// GMPI_IFactory.getPlatformPixelFormat - factory slot 9.
int32_t cProjection_getPlatformPixelFormat(
    GMPI_IFactory* factory, int32_t* returnPixelFormat);

// beginDraw / clear / createSolidColorBrush / fillRectangle / endDraw,
// all through the flattened GMPI_IBitmapRenderTarget table.
int32_t cProjection_fillWholeTarget(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t width, uint32_t height,
    float r, float g, float b, float a);

// As fillWholeTarget, but first clips to the triangle (16,16) (48,16) (48,48)
// built through GMPI_IGeometrySink (by-value GMPI_Point arguments) and applied
// with pushClipGeometry - vtable slot 30, the first of the two beta-period
// appends.
int32_t cProjection_fillWithTriangleClip(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t width, uint32_t height,
    float r, float g, float b, float a);

// GMPI_IBitmapRenderTarget.getBitmap - vtable slot 32, byte offset 0x100 on
// 64-bit: the canary slot the frozen-vtable policy protects. Then getSizeU /
// lockPixels / getAddress / getBytesPerRow / getPixelFormat, decoding the
// pixel at (x, y) into RGBA byte order via the format's channel-layout bits.
// Returns GMPI_RETURN_CODE_OK, a pass-through error code, or:
//   -100 getBitmap failed or returned null
//   -101 (x, y) outside the bitmap per getSizeU
//   -102 lockPixels failed
//   -103 not the 4-byte-per-pixel format the caller asked the target for
int32_t cProjection_readBackPixel(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t x, uint32_t y,
    uint8_t rgbaOut[4], int32_t* returnPixelFormat);

// GMPI_IUnknown.release on any projection interface pointer.
void cProjection_release(void* unknownInterface);

#ifdef __cplusplus
}
#endif

#endif
