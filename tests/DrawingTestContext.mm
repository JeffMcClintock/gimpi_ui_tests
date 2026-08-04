// Cocoa implementation of DrawingTestContext.
//
// Compiled on macOS but INACTIVE by default: the tests render through the
// shared CPU backend so that one set of Direct2D reference images describes
// the expected result on every platform. Set GMPI_UI_TESTS_NATIVE_BACKEND=1
// to make this the live implementation again — see DrawingTestContext.h.

#include "DrawingTestContext.h" // decides which implementation is live

#if !GMPI_UI_TESTS_BACKEND_CPU

#import <Cocoa/Cocoa.h>
#import "backends/CocoaGfx.h"

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

gmpi::drawing::BitmapRenderTarget DrawingTestContext::createCpuRenderTarget(gmpi::drawing::SizeU size, int32_t flags, float dpi)
{
    return impl_->factory.createCpuRenderTarget(size, flags, dpi);
}

#endif // !GMPI_UI_TESTS_BACKEND_CPU
