#include "capture.h"
#include "image_stats.h"
#include "logging.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <cstring>
#include <exception>
#include <mutex>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <wrl/client.h>

namespace sc {

namespace {

using Microsoft::WRL::ComPtr;

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;

wgd11::IDirect3DDevice CreateWinRtD3DDevice(ID3D11Device *d3d_device,
                                            ErrorInfo *err) {
  ComPtr<IDXGIDevice> dxgi_device;
  HRESULT hr = d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
  if (FAILED(hr)) {
    *err =
        ErrorInfo{"QueryInterface IDXGIDevice failed", "CreateWinRtD3DDevice",
                  static_cast<uint32_t>(hr), std::nullopt};
    return nullptr;
  }

  winrt::com_ptr<::IInspectable> inspectable;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
                                            inspectable.put());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateDirect3D11DeviceFromDXGIDevice failed",
                     "CreateWinRtD3DDevice", static_cast<uint32_t>(hr),
                     std::nullopt};
    return nullptr;
  }

  return inspectable.as<wgd11::IDirect3DDevice>();
}

bool CreateCaptureItemFromHwnd(HWND hwnd, wgc::GraphicsCaptureItem *item,
                               ErrorInfo *err) {
  auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>
      abi_item;
  HRESULT hr = interop->CreateForWindow(
      hwnd,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      abi_item.put_void());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateForWindow failed", "CreateCaptureItemFromHwnd",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  *item = abi_item.as<wgc::GraphicsCaptureItem>();
  return true;
}

bool CreateCaptureItemFromMonitor(HMONITOR hmon, wgc::GraphicsCaptureItem *item,
                                  ErrorInfo *err) {
  auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>
      abi_item;
  HRESULT hr = interop->CreateForMonitor(
      hmon,
      winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
      abi_item.put_void());
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateForMonitor failed", "CreateCaptureItemFromMonitor",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  *item = abi_item.as<wgc::GraphicsCaptureItem>();
  return true;
}

