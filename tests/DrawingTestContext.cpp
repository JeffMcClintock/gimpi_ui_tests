#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <vector>
#include "backends/DirectXGfx.h"
#include "DrawingTestContext.h"

struct DrawingTestContext::Impl
{
    std::unique_ptr<gmpi::directx::Factory> backendFactory;
    gmpi::drawing::Factory                  factory;
};

DrawingTestContext::DrawingTestContext() : impl_(std::make_unique<Impl>())
{
    CoInitialize(nullptr);
    impl_->backendFactory = std::make_unique<gmpi::directx::Factory>();
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

/*static*/
bool DrawingTestContext::createSRGBGradientPng(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path))
        return true;

    std::filesystem::create_directories(path.parent_path());

    constexpr uint32_t W = 128, H = 20;
    // Raw BGRA pixels — fully opaque grey, sRGB value = column index.
    std::vector<uint8_t> pixels(W * H * 4);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
        {
            uint8_t v = static_cast<uint8_t>(x);
            uint8_t* p = pixels.data() + (y * W + x) * 4;
            p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
        }

    IWICImagingFactory* rawWic{};
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&rawWic));
    gmpi::directx::ComPtr<IWICImagingFactory> wic(rawWic);
    if (!wic) return false;

    IWICStream* rawStream{};
    wic->CreateStream(&rawStream);
    gmpi::directx::ComPtr<IWICStream> stream(rawStream);
    if (!stream) return false;
    stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);

    IWICBitmapEncoder* rawEncoder{};
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &rawEncoder);
    gmpi::directx::ComPtr<IWICBitmapEncoder> encoder(rawEncoder);
    if (!encoder) return false;
    encoder->Initialize(stream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* rawFrame{};
    IPropertyBag2* props{};
    encoder->CreateNewFrame(&rawFrame, &props);
    gmpi::directx::ComPtr<IWICBitmapFrameEncode> frame(rawFrame);
    if (props) props->Release();
    if (!frame) return false;

    frame->Initialize(nullptr);
    frame->SetSize(W, H);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(H, W * 4, static_cast<UINT>(pixels.size()), pixels.data());
    frame->Commit();
    encoder->Commit();

    return true;
}
#endif // _WIN32
