// Colour probe: shows a gmpi_ui Direct2D frame full of known colour patches on a
// chosen monitor, reads the composited screen pixels back, and compares them with
// what the colour pipeline should produce. Runs the same window through different
// adapters and swap chain formats to find where colours drift:
//
//   gmpi_ui_color_probe [--monitor N | --monitor \\.\DISPLAYn] [--adapter N] [--8bit] [--warp]
//                       [--hwnd-swapchain | --deterrent <mode>] [--hold ms] [--png out.png] [--csv out.csv]
//
// --adapter picks the DXGI adapter gmpi_ui renders on (gmpi::directx::setAdapterOverride),
// --8bit forces the B8G8R8A8 swap chain, --warp the software rasteriser, --hwnd-swapchain
// bypasses the frame's DirectComposition visual and --deterrent swaps the visual's
// overlay-plane deterrent for another (see ProbeFrame). --hold keeps presenting for that
// long and logs whether DWM composes the swap chain or puts it on an overlay plane.
// Not a test: it needs a desktop, and takes the screen for a few seconds.

#define NOMINMAX
#include <windows.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <dxgi1_3.h> // IDXGISwapChainMedia: did DWM compose the swap chain, or give it an overlay plane?

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "backends/DrawingFrameWin.h"

#pragma comment(lib, "shcore.lib")

using namespace gmpi::drawing;

