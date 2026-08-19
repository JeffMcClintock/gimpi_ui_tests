// The C half of the drawing-ABI round trip. See c_projection_calls.h.
//
// Two jobs:
//
//  1. Compile-time: _Static_asserts pin the projection's method-table offsets
//     to the frozen slot table that drawing_abi_tests.cpp pins the C++ vtable
//     to. The two files quote the same literal slot numbers on purpose - the
//     C++ header and the C mirror are proven against the SAME table, not
//     against each other's possibly-shared mistake.
//
//  2. Run-time: every call in this file goes through the projection's struct
//     tables from genuine C, so a wrong slot lands in the wrong backend
//     function and fails loudly in the test suite rather than in a customer's
//     module.
//
// This file must stay plain C (it is the proof the projection IS plain C -
// the 2026 resync replaced a projection that had never compiled at all).

#include "gmpi_drawing.h"

#include "c_projection_calls.h"

#include <stddef.h>

// ---------------------------------------------------------------------------
// The frozen slot table.
//
// EVERY method of every table is pinned by name to its own slot, not just the
// boundaries. Pinning boundaries alone catches an insertion or a removal
// (everything downstream shifts), but NOT a transposition of two same-shaped
// neighbours - and no backend reliably notices that either, because the
// signatures still match and the arguments still arrive intact. Naming each
// slot is what makes the whole table's ORDER the thing under test.
// ---------------------------------------------------------------------------
#define GMPI_SLOT(n) ((n) * sizeof(void*))
#define GMPI_PIN(table, member, slot) \
    _Static_assert(offsetof(table, member) == GMPI_SLOT(slot), \
                   #table "." #member " must be vtable slot " #slot)
#define GMPI_PIN_SIZE(table, slots) \
    _Static_assert(sizeof(table) == GMPI_SLOT(slots), \
                   #table " must have exactly " #slots " slots - the vtable is frozen")

// The three IUnknown methods lead every table, in this order.
#define GMPI_PIN_IUNKNOWN(table)         \
    GMPI_PIN(table, queryInterface, 0);  \
    GMPI_PIN(table, addRef, 1);          \
    GMPI_PIN(table, release, 2)

// The IDeviceContext method table, which GMPI_IBitmapRenderTargetMethods
// repeats verbatim before adding getBitmap. Pinned once as a macro so the two
// tables cannot drift apart: 3 IUnknown + getFactory + the 26 original
// methods + pushClipGeometry + drawTextLayout. FROZEN.
#define GMPI_PIN_DEVICE_CONTEXT_SLOTS(table)          \
    GMPI_PIN_IUNKNOWN(table);                         \
    GMPI_PIN(table, getFactory, 3);                   \
    GMPI_PIN(table, createBitmapBrush, 4);            \
    GMPI_PIN(table, createSolidColorBrush, 5);        \
    GMPI_PIN(table, createGradientstopCollection, 6); \
    GMPI_PIN(table, createLinearGradientBrush, 7);    \
    GMPI_PIN(table, createRadialGradientBrush, 8);    \
    GMPI_PIN(table, drawLine, 9);                     \
    GMPI_PIN(table, drawRectangle, 10);               \
    GMPI_PIN(table, fillRectangle, 11);               \
    GMPI_PIN(table, drawRoundedRectangle, 12);        \
    GMPI_PIN(table, fillRoundedRectangle, 13);        \
    GMPI_PIN(table, drawEllipse, 14);                 \
    GMPI_PIN(table, fillEllipse, 15);                 \
    GMPI_PIN(table, drawGeometry, 16);                \
    GMPI_PIN(table, fillGeometry, 17);                \
    GMPI_PIN(table, drawBitmap, 18);                  \
    GMPI_PIN(table, drawTextU, 19);                   \
    GMPI_PIN(table, drawRichTextU, 20);               \
    GMPI_PIN(table, setTransform, 21);                \
    GMPI_PIN(table, getTransform, 22);                \
    GMPI_PIN(table, pushAxisAlignedClip, 23);         \
    GMPI_PIN(table, popAxisAlignedClip, 24);          \
    GMPI_PIN(table, getAxisAlignedClip, 25);          \
    GMPI_PIN(table, clear, 26);                       \
    GMPI_PIN(table, beginDraw, 27);                   \
    GMPI_PIN(table, endDraw, 28);                     \
    GMPI_PIN(table, createCompatibleRenderTarget, 29);\
    GMPI_PIN(table, pushClipGeometry, 30);            \
    GMPI_PIN(table, drawTextLayout, 31)

// IResource: getFactory directly after the three IUnknown methods.
GMPI_PIN_IUNKNOWN(GMPI_IResourceMethods);
GMPI_PIN(GMPI_IResourceMethods, getFactory, 3);
GMPI_PIN_SIZE(GMPI_IResourceMethods, 4);

GMPI_PIN_DEVICE_CONTEXT_SLOTS(GMPI_IDeviceContextMethods);
GMPI_PIN_SIZE(GMPI_IDeviceContextMethods, 32);

// The same 32 slots, then the canary: getBitmap at slot 32, byte offset 0x100
// on 64-bit. This is the slot the frozen-vtable policy exists to protect.
GMPI_PIN_DEVICE_CONTEXT_SLOTS(GMPI_IBitmapRenderTargetMethods);
GMPI_PIN(GMPI_IBitmapRenderTargetMethods, getBitmap, 32);
GMPI_PIN_SIZE(GMPI_IBitmapRenderTargetMethods, 33);
_Static_assert(offsetof(GMPI_IBitmapRenderTargetMethods, getBitmap) == 0x100 || sizeof(void*) != 8,
               "on 64-bit, getBitmap must sit at byte offset 0x100");

// IFactory: 3 IUnknown + the 5 original methods + the 5 appended ones.
GMPI_PIN_IUNKNOWN(GMPI_IFactoryMethods);
GMPI_PIN(GMPI_IFactoryMethods, createPathGeometry, 3);
GMPI_PIN(GMPI_IFactoryMethods, createTextFormat, 4);
GMPI_PIN(GMPI_IFactoryMethods, createImage, 5);
GMPI_PIN(GMPI_IFactoryMethods, loadImageU, 6);
GMPI_PIN(GMPI_IFactoryMethods, createStrokeStyle, 7);
GMPI_PIN(GMPI_IFactoryMethods, getFontFamilyName, 8);
GMPI_PIN(GMPI_IFactoryMethods, getPlatformPixelFormat, 9);
GMPI_PIN(GMPI_IFactoryMethods, createRichTextFormat, 10);
GMPI_PIN(GMPI_IFactoryMethods, createCpuRenderTarget, 11);
GMPI_PIN(GMPI_IFactoryMethods, createTextLayout, 12);
GMPI_PIN_SIZE(GMPI_IFactoryMethods, 13);

// IBitmap: IResource + getSizeU + lockPixels.
GMPI_PIN_IUNKNOWN(GMPI_IBitmapMethods);
GMPI_PIN(GMPI_IBitmapMethods, getFactory, 3);
GMPI_PIN(GMPI_IBitmapMethods, getSizeU, 4);
GMPI_PIN(GMPI_IBitmapMethods, lockPixels, 5);
GMPI_PIN_SIZE(GMPI_IBitmapMethods, 6);

// IBitmapPixels: 3 IUnknown + the three getters.
GMPI_PIN_IUNKNOWN(GMPI_IBitmapPixelsMethods);
GMPI_PIN(GMPI_IBitmapPixelsMethods, getAddress, 3);
GMPI_PIN(GMPI_IBitmapPixelsMethods, getBytesPerRow, 4);
GMPI_PIN(GMPI_IBitmapPixelsMethods, getPixelFormat, 5);
GMPI_PIN_SIZE(GMPI_IBitmapPixelsMethods, 6);

// IGeometrySink and IPathGeometry, exercised by the clip test below.
GMPI_PIN_IUNKNOWN(GMPI_IGeometrySinkMethods);
GMPI_PIN(GMPI_IGeometrySinkMethods, beginFigure, 3);
GMPI_PIN(GMPI_IGeometrySinkMethods, endFigure, 4);
GMPI_PIN(GMPI_IGeometrySinkMethods, setFillMode, 5);
GMPI_PIN(GMPI_IGeometrySinkMethods, close, 6);
GMPI_PIN(GMPI_IGeometrySinkMethods, addLine, 7);
GMPI_PIN(GMPI_IGeometrySinkMethods, addLines, 8);
GMPI_PIN(GMPI_IGeometrySinkMethods, addBezier, 9);
GMPI_PIN(GMPI_IGeometrySinkMethods, addBeziers, 10);
GMPI_PIN(GMPI_IGeometrySinkMethods, addQuadraticBezier, 11);
GMPI_PIN(GMPI_IGeometrySinkMethods, addQuadraticBeziers, 12);
GMPI_PIN(GMPI_IGeometrySinkMethods, addArc, 13);
GMPI_PIN_SIZE(GMPI_IGeometrySinkMethods, 14);

GMPI_PIN_IUNKNOWN(GMPI_IPathGeometryMethods);
GMPI_PIN(GMPI_IPathGeometryMethods, getFactory, 3);
GMPI_PIN(GMPI_IPathGeometryMethods, open, 4);
GMPI_PIN(GMPI_IPathGeometryMethods, strokeContainsPoint, 5);
GMPI_PIN(GMPI_IPathGeometryMethods, fillContainsPoint, 6);
GMPI_PIN(GMPI_IPathGeometryMethods, getWidenedBounds, 7);
GMPI_PIN_SIZE(GMPI_IPathGeometryMethods, 8);

// ITextFormat plus the two retained-text interfaces.
GMPI_PIN_IUNKNOWN(GMPI_ITextFormatMethods);
GMPI_PIN(GMPI_ITextFormatMethods, setTextAlignment, 3);
GMPI_PIN(GMPI_ITextFormatMethods, setParagraphAlignment, 4);
GMPI_PIN(GMPI_ITextFormatMethods, setWordWrapping, 5);
GMPI_PIN(GMPI_ITextFormatMethods, getTextExtentU, 6);
GMPI_PIN(GMPI_ITextFormatMethods, getFontMetrics, 7);
GMPI_PIN(GMPI_ITextFormatMethods, setLineSpacing, 8);
GMPI_PIN_SIZE(GMPI_ITextFormatMethods, 9);

GMPI_PIN_IUNKNOWN(GMPI_IRichTextFormatMethods);
GMPI_PIN(GMPI_IRichTextFormatMethods, getTextExtentU, 3);
GMPI_PIN_SIZE(GMPI_IRichTextFormatMethods, 4);

GMPI_PIN_IUNKNOWN(GMPI_ITextLayoutMethods);
GMPI_PIN(GMPI_ITextLayoutMethods, getTextExtentU, 3);
GMPI_PIN_SIZE(GMPI_ITextLayoutMethods, 4);

// The brushes, whose only slots are the inherited ones - the case where a
// missing getFactory is easiest to overlook.
GMPI_PIN_IUNKNOWN(GMPI_IBrushMethods);
GMPI_PIN(GMPI_IBrushMethods, getFactory, 3);
GMPI_PIN_SIZE(GMPI_IBrushMethods, 4);
GMPI_PIN(GMPI_IBitmapBrushMethods, getFactory, 3);
GMPI_PIN_SIZE(GMPI_IBitmapBrushMethods, 4);
GMPI_PIN(GMPI_IGradientstopCollectionMethods, getFactory, 3);
GMPI_PIN_SIZE(GMPI_IGradientstopCollectionMethods, 4);
GMPI_PIN(GMPI_IStrokeStyleMethods, getFactory, 3);
GMPI_PIN_SIZE(GMPI_IStrokeStyleMethods, 4);
GMPI_PIN(GMPI_ISolidColorBrushMethods, getFactory, 3);
GMPI_PIN(GMPI_ISolidColorBrushMethods, setColor, 4);
GMPI_PIN_SIZE(GMPI_ISolidColorBrushMethods, 5);
GMPI_PIN(GMPI_ILinearGradientBrushMethods, getFactory, 3);
GMPI_PIN(GMPI_ILinearGradientBrushMethods, setStartPoint, 4);
GMPI_PIN(GMPI_ILinearGradientBrushMethods, setEndPoint, 5);
GMPI_PIN_SIZE(GMPI_ILinearGradientBrushMethods, 6);
GMPI_PIN(GMPI_IRadialGradientBrushMethods, getFactory, 3);
GMPI_PIN(GMPI_IRadialGradientBrushMethods, setCenter, 4);
GMPI_PIN(GMPI_IRadialGradientBrushMethods, setGradientOriginOffset, 5);
GMPI_PIN(GMPI_IRadialGradientBrushMethods, setRadiusX, 6);
GMPI_PIN(GMPI_IRadialGradientBrushMethods, setRadiusY, 7);
GMPI_PIN_SIZE(GMPI_IRadialGradientBrushMethods, 8);

// The structs that cross the ABI by value or by pointer.
_Static_assert(sizeof(GMPI_Color) == 16, "Color is 4 floats");
_Static_assert(sizeof(GMPI_Point) == 8, "Point is 2 floats");
_Static_assert(sizeof(GMPI_Matrix3x2) == 24, "Matrix3x2 is 6 floats");
_Static_assert(sizeof(GMPI_StrokeStyleProperties) == 20, "StrokeStyleProperties is 5 words");
_Static_assert(sizeof(GMPI_FontMetrics) == 36, "FontMetrics is 9 floats");
_Static_assert(sizeof(GMPI_TextStyleRun) == 44, "TextStyleRun is frozen at 44 bytes");

// ---------------------------------------------------------------------------
// Call-throughs.
// ---------------------------------------------------------------------------

int32_t cProjection_createCpuRenderTarget(
    GMPI_IFactory* factory, uint32_t width, uint32_t height, int32_t flags,
    float dpi, GMPI_IBitmapRenderTarget** returnRenderTarget)
{
    const GMPI_SizeU size = { width, height };
    *returnRenderTarget = NULL;
    return factory->methods->createCpuRenderTarget(factory, size, flags, returnRenderTarget, dpi);
}

int32_t cProjection_getFactory(
    GMPI_IBitmapRenderTarget* renderTarget, GMPI_IFactory** returnFactory)
{
    *returnFactory = NULL;
    return renderTarget->methods->getFactory(renderTarget, returnFactory);
}

int32_t cProjection_getPlatformPixelFormat(
    GMPI_IFactory* factory, int32_t* returnPixelFormat)
{
    *returnPixelFormat = 0;
    return factory->methods->getPlatformPixelFormat(factory, returnPixelFormat);
}

static const GMPI_Matrix3x2 identityTransform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

// beginDraw, then clear to transparent and hand back a solid brush.
static int32_t startScene(
    GMPI_IBitmapRenderTarget* rt, float r, float g, float b, float a,
    GMPI_ISolidColorBrush** returnBrush)
{
    *returnBrush = NULL;

    int32_t rc = rt->methods->beginDraw(rt);
    if (rc != GMPI_RETURN_CODE_OK)
        return rc;

    const GMPI_Color transparent = { 0.0f, 0.0f, 0.0f, 0.0f };
    rc = rt->methods->clear(rt, &transparent);
    if (rc != GMPI_RETURN_CODE_OK)
        return rc;

    const GMPI_Color color = { r, g, b, a };
    GMPI_BrushProperties brushProperties;
    brushProperties.opacity = 1.0f;
    brushProperties.transform = identityTransform;
    rc = rt->methods->createSolidColorBrush(rt, &color, &brushProperties, returnBrush);
    if (rc != GMPI_RETURN_CODE_OK)
        return rc;
    return *returnBrush ? GMPI_RETURN_CODE_OK : GMPI_RETURN_CODE_FAIL;
}

int32_t cProjection_fillWholeTarget(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t width, uint32_t height,
    float r, float g, float b, float a)
{
    GMPI_ISolidColorBrush* brush = NULL;
    int32_t rc = startScene(renderTarget, r, g, b, a, &brush);
    if (rc != GMPI_RETURN_CODE_OK)
        return rc;

    const GMPI_Rect all = { 0.0f, 0.0f, (float)width, (float)height };
    rc = renderTarget->methods->fillRectangle(renderTarget, &all, (GMPI_IBrush*)brush);

    brush->methods->release((GMPI_IUnknown*)brush);

    const int32_t endRc = renderTarget->methods->endDraw(renderTarget);
    return rc != GMPI_RETURN_CODE_OK ? rc : endRc;
}

int32_t cProjection_fillWithTriangleClip(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t width, uint32_t height,
    float r, float g, float b, float a)
{
    // The geometry comes from the factory the render target itself hands
    // back, so this also proves getFactory returns something real.
    GMPI_IFactory* factory = NULL;
    int32_t rc = renderTarget->methods->getFactory(renderTarget, &factory);
    if (rc != GMPI_RETURN_CODE_OK || !factory)
        return rc != GMPI_RETURN_CODE_OK ? rc : GMPI_RETURN_CODE_FAIL;

    GMPI_IPathGeometry* geometry = NULL;
    rc = factory->methods->createPathGeometry(factory, &geometry);
    factory->methods->release((GMPI_IUnknown*)factory);
    if (rc != GMPI_RETURN_CODE_OK || !geometry)
        return rc != GMPI_RETURN_CODE_OK ? rc : GMPI_RETURN_CODE_FAIL;

    GMPI_IGeometrySink* sink = NULL;
    rc = geometry->methods->open(geometry, &sink);
    if (rc != GMPI_RETURN_CODE_OK || !sink)
    {
        geometry->methods->release((GMPI_IUnknown*)geometry);
        return rc != GMPI_RETURN_CODE_OK ? rc : GMPI_RETURN_CODE_FAIL;
    }

    // GMPI_Point crosses by VALUE here - the ABI detail the old projection
    // got wrong by passing pointers.
    const GMPI_Point top = { 16.0f, 16.0f };
    const GMPI_Point corner = { 48.0f, 16.0f };
    const GMPI_Point bottom = { 48.0f, 48.0f };
    sink->methods->beginFigure(sink, top, GMPI_FIGURE_BEGIN_FILLED);
    sink->methods->addLine(sink, corner);
    sink->methods->addLine(sink, bottom);
    sink->methods->endFigure(sink, GMPI_FIGURE_END_CLOSED);
    rc = sink->methods->close(sink);
    sink->methods->release((GMPI_IUnknown*)sink);
    if (rc != GMPI_RETURN_CODE_OK)
    {
        geometry->methods->release((GMPI_IUnknown*)geometry);
        return rc;
    }

    GMPI_ISolidColorBrush* brush = NULL;
    rc = startScene(renderTarget, r, g, b, a, &brush);
    if (rc != GMPI_RETURN_CODE_OK)
    {
        geometry->methods->release((GMPI_IUnknown*)geometry);
        return rc;
    }

    rc = renderTarget->methods->pushClipGeometry(renderTarget, geometry);
    if (rc == GMPI_RETURN_CODE_OK)
    {
        const GMPI_Rect all = { 0.0f, 0.0f, (float)width, (float)height };
        rc = renderTarget->methods->fillRectangle(renderTarget, &all, (GMPI_IBrush*)brush);

        const int32_t popRc = renderTarget->methods->popAxisAlignedClip(renderTarget);
        if (rc == GMPI_RETURN_CODE_OK)
            rc = popRc;
    }

    brush->methods->release((GMPI_IUnknown*)brush);
    geometry->methods->release((GMPI_IUnknown*)geometry);

    const int32_t endRc = renderTarget->methods->endDraw(renderTarget);
    return rc != GMPI_RETURN_CODE_OK ? rc : endRc;
}

int32_t cProjection_readBackPixel(
    GMPI_IBitmapRenderTarget* renderTarget, uint32_t x, uint32_t y,
    uint8_t rgbaOut[4], int32_t* returnPixelFormat)
{
    *returnPixelFormat = 0;

    GMPI_IBitmap* bitmap = NULL;
    int32_t rc = renderTarget->methods->getBitmap(renderTarget, &bitmap); // slot 32
    if (rc != GMPI_RETURN_CODE_OK || !bitmap)
        return -100;

    GMPI_SizeU size = { 0, 0 };
    rc = bitmap->methods->getSizeU(bitmap, &size);
    if (rc != GMPI_RETURN_CODE_OK || x >= size.width || y >= size.height)
    {
        bitmap->methods->release((GMPI_IUnknown*)bitmap);
        return -101;
    }

    GMPI_IBitmapPixels* pixels = NULL;
    rc = bitmap->methods->lockPixels(bitmap, &pixels, GMPI_BITMAP_LOCK_FLAGS_READ);
    if (rc != GMPI_RETURN_CODE_OK || !pixels)
    {
        bitmap->methods->release((GMPI_IUnknown*)bitmap);
        return -102;
    }

    uint8_t* address = NULL;
    int32_t bytesPerRow = 0;
    int32_t format = 0;
    pixels->methods->getAddress(pixels, &address);
    pixels->methods->getBytesPerRow(pixels, &bytesPerRow);
    pixels->methods->getPixelFormat(pixels, &format);
    *returnPixelFormat = format;

    rc = GMPI_RETURN_CODE_OK;
    const int32_t bytesPerPixel = format & 0xFF; // bits [7:0], per the header
    if (!address || bytesPerPixel != 4)
    {
        rc = -103;
    }
    else
    {
        const uint8_t* p = address + (size_t)y * (size_t)bytesPerRow + (size_t)x * 4u;
        switch ((format >> 9) & 3) // channel layout bits [10:9]
        {
        case 0: rgbaOut[0] = p[2]; rgbaOut[1] = p[1]; rgbaOut[2] = p[0]; rgbaOut[3] = p[3]; break; // BGRA
        case 1: rgbaOut[0] = p[0]; rgbaOut[1] = p[1]; rgbaOut[2] = p[2]; rgbaOut[3] = p[3]; break; // RGBA
        case 2: rgbaOut[0] = p[1]; rgbaOut[1] = p[2]; rgbaOut[2] = p[3]; rgbaOut[3] = p[0]; break; // ARGB
        default:rgbaOut[0] = p[3]; rgbaOut[1] = p[2]; rgbaOut[2] = p[1]; rgbaOut[3] = p[0]; break; // ABGR
        }
    }

    pixels->methods->release((GMPI_IUnknown*)pixels);
    bitmap->methods->release((GMPI_IUnknown*)bitmap);
    return rc;
}

void cProjection_release(void* unknownInterface)
{
    GMPI_IUnknown* unknown = (GMPI_IUnknown*)unknownInterface;
    unknown->methods->release(unknown);
}
