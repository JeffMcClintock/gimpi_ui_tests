// JUCE-backend implementation of DrawingTestContext (e.g. Linux).
// Renders through gmpi_ui's backends/JuceGfx.h — the same backend the
// SynthEdit JUCE app uses — so the drawing tests exercise the real renderer.

#include "backends/JuceGfx.h"
#include "DrawingTestContext.h"

struct DrawingTestContext::Impl
{
    // JUCE needs its message-manager singleton before fonts/images work.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<gmpi::jucegfx::Factory> backendFactory;
    gmpi::drawing::Factory                  factory;
};

DrawingTestContext::DrawingTestContext() : impl_(std::make_unique<Impl>())
{
    impl_->backendFactory = std::make_unique<gmpi::jucegfx::Factory>();
    *gmpi::drawing::AccessPtr::put(impl_->factory) = impl_->backendFactory.get();
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
