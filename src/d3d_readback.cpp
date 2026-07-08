#include "d3d_readback.h"

#include <cstring>
#include <wrl/client.h>

namespace sc {

bool ReadTextureToImage(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *src, UINT staging_w, UINT staging_h,
                        int read_w, int read_h, bool copy_region,
                        const char *where, ImageBuffer *out, ErrorInfo *err) {
  D3D11_TEXTURE2D_DESC desc{};
  src->GetDesc(&desc);
  desc.Width = staging_w;
  desc.Height = staging_h;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, &staging);
  if (FAILED(hr)) {
    return FailHr(err, "CreateTexture2D staging failed", where, hr);
  }

  if (copy_region) {
    D3D11_BOX src_box{};
    src_box.right = staging_w;
    src_box.bottom = staging_h;
    src_box.back = 1;
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, src, 0,
                                   &src_box);
  } else {
    context->CopyResource(staging.Get(), src);
  }

  D3D11_MAPPED_SUBRESOURCE map{};
  hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map);
  if (FAILED(hr)) {
    return FailHr(err, "Map staging failed", where, hr);
  }

  out->width = read_w;
  out->height = read_h;
  out->row_pitch = read_w * 4;
  out->bgra.resize(static_cast<size_t>(out->row_pitch) *
                   static_cast<size_t>(read_h));

  if (map.RowPitch == static_cast<UINT>(out->row_pitch)) {
    memcpy(out->bgra.data(), map.pData, out->bgra.size());
  } else {
    for (int y = 0; y < read_h; ++y) {
      const uint8_t *row_src = static_cast<const uint8_t *>(map.pData) +
                               static_cast<size_t>(y) * map.RowPitch;
      uint8_t *dst = out->bgra.data() + static_cast<size_t>(y) *
                                            static_cast<size_t>(out->row_pitch);
      memcpy(dst, row_src, static_cast<size_t>(out->row_pitch));
    }
  }

  context->Unmap(staging.Get(), 0);
  return true;
}

} // namespace sc
