// GPU implementation of DrawingTestContext: gmpi_ui's bgfx-based backend
// (backends/GpuGfx.h, the gmpi fork of Visage). Opt in with
// -DGMPI_UI_TESTS_GPU_BACKEND=ON; see DrawingTestContext.h.
//
// Headless: the target is a windowless canvas, so no display server is
// needed, but a Vulkan ICD is. On a machine without a GPU, lavapipe does:
//
//   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./gmpi_ui_tests

#include "DrawingTestContext.h" // decides which implementation is live

#if GMPI_UI_TESTS_BACKEND_GPU

#include "backends/GpuGfx.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h"
#include "helpers/FontProvider.h"

struct DrawingTestContext::Impl
{
    gmpi::gpugfx::Factory        backendFactory;
    gmpi::drawing::CpuTextEngine textEngine{ gmpi::drawing::findFont };
    gmpi::drawing::Factory       factory;
};

DrawingTestContext::DrawingTestContext() : impl_(std::make_unique<Impl>())
{
#ifdef _WIN32
    CoInitialize(nullptr); // the WIC image decoders need COM, as in the Direct2D context
#endif
    // Same seams as the CPU backend: fonts, image decoding and shaping are the
    // host's, and text reaches the GPU as glyph geometry.
    impl_->textEngine.imageDecoder = gmpi::drawing::decodeImageMemory;
    impl_->backendFactory.cpu.imageDecoder = gmpi::drawing::decodeImageFile;
    impl_->backendFactory.cpu.textEngine = &impl_->textEngine;
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

#endif // GMPI_UI_TESTS_BACKEND_GPU
