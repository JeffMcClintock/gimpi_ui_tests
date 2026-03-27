#import <Cocoa/Cocoa.h>
#import "backends/CocoaGfx.h"
#include "DrawingTestContext.h"

struct DrawingTestContext::Impl
{
    std::unique_ptr<gmpi::cocoa::Factory> backendFactory;
    gmpi::drawing::Factory                factory;
};

DrawingTestContext::DrawingTestContext() : impl_(std::make_unique<Impl>())
{
    impl_->backendFactory = std::make_unique<gmpi::cocoa::Factory>();
    *gmpi::drawing::AccessPtr::put(impl_->factory) = impl_->backendFactory.get();
}

DrawingTestContext::~DrawingTestContext() = default;

gmpi::drawing::Factory& DrawingTestContext::factory()
{
    return impl_->factory;
}

gmpi::drawing::BitmapRenderTarget DrawingTestContext::createCpuRenderTarget(gmpi::drawing::SizeU size, int32_t flags)
{
    return impl_->factory.createCpuRenderTarget(size, flags);
}
