#include "pch.h"

#include "ScreenCapturing.h"
#include "BGRATextureView.h"
#include "EdgeDetection.h"

class OwnedTextureView
{
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::com_ptr<ID3D11DeviceContext> context;

public:
    BGRATextureView view;
    OwnedTextureView(winrt::com_ptr<ID3D11Texture2D> _texture, winrt::com_ptr<ID3D11DeviceContext> _context) :
        texture{ std::move(_texture) }, context{ std::move(_context) }
    {
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        // TODO: It's unclear why this assert is here, since pixelFormat doesn't exist on this context.
        // assert(desc.Format == static_cast<DXGI_FORMAT>(pixelFormat));

        D3D11_MAPPED_SUBRESOURCE resource;
        winrt::check_hresult(context->Map(texture.get(), D3D11CalcSubresource(0, 0, 0), D3D11_MAP_READ, 0, &resource));

        const size_t texWidth = resource.RowPitch / 4;
        const size_t texHeight = resource.DepthPitch / texWidth / 4;
        view.pixels = static_cast<const uint32_t*>(resource.pData);

        view.width = texWidth;
        view.height = texHeight;
    }

    OwnedTextureView(OwnedTextureView&&) = default;
    OwnedTextureView& operator=(OwnedTextureView&&) = default;

    ~OwnedTextureView()
    {
        if (context && texture)
            context->Unmap(texture.get(), D3D11CalcSubresource(0, 0, 0));
    }
};

class D3DCaptureState final
{
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::IDirect3DDevice device;
    winrt::com_ptr<IDXGISwapChain1> swapchain;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::SizeInt32 frameSize;

    winrt::DirectXPixelFormat pixelFormat;
    winrt::Direct3D11CaptureFramePool framePool;
    winrt::GraphicsCaptureSession session;

    std::function<void(OwnedTextureView)> frameCallback;

    D3DCaptureState(winrt::com_ptr<ID3D11Device> d3dDevice,
                    winrt::IDirect3DDevice _device,
                    winrt::com_ptr<IDXGISwapChain1> _swapchain,
                    winrt::com_ptr<ID3D11DeviceContext> _context,
                    const winrt::GraphicsCaptureItem& item,
                    winrt::DirectXPixelFormat _pixelFormat);

    winrt::com_ptr<ID3D11Texture2D> CopyFrameToCPU(const winrt::com_ptr<ID3D11Texture2D>& texture);

    void OnFrameArrived(const winrt::Direct3D11CaptureFramePool& sender, const winrt::IInspectable&);

    void StartSessionInPreferredMode();

    std::mutex dtorMutex;

public:
    static std::unique_ptr<D3DCaptureState> Create(const winrt::GraphicsCaptureItem& item, const winrt::DirectXPixelFormat pixelFormat);

    ~D3DCaptureState();

    void StartCapture(std::function<void(OwnedTextureView)> _frameCallback);
    OwnedTextureView CaptureSingleFrame();

    void StopCapture();
};

D3DCaptureState::D3DCaptureState(winrt::com_ptr<ID3D11Device> _d3dDevice,
                                 winrt::IDirect3DDevice _device,
                                 winrt::com_ptr<IDXGISwapChain1> _swapchain,
                                 winrt::com_ptr<ID3D11DeviceContext> _context,
                                 const winrt::GraphicsCaptureItem& item,
                                 winrt::DirectXPixelFormat _pixelFormat) :
    d3dDevice{ std::move(_d3dDevice) },
    device{ std::move(_device) },
    swapchain{ std::move(_swapchain) },
    context{ std::move(_context) },
    frameSize{ item.Size() },
    pixelFormat{ std::move(_pixelFormat) },
    framePool{ winrt::Direct3D11CaptureFramePool::CreateFreeThreaded /*Create*/ (device, pixelFormat, 2, item.Size()) },
    session{ framePool.CreateCaptureSession(item) }
{
    framePool.FrameArrived({ this, &D3DCaptureState::OnFrameArrived });
}

winrt::com_ptr<ID3D11Texture2D> D3DCaptureState::CopyFrameToCPU(const winrt::com_ptr<ID3D11Texture2D>& frameTexture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    frameTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.BindFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> cpuTexture;
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, cpuTexture.put()));
    context->CopyResource(cpuTexture.get(), frameTexture.get());

    return cpuTexture;
}