namespace
{

constexpr float kPatchDips = 36.0f;       // patch edge
constexpr int   kColumns   = 10;

float linearToSrgb(float v)
{
	v = std::clamp(v, 0.0f, 1.0f);
	return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float srgbToLinear(float v)
{
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// One patch: a background fill, then a (possibly translucent) foreground fill on top.
struct Patch
{
	std::string name;
	Color background;
	Color foreground;
	bool gradient{}; // foreground ignored: a black-to-white gradient across the patch, sampled at `gradientT`
	float gradientT{};
};

std::vector<Patch> makePatches()
{
	std::vector<Patch> p;
	const Color black{ 0, 0, 0, 1 }, white{ 1, 1, 1, 1 };

	for (int v : { 0, 8, 16, 32, 48, 64, 96, 128, 160, 192, 224, 240, 248, 255 })
		p.push_back({ "grey" + std::to_string(v), black, colorFromSrgba(v, v, v) });

	p.push_back({ "red", black, colorFromSrgba(255, 0, 0) });
	p.push_back({ "green", black, colorFromSrgba(0, 255, 0) });
	p.push_back({ "blue", black, colorFromSrgba(0, 0, 255) });
	p.push_back({ "cyan", black, colorFromSrgba(0, 255, 255) });
	p.push_back({ "magenta", black, colorFromSrgba(255, 0, 255) });
	p.push_back({ "yellow", black, colorFromSrgba(255, 255, 0) });
	p.push_back({ "red128", black, colorFromSrgba(128, 0, 0) });
	p.push_back({ "green128", black, colorFromSrgba(0, 128, 0) });
	p.push_back({ "blue128", black, colorFromSrgba(0, 0, 128) });
	p.push_back({ "skin", black, colorFromSrgba(224, 172, 105) });

	// Blends: linear-light and gamma-space compositing give different answers here.
	p.push_back({ "white50/black", black, Color{ 1, 1, 1, 0.5f } });
	p.push_back({ "white25/black", black, Color{ 1, 1, 1, 0.25f } });
	p.push_back({ "white75/black", black, Color{ 1, 1, 1, 0.75f } });
	p.push_back({ "black50/white", white, Color{ 0, 0, 0, 0.5f } });
	p.push_back({ "red50/white", white, Color{ 1, 0, 0, 0.5f } });
	p.push_back({ "red50/blue", colorFromSrgba(0, 0, 255), Color{ 1, 0, 0, 0.5f } });
	p.push_back({ "green50/magenta", colorFromSrgba(255, 0, 255), Color{ 0, 1, 0, 0.5f } });
	p.push_back({ "grey128a50/black", black, colorFromSrgba(128, 128, 128, 0.5f) });

	for (int i = 0; i <= 8; ++i)
		p.push_back({ "gradient" + std::to_string(i) + "/8", black, {}, true, i / 8.0f });

	return p;
}

Rect patchRect(size_t index)
{
	const float x = (index % kColumns) * kPatchDips;
	const float y = (index / kColumns) * kPatchDips;
	return { x, y, x + kPatchDips, y + kPatchDips };
}

// Expected sRGB bytes. Linear: alpha blend in linear light, then encode (what an fp16
// scRGB target does). Gamma: blend the encoded values (what an 8-bit target does).
struct Expected { float linear[3]; float gamma[3]; };

Expected expectedFor(const Patch& p)
{
	Expected e{};
	if (p.gradient)
	{
		// D2D interpolates the stops in the gradient's own colour space, so the sample
		// has two candidate answers as well: linear-light or sRGB-space interpolation.
		for (int c = 0; c < 3; ++c)
		{
			e.linear[c] = 255.0f * linearToSrgb(p.gradientT);
			e.gamma[c] = 255.0f * p.gradientT;
		}
		return e;
	}
	const float bg[3]{ p.background.r, p.background.g, p.background.b };
	const float fg[3]{ p.foreground.r, p.foreground.g, p.foreground.b };
	const float a = p.foreground.a;
	for (int c = 0; c < 3; ++c)
	{
		e.linear[c] = 255.0f * linearToSrgb(a * fg[c] + (1 - a) * bg[c]);
		e.gamma[c] = 255.0f * (a * linearToSrgb(fg[c]) + (1 - a) * linearToSrgb(bg[c]));
	}
	return e;
}

// The frame's client: paints every patch each time it is asked.
class PatternClient final : public gmpi::api::IDrawingClient
{
	std::vector<Patch> patches = makePatches();

public:
	size_t patchCount() const { return patches.size(); }
	const Patch& patch(size_t i) const { return patches[i]; }

	gmpi::ReturnCode setHost(gmpi::api::IUnknown*) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override { *returnDesiredSize = *availableSize; return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode arrange(const Rect*) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode getClipArea(Rect* returnRect) override { *returnRect = { 0, 0, 10000, 10000 }; return gmpi::ReturnCode::Ok; }

	gmpi::ReturnCode render(api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		g.clear(Color{ 0.25f, 0.25f, 0.25f, 1.0f });

		for (size_t i = 0; i < patches.size(); ++i)
		{
			const auto& p = patches[i];
			const auto r = patchRect(i);
			g.fillRectangle(r, g.createSolidColorBrush(p.background));
			if (p.gradient)
			{
				// One gradient spanning the patch, offset so the sample point sits at the patch centre.
				const float cx = (r.left + r.right) * 0.5f;
				const Gradientstop stops[]{ { 0.0f, Color{ 0, 0, 0, 1 } }, { 1.0f, Color{ 1, 1, 1, 1 } } };
				const float x0 = cx - p.gradientT * kPatchDips * 2;
				g.fillRectangle(r, g.createLinearGradientBrush(stops, { x0, 0 }, { x0 + kPatchDips * 2, 0 }));
			}
			else
			{
				g.fillRectangle(r, g.createSolidColorBrush(p.foreground));
			}
		}
		return gmpi::ReturnCode::Ok;
	}

	GMPI_QUERYINTERFACE_METHOD(gmpi::api::IDrawingClient);
	GMPI_REFCOUNT_NO_DELETE;
};

struct Options
{
	std::wstring monitor;  // index, or \\.\DISPLAYn
	int adapter = -1;
	bool eightBit{}, warp{}, hwndSwapChain{};
	int holdMs{};
	std::wstring png, csv;
	std::wstring deterrent; // an alternative overlay-plane deterrent to try, see ProbeFrame
};

// The frame as shipped, with a plain HWND swap chain instead of its DirectComposition
// visual, or with a composition visual carrying a different overlay-plane deterrent:
//   none           a plain visual (the control: DWM may promote it to an overlay plane)
//   opacity:<v>    an effect group with that opacity (the frame shipped 0.99 until 2026-10-21)
//   3d             an identity 3D matrix transform
//   rot3d:<deg>    a rotation about the Y axis (the frame ships 0.1; DWM ignores 0.002 and below, 1 resamples)
//   clip:<radius>  a rounded-rectangle clip with that corner radius, in pixels
class ProbeFrame final : public gmpi::hosting::DrawingFrame
{
public:
	bool hwndSwapChain{};
	std::wstring deterrent;

	HRESULT createNativeSwapChain(IDXGIFactory2* factory, ID3D11Device* d3dDevice, DXGI_SWAP_CHAIN_DESC1* desc, IDXGISwapChain1** returnSwapChain) override
	{
		if (hwndSwapChain)
			return factory->CreateSwapChainForHwnd(d3dDevice, getWindowHandle(), desc, nullptr, nullptr, returnSwapChain);
		if (deterrent.empty())
			return DrawingFrame::createNativeSwapChain(factory, d3dDevice, desc, returnSwapChain);
		return compositionWith(factory, d3dDevice, desc, returnSwapChain);
	}

private:
	HRESULT compositionWith(IDXGIFactory2* factory, ID3D11Device* d3dDevice, const DXGI_SWAP_CHAIN_DESC1* desc, IDXGISwapChain1** returnSwapChain)
	{
		const auto hwnd = getWindowHandle();
		RECT client{};
		GetClientRect(hwnd, &client);
		DXGI_SWAP_CHAIN_DESC1 compositionDesc{ *desc };
		compositionDesc.Width = static_cast<UINT>(std::max(1L, client.right - client.left));
		compositionDesc.Height = static_cast<UINT>(std::max(1L, client.bottom - client.top));
		compositionDesc.Scaling = DXGI_SCALING_STRETCH;

		gmpi::directx::ComPtr<IDXGISwapChain1> newSwapChain;
		HRESULT hr = factory->CreateSwapChainForComposition(d3dDevice, &compositionDesc, nullptr, newSwapChain.put());
		if (FAILED(hr)) return hr;

		compositionVisual = {}; compositionTarget = {}; compositionDevice = {};
		hr = DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice), compositionDevice.put_void());
		if (SUCCEEDED(hr)) hr = compositionDevice->CreateTargetForHwnd(hwnd, FALSE, compositionTarget.put());
		if (SUCCEEDED(hr)) hr = compositionDevice->CreateVisual(compositionVisual.put());

		const auto& m = deterrent;
		const auto value = [&](size_t prefix) { return static_cast<float>(_wtof(m.c_str() + prefix)); };
		gmpi::directx::ComPtr<IDCompositionEffectGroup> effect;
		if (SUCCEEDED(hr) && m.rfind(L"opacity:", 0) == 0)
		{
			hr = compositionDevice->CreateEffectGroup(effect.put());
			if (SUCCEEDED(hr)) hr = effect->SetOpacity(value(8));
		}
		else if (SUCCEEDED(hr) && m == L"3d")
		{
			gmpi::directx::ComPtr<IDCompositionMatrixTransform3D> transform;
			hr = compositionDevice->CreateEffectGroup(effect.put());
			if (SUCCEEDED(hr)) hr = compositionDevice->CreateMatrixTransform3D(transform.put());
			D3DMATRIX identity{};
			identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
			if (SUCCEEDED(hr)) hr = transform->SetMatrix(identity);
			if (SUCCEEDED(hr)) hr = effect->SetTransform3D(transform.get());
		}
		else if (SUCCEEDED(hr) && m.rfind(L"rot3d:", 0) == 0)
		{
			gmpi::directx::ComPtr<IDCompositionRotateTransform3D> rotate;
			hr = compositionDevice->CreateEffectGroup(effect.put());
			if (SUCCEEDED(hr)) hr = compositionDevice->CreateRotateTransform3D(rotate.put());
			if (SUCCEEDED(hr)) hr = rotate->SetAxisX(0.0f);
			if (SUCCEEDED(hr)) hr = rotate->SetAxisY(1.0f);
			if (SUCCEEDED(hr)) hr = rotate->SetAxisZ(0.0f);
			if (SUCCEEDED(hr)) hr = rotate->SetAngle(value(6));
			if (SUCCEEDED(hr)) hr = effect->SetTransform3D(rotate.get());
		}
		else if (SUCCEEDED(hr) && m.rfind(L"clip:", 0) == 0)
		{
			gmpi::directx::ComPtr<IDCompositionRectangleClip> clip;
			const float radius = value(5);
			hr = compositionDevice->CreateRectangleClip(clip.put());
			if (SUCCEEDED(hr)) hr = clip->SetLeft(0.0f);
			if (SUCCEEDED(hr)) hr = clip->SetTop(0.0f);
			if (SUCCEEDED(hr)) hr = clip->SetRight(static_cast<float>(compositionDesc.Width));
			if (SUCCEEDED(hr)) hr = clip->SetBottom(static_cast<float>(compositionDesc.Height));
			if (SUCCEEDED(hr)) hr = clip->SetTopLeftRadiusX(radius);
			if (SUCCEEDED(hr)) hr = clip->SetTopLeftRadiusY(radius);
			if (SUCCEEDED(hr)) hr = compositionVisual->SetClip(clip.get());
		}
		if (SUCCEEDED(hr) && effect) hr = compositionVisual->SetEffect(effect.get());
		if (SUCCEEDED(hr)) hr = compositionTarget->SetRoot(compositionVisual.get());
		if (SUCCEEDED(hr)) hr = compositionVisual->SetContent(newSwapChain.get());
		if (SUCCEEDED(hr)) hr = compositionDevice->Commit();
		if (FAILED(hr)) return hr;

		compositionWindow = hwnd;
		*returnSwapChain = newSwapChain.get();
		(*returnSwapChain)->AddRef();
		return S_OK;
	}
};

struct MonitorPick { HMONITOR monitor{}; int index{}; };

MonitorPick pickMonitor(const std::wstring& which)
{
	struct Ctx { std::wstring which; int i{}; MonitorPick found; } ctx{ which };
	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL
	{
		auto& c = *reinterpret_cast<Ctx*>(lp);
		MONITORINFOEXW info{}; info.cbSize = sizeof(info);
		GetMonitorInfoW(m, &info);
		const bool byName = !c.which.empty() && c.which[0] == L'\\';
		const bool match = c.which.empty() ? (info.dwFlags & MONITORINFOF_PRIMARY)
			: byName ? _wcsicmp(info.szDevice, c.which.c_str()) == 0
			: c.i == _wtoi(c.which.c_str());
		if (match && !c.found.monitor) c.found = { m, c.i };
		++c.i;
		return TRUE;
	}, reinterpret_cast<LPARAM>(&ctx));
	return ctx.found;
}

bool savePng(const std::wstring& path, const std::vector<uint8_t>& bgra, UINT w, UINT h)
{
	gmpi::directx::ComPtr<IWICImagingFactory> wic;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), wic.put_void());
	gmpi::directx::ComPtr<IWICStream> stream;
	gmpi::directx::ComPtr<IWICBitmapEncoder> encoder;
	gmpi::directx::ComPtr<IWICBitmapFrameEncode> frame;
	if (!wic || FAILED(wic->CreateStream(stream.put())) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
		return false;
	if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) || FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
		return false;
	IPropertyBag2* props{};
	if (FAILED(encoder->CreateNewFrame(frame.put(), &props))) return false;
	if (props) props->Release();
	frame->Initialize(nullptr);
	frame->SetSize(w, h);
	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
	frame->SetPixelFormat(&fmt);
	frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()));
	frame->Commit();
	return SUCCEEDED(encoder->Commit());
}

