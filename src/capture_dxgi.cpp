#include "capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstring>
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
    *err = ErrorInfo{"CreateDXGIFactory1 failed", "FindOutputForMonitor",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
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
          *err = ErrorInfo{"QueryInterface IDXGIOutput1 failed",
                           "FindOutputForMonitor", static_cast<uint32_t>(hr),
                           std::nullopt};
          return false;
        }
        *out_adapter = adapter.Detach();
        *out_output = o1.Detach();
        *out_ai = static_cast<int>(ai);
        *out_oi = static_cast<int>(oi);
        return true;
      }
    }
  }

  *err = ErrorInfo{"monitor output not found", "FindOutputForMonitor",
                   std::nullopt, std::nullopt};
  return false;
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
    *err = ErrorInfo{"D3D11CreateDevice failed", "AcquireDupFrame",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  ComPtr<IDXGIOutputDuplication> dup;
  hr = output1->DuplicateOutput(device.Get(), &dup);
  if (FAILED(hr)) {
    *err = ErrorInfo{"DuplicateOutput failed", "AcquireDupFrame",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  DXGI_OUTDUPL_FRAME_INFO frame_info{};
  ComPtr<IDXGIResource> resource;
  hr = dup->AcquireNextFrame(static_cast<UINT>(timeout_ms), &frame_info,
                             &resource);
  if (FAILED(hr)) {
    *err = ErrorInfo{"AcquireNextFrame failed", "AcquireDupFrame",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  ComPtr<ID3D11Texture2D> tex;
  hr = resource.As(&tex);
  if (FAILED(hr)) {
    dup->ReleaseFrame();
    *err = ErrorInfo{"frame resource to texture failed", "AcquireDupFrame",
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
    dup->ReleaseFrame();
    *err = ErrorInfo{"CreateTexture2D staging failed", "AcquireDupFrame",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  context->CopyResource(staging.Get(), tex.Get());

  D3D11_MAPPED_SUBRESOURCE map{};
  hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map);
  if (FAILED(hr)) {
    dup->ReleaseFrame();
    *err = ErrorInfo{"Map staging failed", "AcquireDupFrame",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  const int w = Width(capture_rect);
  const int h = Height(capture_rect);
  out->width = w;
  out->height = h;
  out->row_pitch = w * 4;
  out->origin_x = capture_rect.left;
  out->origin_y = capture_rect.top;
  out->bgra.resize(static_cast<size_t>(w * h * 4));

  for (int y = 0; y < h; ++y) {
    const uint8_t *src = static_cast<const uint8_t *>(map.pData) +
                         static_cast<size_t>(y * map.RowPitch);
    uint8_t *dst = out->bgra.data() + static_cast<size_t>(y * out->row_pitch);
    memcpy(dst, src, static_cast<size_t>(w * 4));
  }

  context->Unmap(staging.Get(), 0);
  dup->ReleaseFrame();
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
    *err = ErrorInfo{"unable to resolve monitor for DXGI", "CaptureWithDxgi",
                     std::nullopt, std::nullopt};
    return false;
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
    *err = ErrorInfo{"GetMonitorInfo failed", "CaptureWithDxgi", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
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

  if (ctx.cap.force_alpha_255) {
    for (size_t i = 3; i < out->bgra.size(); i += 4) {
      out->bgra[i] = 0xFF;
    }
  }
  return true;
}

} // namespace sc