template<typename T>
auto GetDXGIInterfaceFromObject(winrt::IInspectable const& object)
{
    auto access = object.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<T> result;
    winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
    return result;
}

void D3DCaptureState::OnFrameArrived(const winrt::Direct3D11CaptureFramePool& sender, const winrt::IInspectable&)
{
    // Prevent calling a callback on a partially destroyed state
    std::unique_lock callbackLock{ dtorMutex };

    bool resized = false;
    winrt::com_ptr<ID3D11Texture2D> texture;
    {
        auto frame = sender.TryGetNextFrame();
        winrt::check_bool(frame);

        if (auto newFrameSize = frame.ContentSize(); newFrameSize != frameSize)
        {
            winrt::check_hresult(swapchain->ResizeBuffers(2,
                                                          static_cast<uint32_t>(newFrameSize.Height),
                                                          static_cast<uint32_t>(newFrameSize.Width),
                                                          static_cast<DXGI_FORMAT>(pixelFormat),
                                                          0));
            frameSize = newFrameSize;
            resized = true;
        }

        winrt::check_hresult(swapchain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));
        auto gpuTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
        texture = CopyFrameToCPU(gpuTexture);
    }

    OwnedTextureView textureView{ texture, context };

    DXGI_PRESENT_PARAMETERS presentParameters = {};
    swapchain->Present1(1, 0, &presentParameters);

    frameCallback(std::move(textureView));

    if (resized)
    {
        framePool.Recreate(device, pixelFormat, 2, frameSize);
    }
}

std::unique_ptr<D3DCaptureState> D3DCaptureState::Create(const winrt::GraphicsCaptureItem& item, const winrt::DirectXPixelFormat pixelFormat)
{
    winrt::com_ptr<ID3D11Device> d3dDevice;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr =
        D3D11CreateDevice(nullptr,
                          D3D_DRIVER_TYPE_HARDWARE,
                          nullptr,
                          flags,
                          nullptr,
                          0,
                          D3D11_SDK_VERSION,
                          d3dDevice.put(),
                          nullptr,
                          nullptr);
    if (hr == DXGI_ERROR_UNSUPPORTED)
    {
        hr = D3D11CreateDevice(nullptr,
                               D3D_DRIVER_TYPE_WARP,
                               nullptr,
                               flags,
                               nullptr,
                               0,
                               D3D11_SDK_VERSION,
                               d3dDevice.put(),
                               nullptr,
                               nullptr);
    }
    winrt::check_hresult(hr);

    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    winrt::com_ptr<IInspectable> d3dDeviceInspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), d3dDeviceInspectable.put()));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<uint32_t>(item.Size().Width);
    desc.Height = static_cast<uint32_t>(item.Size().Height);
    desc.Format = static_cast<DXGI_FORMAT>(pixelFormat);
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    auto dxgiDevice2 = d3dDevice.as<IDXGIDevice2>();
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgiDevice->GetParent(winrt::guid_of<IDXGIAdapter>(), adapter.put_void()));
    winrt::com_ptr<IDXGIFactory2> factory;
    winrt::check_hresult(adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), factory.put_void()));

    winrt::com_ptr<IDXGISwapChain1> swapchain;
    winrt::check_hresult(factory->CreateSwapChainForComposition(d3dDevice.get(), &desc, nullptr, swapchain.put()));

    winrt::com_ptr<ID3D11DeviceContext> context;
    d3dDevice->GetImmediateContext(context.put());
    winrt::check_bool(context);

    // We must create the object in a heap, since we need to pin it in memory to receive callbacks
    auto statePtr = new D3DCaptureState{ d3dDevice,
                                         d3dDeviceInspectable.as<winrt::IDirect3DDevice>(),
                                         std::move(swapchain),
                                         std::move(context),
                                         item,
                                         pixelFormat };

    return std::unique_ptr<D3DCaptureState>{ statePtr };
}

D3DCaptureState::~D3DCaptureState()
{
    std::unique_lock callbackLock{ dtorMutex };
    StopCapture();
    framePool.Close();
}

void D3DCaptureState::StartSessionInPreferredMode()
{
    // Try disable border if possible (available on Windows ver >= 20348)
    if (auto session3 = session.try_as<winrt::IGraphicsCaptureSession3>())
    {
        session3.IsBorderRequired(false);
    }

    session.IsCursorCaptureEnabled(false);
    session.StartCapture();
}