class ProbeWindow
{
public:
	bool create(HMONITOR monitor, const Options& opt)
	{
		MONITORINFOEXW info{}; info.cbSize = sizeof(info);
		GetMonitorInfoW(monitor, &info);
		UINT dpiX{}, dpiY{};
		GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		scale_ = dpiX / 96.0f;

		const int rows = static_cast<int>((client_.patchCount() + kColumns - 1) / kColumns);
		widthPx_ = static_cast<int>(std::ceil(kColumns * kPatchDips * scale_));
		heightPx_ = static_cast<int>(std::ceil(rows * kPatchDips * scale_));

		WNDCLASSEXW wc{ sizeof(wc) };
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"GmpiColorProbe";
		RegisterClassExW(&wc);

		// A borderless, top-most popup: its window rect is its client rect, and nothing covers it.
		hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, wc.lpszClassName, L"gmpi_ui colour probe", WS_POPUP,
			info.rcWork.left + 40, info.rcWork.top + 40, widthPx_, heightPx_, nullptr, nullptr, wc.hInstance, nullptr);
		if (!hwnd_) return false;

		gmpi::hosting::tempSharedD2DBase::m_disable_deep_color = opt.eightBit;
		gmpi::hosting::tempSharedD2DBase::m_disable_gpu = opt.warp;
		gmpi::directx::setAdapterOverride(opt.adapter);
		frame_.hwndSwapChain = opt.hwndSwapChain;
		frame_.deterrent = opt.deterrent;

