// Pure-software implementation of DrawingTestContext.
//
// This is the DEFAULT everywhere except Windows (which renders natively,
// because the reference images are Direct2D output). Rendering macOS and
// Linux through one backend is what lets a single set of references describe
// the expected result on every platform: the CPU backend reproduces Direct2D's
// own rasterisation rather than approximating it. See DrawingTestContext.h for
// the switch back to native.
//
// It needs no GPU, no display server and no JUCE. The three platform-shaped
// pieces the backend deliberately does not contain are wired in here, and each
// resolves to the host's own facility:
//
//   fonts   helpers/FontProvider.h  (CoreText on macOS, fontconfig on Linux)
//   decode  helpers/DecodeImage.h   (ImageIO on macOS, libpng on Linux)
//   shaping helpers/CpuTextEngine.h (HarfBuzz, the same everywhere)

#include "DrawingTestContext.h" // decides which implementation is live

#if GMPI_UI_TESTS_BACKEND_CPU

#include "backends/CpuGfx.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h"
#include "helpers/FontProvider.h"

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

#endif // GMPI_UI_TESTS_BACKEND_CPU