bool CopyFrameToImage(const wgc::Direct3D11CaptureFrame &frame,
                      ID3D11Device *device,
                      ID3D11DeviceContext *context, const Rect &origin_rect,
                      bool use_content_size, ImageBuffer *out,
                      ErrorInfo *err) {
  auto surface = frame.Surface();
  auto access =
      surface.as<::Windows::Graphics::DirectX::Direct3D11::
                     IDirect3DDxgiInterfaceAccess>();

  ComPtr<ID3D11Texture2D> tex;
  HRESULT hr = access->GetInterface(IID_PPV_ARGS(&tex));
  if (FAILED(hr)) {
    *err = ErrorInfo{"GetInterface(ID3D11Texture2D) failed", "CopyFrameToImage",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  if (use_content_size) {
    const auto content_size = frame.ContentSize();
    if (content_size.Width <= 0 || content_size.Height <= 0) {
      *err = ErrorInfo{"invalid WGC ContentSize", "CopyFrameToImage",
                       std::nullopt, std::nullopt};
      return false;
    }
    desc.Width =
        std::min(desc.Width, static_cast<UINT>(content_size.Width));
    desc.Height =
        std::min(desc.Height, static_cast<UINT>(content_size.Height));
  }
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

  if (use_content_size) {
    D3D11_BOX src_box{};
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.right = desc.Width;
    src_box.bottom = desc.Height;
    src_box.back = 1;
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0,
                                   &src_box);
  } else {
    context->CopyResource(staging.Get(), tex.Get());
  }

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

bool IsProbablyUsableFrame(const ImageBuffer &img) {
  const ImageStats stats = ComputeImageStats(img);
  return stats.transparent_ratio < 0.98 && stats.black_ratio < 0.98;
}

void WgcLog(Logger *logger, const std::string &msg) {
  if (logger) {
    logger->Log(LogLevel::kDebug, "wgc: " + msg);
  }
}

} // namespace

bool CaptureWithWgc(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err) {
  try {
    WgcLog(ctx.logger, "init_apartment");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    WgcLog(ctx.logger, "check supported");
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
      *err = ErrorInfo{"GraphicsCaptureSession::IsSupported false",
                       "CaptureWithWgc", std::nullopt, std::nullopt};
      return false;
    }

    WgcLog(ctx.logger, "create d3d device");
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3d_device, nullptr, &d3d_context);
    if (FAILED(hr)) {
      *err = ErrorInfo{"D3D11CreateDevice failed", "CaptureWithWgc",
                       static_cast<uint32_t>(hr), std::nullopt};
      return false;
    }

    WgcLog(ctx.logger, "create winrt d3d device");
    auto winrt_device = CreateWinRtD3DDevice(d3d_device.Get(), err);
    if (!winrt_device) {
      return false;
    }

    wgc::GraphicsCaptureItem item{nullptr};
    if (ctx.method == "wgc-window" || ctx.method == "wgc-window2") {
      WgcLog(ctx.logger, "create item for window");
      if (!ctx.window.has_value()) {
        *err = ErrorInfo{"wgc-window needs window target", "CaptureWithWgc",
                         std::nullopt, std::nullopt};
        return false;
      }
      if (!CreateCaptureItemFromHwnd(ctx.window->hwnd, &item, err)) {
        return false;
      }
    } else if (ctx.method == "wgc-monitor" || ctx.method == "wgc-monitor2") {
      WgcLog(ctx.logger, "create item for monitor");
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
    WgcLog(ctx.logger, "item size=" + std::to_string(size.Width) + "x" +
                           std::to_string(size.Height));
    WgcLog(ctx.logger, "create frame pool");
    auto frame_pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrt_device, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
    WgcLog(ctx.logger, "create session");
    auto session = frame_pool.CreateCaptureSession(item);

    WgcLog(ctx.logger, "create event");
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) {
      *err = ErrorInfo{"CreateEvent failed", "CaptureWithWgc", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      return false;
    }

    // FrameArrived fires on a free-threaded pool worker, so all access to
    // `captured` is serialized through `frame_mutex`. The main loop moves the
    // frame out under the lock before touching it, which prevents a later frame
    // from releasing the object we are still copying (use-after-free). The
    // handler does no logging: Logger is not thread-safe.
    wgc::Direct3D11CaptureFrame captured{nullptr};
    std::mutex frame_mutex;
    auto revoker =
        frame_pool.FrameArrived(winrt::auto_revoke, [&](auto &sender, auto &) {
          auto frame = sender.TryGetNextFrame();
          {
            std::lock_guard<std::mutex> lock(frame_mutex);
            captured = frame;
          }
          SetEvent(ev);
        });

    WgcLog(ctx.logger, "start capture");
    session.StartCapture();
    ImageBuffer best;
    ErrorInfo copy_err;
    bool have_candidate = false;
    constexpr int kMaxFrames = 5;
    for (int frame_index = 0; frame_index < kMaxFrames; ++frame_index) {
      DWORD wr =
          WaitForSingleObject(ev, static_cast<DWORD>(ctx.common.timeout_ms));

      wgc::Direct3D11CaptureFrame frame{nullptr};
      {
        std::lock_guard<std::mutex> lock(frame_mutex);
        frame = std::move(captured);
        ResetEvent(ev);
      }
      if (wr != WAIT_OBJECT_0 || !frame) {
        WgcLog(ctx.logger, "wait did not produce frame");
        continue;
      }
      WgcLog(ctx.logger, "frame arrived");

      Rect origin = ctx.capture_rect_screen;
      if ((ctx.method == "wgc-window" || ctx.method == "wgc-window2") &&
          ctx.window.has_value()) {
        origin = ctx.window->rect;
      }

      ImageBuffer candidate;
      if (!CopyFrameToImage(frame, d3d_device.Get(), d3d_context.Get(),
                            origin, true, &candidate, &copy_err)) {
        WgcLog(ctx.logger, "copy frame failed: " + copy_err.message);
        continue;
      }

      best = std::move(candidate);
      WgcLog(ctx.logger, "candidate size=" + std::to_string(best.width) + "x" +
                             std::to_string(best.height));
      have_candidate = true;
      if (IsProbablyUsableFrame(best)) {
        break;
      }
    }
    WgcLog(ctx.logger, "revoke frame handler");
    revoker.revoke();
    WgcLog(ctx.logger, "release captured frame");
    captured = nullptr;
    WgcLog(ctx.logger, "close session");
    session.Close();
    WgcLog(ctx.logger, "close frame pool");
    frame_pool.Close();
    WgcLog(ctx.logger, "close event");
    CloseHandle(ev);

    if (!have_candidate) {
      if (!copy_err.message.empty()) {
        *err = copy_err;
        return false;
      }
      *err = ErrorInfo{"WGC frame timeout", "CaptureWithWgc", std::nullopt,
                       std::nullopt};
      return false;
    }

    *out = std::move(best);
    return true;
  } catch (const winrt::hresult_error &e) {
    *err = ErrorInfo{winrt::to_string(e.message()), "CaptureWithWgc",
                     static_cast<uint32_t>(e.code()), std::nullopt};
    return false;
  } catch (const std::exception &e) {
    *err = ErrorInfo{e.what(), "CaptureWithWgc", std::nullopt, std::nullopt};
    return false;
  }
}

} // namespace sc
