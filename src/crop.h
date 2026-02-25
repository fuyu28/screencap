#pragma once

#include "cli.h"
#include "common.h"
#include "window_enum.h"

namespace sc {

Rect ResolveCropRectScreen(CropMode mode, const std::optional<CropRect>& manual, const WindowInfo* window,
                           const Rect& capture_screen_rect, const Pad& pad, ErrorInfo* err);
bool CropImageInPlace(const Rect& crop_screen_rect, ImageBuffer* img, ErrorInfo* err);

}  // namespace sc
