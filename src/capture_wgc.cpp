#include "capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <cstring>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <wrl/client.h>

namespace sc {

namespace {

using Microsoft::WRL::ComPtr;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics;
using namespace Windows::Graphics::Capture;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;

IDirect3DDevice CreateWinRtD3DDevice(ID3D11Device *d3d_device, ErrorInfo *err) {
  ComPtr<IDXGIDevice> dxgi_device;
  HRESULT hr = d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
  if (FAILED(hr)) {
    *err =
        ErrorInfo{"QueryInterface IDXGIDevice failed", "CreateWinRtD3DDevice",
                  static_cast<uint32_t>(hr), std::nullopt};
    return nullptr;
  }

  ComPtr<::IInspectable> inspectable;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
                                            inspectable.GetAddressOf());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateDirect3D11DeviceFromDXGIDevice failed",
                     "CreateWinRtD3DDevice", static_cast<uint32_t>(hr),
                     std::nullopt};
    return nullptr;
  }

  return inspectable.as<IDirect3DDevice>();
}

bool CreateCaptureItemFromHwnd(HWND hwnd, GraphicsCaptureItem *item,
                               ErrorInfo *err) {
  auto interop = get_activation_factory<GraphicsCaptureItem,
                                        IGraphicsCaptureItemInterop>();
  com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> abi_item;
  HRESULT hr = interop->CreateForWindow(
      hwnd, guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      abi_item.put_void());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateForWindow failed", "CreateCaptureItemFromHwnd",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  *item = abi_item.as<GraphicsCaptureItem>();
  return true;
}

bool CreateCaptureItemFromMonitor(HMONITOR hmon, GraphicsCaptureItem *item,
                                  ErrorInfo *err) {
  auto interop = get_activation_factory<GraphicsCaptureItem,
                                        IGraphicsCaptureItemInterop>();
  com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> abi_item;
  HRESULT hr = interop->CreateForMonitor(
      hmon, guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      abi_item.put_void());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateForMonitor failed", "CreateCaptureItemFromMonitor",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  *item = abi_item.as<GraphicsCaptureItem>();
  return true;
}

bool CopyFrameToImage(const Direct3D11CaptureFrame &frame, ID3D11Device *device,
                      ID3D11DeviceContext *context, const Rect &origin_rect,
                      ImageBuffer *out, ErrorInfo *err) {
  auto surface = frame.Surface();
  auto access = surface.as<IDirect3DDxgiInterfaceAccess>();

  ComPtr<ID3D11Texture2D> tex;
  HRESULT hr = access->GetInterface(IID_PPV_ARGS(&tex));
  if (FAILED(hr)) {
    *err = ErrorInfo{"GetInterface(ID3D11Texture2D) failed", "CopyFrameToImage",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;

  ComPtr<ID3D11Texture2D> staging;
  hr = device->CreateTexture2D(&desc, nullptr, &staging);
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateTexture2D staging failed", "CopyFrameToImage",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  context->CopyResource(staging.Get(), tex.Get());

  D3D11_MAPPED_SUBRESOURCE map{};
  hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map);
  if (FAILED(hr)) {
    *err = ErrorInfo{"Map staging failed", "CopyFrameToImage",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  out->width = static_cast<int>(desc.Width);
  out->height = static_cast<int>(desc.Height);
  out->row_pitch = out->width * 4;
  out->origin_x = origin_rect.left;
  out->origin_y = origin_rect.top;
  out->bgra.resize(static_cast<size_t>(out->row_pitch * out->height));

  for (int y = 0; y < out->height; ++y) {
    const uint8_t *src = static_cast<const uint8_t *>(map.pData) +
                         static_cast<size_t>(y * map.RowPitch);
    uint8_t *dst = out->bgra.data() + static_cast<size_t>(y * out->row_pitch);
    memcpy(dst, src, static_cast<size_t>(out->row_pitch));
  }

  context->Unmap(staging.Get(), 0);
  return true;
}

} // namespace

bool CaptureWithWgc(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err) {
  winrt::init_apartment(apartment_type::multi_threaded);

  if (!GraphicsCaptureSession::IsSupported()) {
    *err = ErrorInfo{"GraphicsCaptureSession::IsSupported false",
                     "CaptureWithWgc", std::nullopt, std::nullopt};
    return false;
  }

  ComPtr<ID3D11Device> d3d_device;
  ComPtr<ID3D11DeviceContext> d3d_context;
  HRESULT hr =
      D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                        D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
  if (FAILED(hr)) {
    *err = ErrorInfo{"D3D11CreateDevice failed", "CaptureWithWgc",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  auto winrt_device = CreateWinRtD3DDevice(d3d_device.Get(), err);
  if (!winrt_device) {
    return false;
  }

  GraphicsCaptureItem item{nullptr};
  if (ctx.method == "wgc-window") {
    if (!ctx.window.has_value()) {
      *err = ErrorInfo{"wgc-window needs window target", "CaptureWithWgc",
                       std::nullopt, std::nullopt};
      return false;
    }
    if (!CreateCaptureItemFromHwnd(ctx.window->hwnd, &item, err)) {
      return false;
    }
  } else if (ctx.method == "wgc-monitor") {
    if (!ctx.monitor.has_value()) {
      *err = ErrorInfo{"wgc-monitor needs monitor target", "CaptureWithWgc",
                       std::nullopt, std::nullopt};
      return false;
    }
    if (!CreateCaptureItemFromMonitor(ctx.monitor->hmon, &item, err)) {
      return false;
    }
  } else {
    *err = ErrorInfo{"unknown wgc method", "CaptureWithWgc", std::nullopt,
                     std::nullopt};
    return false;
  }

  auto size = item.Size();
  auto frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
      winrt_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
  auto session = frame_pool.CreateCaptureSession(item);

  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ev) {
    *err = ErrorInfo{"CreateEvent failed", "CaptureWithWgc", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
  }

  Direct3D11CaptureFrame captured{nullptr};
  auto revoker =
      frame_pool.FrameArrived(auto_revoke, [&](auto &sender, auto &) {
        captured = sender.TryGetNextFrame();
        SetEvent(ev);
      });

  session.StartCapture();
  DWORD wr = WaitForSingleObject(ev, static_cast<DWORD>(ctx.common.timeout_ms));
  session.Close();
  frame_pool.Close();
  CloseHandle(ev);

  if (wr != WAIT_OBJECT_0 || !captured) {
    *err = ErrorInfo{"WGC frame timeout", "CaptureWithWgc", std::nullopt,
                     std::nullopt};
    return false;
  }

  Rect origin = ctx.capture_rect_screen;
  if (ctx.method == "wgc-window" && ctx.window.has_value()) {
    origin = ctx.window->rect;
  }
  return CopyFrameToImage(captured, d3d_device.Get(), d3d_context.Get(), origin,
                          out, err);
}

} // namespace sc
