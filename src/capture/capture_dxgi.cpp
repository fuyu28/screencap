#include "capture/capture.h"
#include "capture/d3d_readback.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>

#include <wrl/client.h>

namespace sc {

namespace {

using Microsoft::WRL::ComPtr;

bool FindOutputForMonitor(HMONITOR hmon, IDXGIAdapter1 **out_adapter,
                          IDXGIOutput1 **out_output, int *out_ai, int *out_oi,
                          ErrorInfo *err) {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return FailHr(err, "CreateDXGIFactory1 failed", "FindOutputForMonitor",
                  hr);
  }

  for (UINT ai = 0;; ++ai) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND)
      break;

    for (UINT oi = 0;; ++oi) {
      ComPtr<IDXGIOutput> output;
      if (adapter->EnumOutputs(oi, &output) == DXGI_ERROR_NOT_FOUND)
        break;

      DXGI_OUTPUT_DESC desc{};
      output->GetDesc(&desc);
      if (desc.Monitor == hmon) {
        ComPtr<IDXGIOutput1> o1;
        hr = output.As(&o1);
        if (FAILED(hr)) {
          return FailHr(err, "QueryInterface IDXGIOutput1 failed",
                        "FindOutputForMonitor", hr);
        }
        *out_adapter = adapter.Detach();
        *out_output = o1.Detach();
        *out_ai = static_cast<int>(ai);
        *out_oi = static_cast<int>(oi);
        return true;
      }
    }
  }

  return Fail(err, "monitor output not found", "FindOutputForMonitor");
}

bool AcquireDupFrame(IDXGIOutput1 *output1, IDXGIAdapter1 *adapter,
                     int timeout_ms, Rect capture_rect, ImageBuffer *out,
                     ErrorInfo *err) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, nullptr, &context);
  if (FAILED(hr)) {
    return FailHr(err, "D3D11CreateDevice failed", "AcquireDupFrame", hr);
  }

  ComPtr<IDXGIOutputDuplication> dup;
  hr = output1->DuplicateOutput(device.Get(), &dup);
  if (FAILED(hr)) {
    return FailHr(err, "DuplicateOutput failed", "AcquireDupFrame", hr);
  }

  DXGI_OUTDUPL_FRAME_INFO frame_info{};
  ComPtr<IDXGIResource> resource;
  hr = dup->AcquireNextFrame(static_cast<UINT>(timeout_ms), &frame_info,
                             &resource);
  if (FAILED(hr)) {
    return FailHr(err, "AcquireNextFrame failed", "AcquireDupFrame", hr);
  }

  ComPtr<ID3D11Texture2D> tex;
  hr = resource.As(&tex);
  if (FAILED(hr)) {
    dup->ReleaseFrame();
    return FailHr(err, "frame resource to texture failed", "AcquireDupFrame",
                  hr);
  }

  D3D11_TEXTURE2D_DESC desc{};
  tex->GetDesc(&desc);

  // rcMonitor (logical coords) can disagree with the duplication surface's
  // physical pixel dimensions under rotation or DPI virtualization, so clamp
  // the requested read region to what the staging texture actually has.
  const int read_w = std::min(Width(capture_rect), static_cast<int>(desc.Width));
  const int read_h =
      std::min(Height(capture_rect), static_cast<int>(desc.Height));

  const bool ok = ReadTextureToImage(
      device.Get(), context.Get(), tex.Get(), desc.Width, desc.Height, read_w,
      read_h, /*copy_region=*/false, "AcquireDupFrame", out, err);
  dup->ReleaseFrame();
  if (!ok) {
    return false;
  }
  out->origin_x = capture_rect.left;
  out->origin_y = capture_rect.top;
  return true;
}

} // namespace

bool CaptureWithDxgi(const CaptureContext &ctx, ImageBuffer *out,
                     int *out_adapter_index, int *out_output_index,
                     ErrorInfo *err) {
  HMONITOR hmon = nullptr;
  if (ctx.monitor.has_value()) {
    hmon = ctx.monitor->hmon;
  } else if (ctx.window.has_value()) {
    hmon = MonitorFromWindow(ctx.window->hwnd, MONITOR_DEFAULTTONEAREST);
  }
  if (!hmon) {
    return Fail(err, "unable to resolve monitor for DXGI", "CaptureWithDxgi");
  }

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput1> output;
  int ai = -1;
  int oi = -1;
  if (!FindOutputForMonitor(hmon, &adapter, &output, &ai, &oi, err)) {
    return false;
  }

  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(hmon, &mi)) {
    return FailWin32(err, "GetMonitorInfo failed", "CaptureWithDxgi");
  }
  Rect monitor_rect = ToRect(mi.rcMonitor);

  ImageBuffer full;
  if (!AcquireDupFrame(output.Get(), adapter.Get(), ctx.common.timeout_ms,
                       monitor_rect, &full, err)) {
    return false;
  }

  *out = std::move(full);
  *out_adapter_index = ai;
  *out_output_index = oi;

  return true;
}

} // namespace sc
