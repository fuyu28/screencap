#include "crop.h"

#include <algorithm>
#include <cstring>

namespace sc {

namespace {

Rect Intersect(const Rect& a, const Rect& b) {
  return Rect{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

}  // namespace

Rect ResolveCropRectScreen(CropMode mode, const std::optional<CropRect>& manual, const WindowInfo* window,
                           const Rect& capture_screen_rect, const Pad& pad, ErrorInfo* err) {
  Rect base{};
  switch (mode) {
    case CropMode::kNone:
      base = capture_screen_rect;
      break;
    case CropMode::kWindow:
      if (!window) {
        *err = ErrorInfo{"crop window requested but no window target", "ResolveCropRectScreen", std::nullopt, std::nullopt};
        return {};
      }
      base = window->rect;
      break;
    case CropMode::kClient:
      if (!window) {
        *err = ErrorInfo{"crop client requested but no window target", "ResolveCropRectScreen", std::nullopt, std::nullopt};
        return {};
      }
      base = window->client_rect_screen;
      break;
    case CropMode::kDwmFrame:
      if (!window) {
        *err = ErrorInfo{"crop dwm-frame requested but no window target", "ResolveCropRectScreen", std::nullopt, std::nullopt};
        return {};
      }
      base = window->dwm_frame_rect;
      break;
    case CropMode::kManual:
      if (!manual.has_value()) {
        *err = ErrorInfo{"manual crop missing rect", "ResolveCropRectScreen", std::nullopt, std::nullopt};
        return {};
      }
      base = Rect{manual->x, manual->y, manual->x + manual->w, manual->y + manual->h};
      break;
  }

  base.left -= pad.l;
  base.top -= pad.t;
  base.right += pad.r;
  base.bottom += pad.b;

  Rect clipped = Intersect(base, capture_screen_rect);
  if (!IsValidRect(clipped)) {
    *err = ErrorInfo{"crop rect is empty after intersection", "ResolveCropRectScreen", std::nullopt, std::nullopt};
    return {};
  }
  return clipped;
}

bool CropImageInPlace(const Rect& crop_screen_rect, ImageBuffer* img, ErrorInfo* err) {
  Rect img_rect{img->origin_x, img->origin_y, img->origin_x + img->width, img->origin_y + img->height};
  Rect c = Intersect(crop_screen_rect, img_rect);
  if (!IsValidRect(c)) {
    *err = ErrorInfo{"crop does not overlap image", "CropImageInPlace", std::nullopt, std::nullopt};
    return false;
  }

  const int x0 = c.left - img->origin_x;
  const int y0 = c.top - img->origin_y;
  const int nw = Width(c);
  const int nh = Height(c);

  std::vector<uint8_t> out(static_cast<size_t>(nw * nh * 4));
  for (int y = 0; y < nh; ++y) {
    const uint8_t* src = img->bgra.data() + static_cast<size_t>((y0 + y) * img->row_pitch + x0 * 4);
    uint8_t* dst = out.data() + static_cast<size_t>(y * nw * 4);
    memcpy(dst, src, static_cast<size_t>(nw * 4));
  }

  img->width = nw;
  img->height = nh;
  img->row_pitch = nw * 4;
  img->origin_x = c.left;
  img->origin_y = c.top;
  img->bgra.swap(out);
  return true;
}

}  // namespace sc
