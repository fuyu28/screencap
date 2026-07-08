#pragma once

#include "common/common.h"

#include <d3d11.h>

namespace sc {

// Copies pixels from `src` through a CPU staging texture into out->bgra
// (BGRA, row_pitch = read_w * 4). The staging texture is created as
// staging_w x staging_h; read_w x read_h pixels are then read out of the
// mapped memory. When copy_region is true only the staging_w x staging_h
// region of `src` is copied (CopySubresourceRegion), otherwise the whole
// resource is copied. out->origin_x/origin_y are left for the caller.
// `where` is used for ErrorInfo reporting.
bool ReadTextureToImage(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *src, UINT staging_w, UINT staging_h,
                        int read_w, int read_h, bool copy_region,
                        const char *where, ImageBuffer *out, ErrorInfo *err);

} // namespace sc