		frame_.open(hwnd_);
		if (!frame_.getWindowHandle()) return false;
		frame_.attachClient(static_cast<gmpi::api::IDrawingClient*>(&client_));

		ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
		return true;
	}

	~ProbeWindow()
	{
		frame_.detachClient();
		frame_.close();
		if (hwnd_) DestroyWindow(hwnd_);
	}

	// Runs the message loop for `ms`. With `present`, the whole frame is redrawn and
	// presented every tick, which is what gets a swap chain promoted to an overlay
	// plane; DWM's choice is logged whenever it changes.
	void pump(int ms, bool present = false)
	{
		const auto start = GetTickCount64();
		MSG msg{};
		while (GetTickCount64() < start + ms)
		{
			if (present)
				frame_.invalidateRect(nullptr);
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
			MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
			logCompositionMode(start);
		}
	}

	void logCompositionMode(ULONGLONG start)
	{
		if (!frame_.swapChain) return;
		auto media = frame_.swapChain.as<IDXGISwapChainMedia>();
		DXGI_FRAME_STATISTICS_MEDIA stats{};
		if (!media || FAILED(media->GetFrameStatisticsMedia(&stats)) || static_cast<int>(stats.CompositionMode) == lastMode_)
			return;
		lastMode_ = static_cast<int>(stats.CompositionMode);
		static const char* names[]{ "COMPOSED", "OVERLAY", "NONE", "COMPOSITION_FAILURE" };
		printf("t=%.2fs DWM composition mode -> %s\n", (GetTickCount64() - start) / 1000.0, lastMode_ < 4 ? names[lastMode_] : "?");
	}

	// What gmpi_ui actually ended up with.
	void describe()
	{
		auto& sc = frame_.swapChain;
		if (!sc) { puts("no swap chain"); return; }
		DXGI_SWAP_CHAIN_DESC1 desc{}; sc->GetDesc1(&desc);
		gmpi::directx::ComPtr<IDXGIDevice> dev; sc->GetDevice(__uuidof(IDXGIDevice), dev.put_void());
		gmpi::directx::ComPtr<IDXGIAdapter> adapter; if (dev) dev->GetAdapter(adapter.put());
		DXGI_ADAPTER_DESC ad{}; if (adapter) adapter->GetDesc(&ad);
		HWND scHwnd{};
		const bool composition = FAILED(sc->GetHwnd(&scHwnd)); // a composition swap chain has no HWND
		printf("device: %ls\nswapchain: %s, %s%ls, %ux%u px, scale %.2f\n",
			ad.Description,
			desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "R16G16B16A16_FLOAT" : desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ? "B8G8R8A8_UNORM" : "other format",
			composition ? "DirectComposition visual, deterrent " : "HWND swap chain",
			composition ? (frame_.deterrent.empty() ? L"as shipped" : frame_.deterrent.c_str()) : L"",
			desc.Width, desc.Height, scale_);
	}

	// The composited desktop pixels under the window, BGRA.
	std::vector<uint8_t> capture()
	{
		RECT r{}; GetWindowRect(hwnd_, &r);
		const HDC screen = GetDC(nullptr);
		const HDC mem = CreateCompatibleDC(screen);
		BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), widthPx_, -heightPx_, 1, 32, BI_RGB };
		void* bits{};
		const HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		const auto old = SelectObject(mem, bmp);
		BitBlt(mem, 0, 0, widthPx_, heightPx_, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
		GdiFlush();
		std::vector<uint8_t> out(static_cast<size_t>(widthPx_) * heightPx_ * 4);
		memcpy(out.data(), bits, out.size());
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		ReleaseDC(nullptr, screen);
		return out;
	}

	const PatternClient& client() const { return client_; }
	float scale() const { return scale_; }
	int width() const { return widthPx_; }
	int height() const { return heightPx_; }