void D3DCaptureState::StartCapture(std::function<void(OwnedTextureView)> _frameCallback)
{
    frameCallback = std::move(_frameCallback);
    StartSessionInPreferredMode();
}

OwnedTextureView D3DCaptureState::CaptureSingleFrame()
{
    std::optional<OwnedTextureView> result;
    wil::shared_event frameArrivedEvent(wil::EventOptions::ManualReset);

    frameCallback = [frameArrivedEvent, &result, this](OwnedTextureView tex) {
        if (result)
            return;

        StopCapture();
        result.emplace(std::move(tex));
        frameArrivedEvent.SetEvent();
    };

    StartSessionInPreferredMode();

    frameArrivedEvent.wait();

    assert(result.has_value());
    return std::move(*result);
}

void D3DCaptureState::StopCapture()
{
    session.Close();
}

void UpdateCaptureState(MeasureToolState& state, HWND targetWindow, const uint8_t pixelTolerance, const OwnedTextureView& textureView)
{
    MeasureToolState::State::CrossCoords cross;
    POINT cursorPos{};
    GetCursorPos(&cursorPos);
    ScreenToClient(targetWindow, &cursorPos);

    const bool cursorInLeftScreenHalf = cursorPos.x < textureView.view.width / 2;
    const bool cursorInTopScreenHalf = cursorPos.y < textureView.view.height / 2;

    const RECT bounds = DetectEdges(textureView.view, cursorPos, pixelTolerance);

    cross.hLineStart.x = static_cast<float>(bounds.left);
    cross.hLineEnd.x = static_cast<float>(bounds.right);
    cross.hLineStart.y = cross.hLineEnd.y = static_cast<float>(cursorPos.y);

    cross.vLineStart.x = cross.vLineEnd.x = static_cast<float>(cursorPos.x);
    cross.vLineStart.y = static_cast<float>(bounds.top);
    cross.vLineEnd.y = static_cast<float>(bounds.bottom);

    state.Access([&](MeasureToolState::State& state) {
        state.cross = cross;
        state.cursorInLeftScreenHalf = cursorInLeftScreenHalf;
        state.cursorInTopScreenHalf = cursorInTopScreenHalf;
        state.cursorPos = cursorPos;
    });
}

void StartCapturingThread(MeasureToolState& state, HWND targetWindow, HMONITOR targetMonitor)
{
    SpawnLoggedThread([&state, targetMonitor, targetWindow] {
        winrt::check_pointer(targetMonitor);

        auto captureInterop = winrt::get_activation_factory<
            winrt::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

        winrt::GraphicsCaptureItem item = nullptr;

        winrt::check_hresult(captureInterop->CreateForMonitor(
            targetMonitor,
            winrt::guid_of<winrt::GraphicsCaptureItem>(),
            winrt::put_abi(item)));

        auto captureState = D3DCaptureState::Create(item, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized);

        bool stopCapturing = false;

        uint8_t pixelTolerance = 1;
        bool continuousCapture = false;
        state.Access([&](MeasureToolState::State& state) {
            pixelTolerance = state.pixelTolerance;
            continuousCapture = state.continuousCapture;
        });

        constexpr size_t TARGET_FRAMERATE = 120;
        constexpr auto TARGET_FRAME_DURATION = std::chrono::milliseconds{ 1000 } / TARGET_FRAMERATE;

        if (continuousCapture)
        {
            captureState->StartCapture([&, targetWindow, pixelTolerance](OwnedTextureView textureView) {
                UpdateCaptureState(state, targetWindow, pixelTolerance, textureView);
            });

            while (!stopCapturing)
            {
                std::this_thread::sleep_for(TARGET_FRAME_DURATION);
                state.Access([&](MeasureToolState::State& state) {
                    stopCapturing = state.stopCapturing;
                });
            }
            captureState->StopCapture();
        }
        else
        {
            const auto textureView = captureState->CaptureSingleFrame();
            while (!stopCapturing)
            {
                const auto now = std::chrono::high_resolution_clock::now();
                UpdateCaptureState(state, targetWindow, pixelTolerance, textureView);

                state.Access([&](MeasureToolState::State& state) {
                    stopCapturing = state.stopCapturing;
                });

                const auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - now);
                if (frameTime < TARGET_FRAME_DURATION)
                {
                    std::this_thread::sleep_for(TARGET_FRAME_DURATION - frameTime);
                }
            }
        }
    }, L"Screen Capture thread");
}
