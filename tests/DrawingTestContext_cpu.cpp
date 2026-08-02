// Pure-software implementation of DrawingTestContext (Linux, and anywhere else
// without a platform renderer).
//
// Renders through gmpi_ui's CPU backend (backends/CpuGfx.h) rather than an OS
// graphics stack, so the drawing tests run on a headless machine with no GPU,
// no display server and no JUCE. The three platform-shaped pieces the backend
// deliberately does not contain are wired in here:
//
//   fonts   helpers/FontProvider.h  (fontconfig on Linux)
//   decode  helpers/DecodeImage.h   (libpng)
//   shaping helpers/CpuTextEngine.h (HarfBuzz)

#include "backends/CpuGfx.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h"
#include "helpers/FontProvider.h"
#include "DrawingTestContext.h"

struct DrawingTestContext::Impl
{
    gmpi::cpugfx::Factory        backendFactory;
    gmpi::drawing::CpuTextEngine textEngine{ gmpi::drawing::findFont };
    gmpi::drawing::Factory       factory;
};

DrawingTestContext::DrawingTestContext() : impl_(std::make_unique<Impl>())
{
    // Colour-emoji glyphs arrive as PNG blobs inside the font, so the text
    // engine needs a decoder of its own, separate from the factory's.
    impl_->textEngine.imageDecoder = gmpi::drawing::decodeImageMemory;
    impl_->backendFactory.imageDecoder = gmpi::drawing::decodeImageFile;
    impl_->backendFactory.textEngine = &impl_->textEngine;

    *gmpi::drawing::AccessPtr::put(impl_->factory) = &impl_->backendFactory;
}

DrawingTestContext::~DrawingTestContext() = default;

gmpi::drawing::Factory& DrawingTestContext::factory()
{
    return impl_->factory;
}

gmpi::drawing::BitmapRenderTarget DrawingTestContext::createCpuRenderTarget(gmpi::drawing::SizeU size, int32_t flags, float dpi)
{
    return impl_->factory.createCpuRenderTarget(size, flags, dpi);
}
