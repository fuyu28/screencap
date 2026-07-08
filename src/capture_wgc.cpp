#include "capture.h"
#include "d3d_readback.h"
#include "image_stats.h"
#include "logging.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
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
    FailHr(err, "QueryInterface IDXGIDevice failed", "CreateWinRtD3DDevice",
           hr);
    return nullptr;
  }

  winrt::com_ptr<::IInspectable> inspectable;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
                                            inspectable.put());
  if (FAILED(hr)) {
    FailHr(err, "CreateDirect3D11DeviceFromDXGIDevice failed",
           "CreateWinRtD3DDevice", hr);
    return nullptr;
  }

  return inspectable.as<wgd11::IDirect3DDevice>();
}

// `create` invokes the interop factory (CreateForWindow / CreateForMonitor)
// and returns its HRESULT; `what` names the call for error reporting.
template <typename CreateFn>
bool CreateCaptureItem(CreateFn create, const char *what,
                       wgc::GraphicsCaptureItem *item, ErrorInfo *err) {
  auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>
      abi_item;
  HRESULT hr = create(interop, abi_item.put_void());
  if (FAILED(hr)) {
    return FailHr(err, std::string(what) + " failed", "CreateCaptureItem", hr);
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
    return FailHr(err, "GetInterface(ID3D11Texture2D) failed",
                  "CopyFrameToImage", hr);
  }

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);
  UINT w = desc.Width;
  UINT h = desc.Height;
  if (use_content_size) {
    const auto content_size = frame.ContentSize();
    if (content_size.Width <= 0 || content_size.Height <= 0) {
      return Fail(err, "invalid WGC ContentSize", "CopyFrameToImage");
    }
    w = std::min(w, static_cast<UINT>(content_size.Width));
    h = std::min(h, static_cast<UINT>(content_size.Height));
  }

  if (!ReadTextureToImage(device, context, tex.Get(), w, h,
                          static_cast<int>(w), static_cast<int>(h),
                          /*copy_region=*/use_content_size, "CopyFrameToImage",
                          out, err)) {
    return false;
  }
  out->origin_x = origin_rect.left;
  out->origin_y = origin_rect.top;
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

// Runs `fn` when the guard goes out of scope, regardless of how the scope is
// exited (normal return or exception), so WinRT/Win32 resources are always
// released even if a winrt::hresult_error is thrown mid-capture.
template <typename F>
struct ScopeExit {
  F fn;
  ~ScopeExit() {
    try {
      fn();
    } catch (...) {
    }
  }
};

template <typename F>
ScopeExit(F) -> ScopeExit<F>;

} // namespace

bool CaptureWithWgc(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err) {
  try {
    WgcLog(ctx.logger, "init_apartment");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    WgcLog(ctx.logger, "check supported");
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
      return Fail(err, "GraphicsCaptureSession::IsSupported false",
                  "CaptureWithWgc");
    }

    WgcLog(ctx.logger, "create d3d device");
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3d_device, nullptr, &d3d_context);
    if (FAILED(hr)) {
      return FailHr(err, "D3D11CreateDevice failed", "CaptureWithWgc", hr);
    }

    WgcLog(ctx.logger, "create winrt d3d device");
    auto winrt_device = CreateWinRtD3DDevice(d3d_device.Get(), err);
    if (!winrt_device) {
      return false;
    }

    const auto &method = ctx.cap.method;
    const bool is_window = method == "wgc-window" || method == "wgc-window2";
    const bool is_monitor = method == "wgc-monitor" || method == "wgc-monitor2";
    const auto item_guid =
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>();

    wgc::GraphicsCaptureItem item{nullptr};
    if (is_window) {
      WgcLog(ctx.logger, "create item for window");
      if (!ctx.window.has_value()) {
        return Fail(err, "wgc-window needs window target", "CaptureWithWgc");
      }
      HWND hwnd = ctx.window->hwnd;
      if (!CreateCaptureItem(
              [&](auto &interop, void **out_item) {
                return interop->CreateForWindow(hwnd, item_guid, out_item);
              },
              "CreateForWindow", &item, err)) {
        return false;
      }
    } else if (is_monitor) {
      WgcLog(ctx.logger, "create item for monitor");
      if (!ctx.monitor.has_value()) {
        return Fail(err, "wgc-monitor needs monitor target", "CaptureWithWgc");
      }
      HMONITOR hmon = ctx.monitor->hmon;
      if (!CreateCaptureItem(
              [&](auto &interop, void **out_item) {
                return interop->CreateForMonitor(hmon, item_guid, out_item);
              },
              "CreateForMonitor", &item, err)) {
        return false;
      }
    } else {
      return Fail(err, "unknown wgc method", "CaptureWithWgc");
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
      return FailWin32(err, "CreateEvent failed", "CaptureWithWgc");
    }

    // Guarantees session/frame_pool/ev are released on every exit path,
    // including a winrt::hresult_error thrown by StartCapture() or by
    // CopyFrameToImage's surface.as<>() call. Declared before `captured`,
    // `frame_mutex`, and `revoker` so it is destroyed after them: the frame
    // handler is revoked and the last frame released before the session,
    // frame pool, and event are closed.
    ScopeExit cleanup{[&] {
      WgcLog(ctx.logger, "close session");
      session.Close();
      WgcLog(ctx.logger, "close frame pool");
      frame_pool.Close();
      WgcLog(ctx.logger, "close event");
      CloseHandle(ev);
    }};

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
            SetEvent(ev);
          }
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
      if (is_window && ctx.window.has_value()) {
        // A window GraphicsCaptureItem's surface covers the DWM extended
        // frame bounds (client area plus visible chrome, excluding the
        // invisible resize border), not the raw GetWindowRect rect.
        origin = ctx.window->dwm_frame_rect;
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

    if (!have_candidate) {
      if (!copy_err.message.empty()) {
        *err = copy_err;
        return false;
      }
      return Fail(err, "WGC frame timeout", "CaptureWithWgc");
    }

    *out = std::move(best);
    return true;
  } catch (const winrt::hresult_error &e) {
    return FailHr(err, winrt::to_string(e.message()), "CaptureWithWgc",
                  e.code());
  } catch (const std::exception &e) {
    return Fail(err, e.what(), "CaptureWithWgc");
  }
}

} // namespace sc