private:
	PatternClient client_;
	ProbeFrame frame_;
	HWND hwnd_{};
	float scale_ = 1.0f;
	int widthPx_{}, heightPx_{};
	int lastMode_ = -1;
};

Options parseArgs(int argc, wchar_t** argv)
{
	Options o;
	for (int i = 1; i < argc; ++i)
	{
		const std::wstring a = argv[i];
		auto next = [&] { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
		if (a == L"--monitor") o.monitor = next();
		else if (a == L"--adapter") o.adapter = _wtoi(next().c_str());
		else if (a == L"--8bit") o.eightBit = true;
		else if (a == L"--warp") o.warp = true;
		else if (a == L"--hwnd-swapchain") o.hwndSwapChain = true;
		else if (a == L"--deterrent") o.deterrent = next();
		else if (a == L"--hold") o.holdMs = _wtoi(next().c_str());
		else if (a == L"--png") o.png = next();
		else if (a == L"--csv") o.csv = next();
	}
	return o;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const Options opt = parseArgs(argc, argv);

	const auto pick = pickMonitor(opt.monitor);
	if (!pick.monitor) { fprintf(stderr, "monitor not found\n"); return 2; }
	MONITORINFOEXW info{}; info.cbSize = sizeof(info);
	GetMonitorInfoW(pick.monitor, &info);
	printf("monitor %d: %ls (%ld,%ld)-(%ld,%ld)%s\n", pick.index, info.szDevice,
		info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
		(info.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "");

	std::vector<uint8_t> pixels;
	int w{}, h{}; float scale{};
	const PatternClient reference; // for the patch list; the window's own client paints
	{
		ProbeWindow window;
		if (!window.create(pick.monitor, opt)) { fprintf(stderr, "window/frame creation failed\n"); return 3; }
		window.pump(1200); // several frames, and DWM
		window.describe();
		pixels = window.capture();
		w = window.width(); h = window.height(); scale = window.scale();
		if (opt.holdMs > 0)
		{
			// Present continuously, so DWM gets the chance to promote the swap chain, then see
			// whether the screen still shows the same pixels.
			window.pump(opt.holdMs, true);
			const auto again = window.capture();
			int worst = 0;
			for (size_t i = 0; i < pixels.size(); ++i)
				worst = std::max(worst, std::abs(int(again[i]) - int(pixels[i])));
			printf("after %d ms of presenting, the capture differs from the first by at most %d code values\n", opt.holdMs, worst);
		}
	}

	if (!opt.png.empty())
		printf("%s %ls\n", savePng(opt.png, pixels, w, h) ? "wrote" : "FAILED to write", opt.png.c_str());

	FILE* csv{};
	if (!opt.csv.empty()) { _wfopen_s(&csv, opt.csv.c_str(), L"w"); if (csv) fputs("patch,R,G,B,expLinR,expLinG,expLinB,expGamR,expGamG,expGamB,spread\n", csv); }

	printf("\n%-18s %-13s %-13s %-13s %7s %7s %s\n", "patch", "measured", "exp.linear", "exp.gamma", "dLin", "dGam", "spread");
	float worstLin = 0, worstGam = 0;
	for (size_t i = 0; i < reference.patchCount(); ++i)
	{
		const auto& p = reference.patch(i);
		const auto r = patchRect(i);
		// Average the middle third of the patch, so edges and any antialiasing stay out;
		// a gradient is sampled on its centre column only.
		const float inset = p.gradient ? kPatchDips / 2 - 0.5f : kPatchDips / 3;
		const int x0 = static_cast<int>((r.left + inset) * scale), x1 = std::max(x0 + 1, static_cast<int>((r.right - inset) * scale));
		const int y0 = static_cast<int>((r.top + kPatchDips / 3) * scale), y1 = static_cast<int>((r.bottom - kPatchDips / 3) * scale);
		double sum[3]{}; int lo[3]{ 255, 255, 255 }, hi[3]{}; int n = 0;
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
			{
				const uint8_t* px = &pixels[(static_cast<size_t>(y) * w + x) * 4];
				const int rgb[3]{ px[2], px[1], px[0] };
				for (int c = 0; c < 3; ++c) { sum[c] += rgb[c]; lo[c] = std::min(lo[c], rgb[c]); hi[c] = std::max(hi[c], rgb[c]); }
				++n;
			}
		float m[3]{}; int spread = 0;
		for (int c = 0; c < 3; ++c) { m[c] = static_cast<float>(sum[c] / std::max(n, 1)); spread = std::max(spread, hi[c] - lo[c]); }
		const auto e = expectedFor(p);
		float dLin = 0, dGam = 0;
		for (int c = 0; c < 3; ++c) { dLin = std::max(dLin, std::fabs(m[c] - e.linear[c])); dGam = std::max(dGam, std::fabs(m[c] - e.gamma[c])); }
		worstLin = std::max(worstLin, dLin); worstGam = std::max(worstGam, dGam);
		char meas[32], lin[32], gam[32];
		snprintf(meas, sizeof meas, "%3.0f %3.0f %3.0f", m[0], m[1], m[2]);
		snprintf(lin, sizeof lin, "%3.0f %3.0f %3.0f", e.linear[0], e.linear[1], e.linear[2]);
		snprintf(gam, sizeof gam, "%3.0f %3.0f %3.0f", e.gamma[0], e.gamma[1], e.gamma[2]);
		printf("%-18s %-13s %-13s %-13s %7.1f %7.1f %d\n", p.name.c_str(), meas, lin, gam, dLin, dGam, spread);
		if (csv) fprintf(csv, "%s,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d\n", p.name.c_str(), m[0], m[1], m[2], e.linear[0], e.linear[1], e.linear[2], e.gamma[0], e.gamma[1], e.gamma[2], spread);
	}
	if (csv) fclose(csv);
	printf("\nworst |measured - expected|: linear-light model %.1f, gamma-space model %.1f (8-bit code values)\n", worstLin, worstGam);

	CoUninitialize();
	return 0;
}
