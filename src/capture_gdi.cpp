#include "capture.h"

#include <cstring>

namespace sc {

namespace {

bool CaptureFromDc(HDC src_dc, int src_x, int src_y, int w, int h, int origin_x,
                   int origin_y, ImageBuffer *out, ErrorInfo *err) {
  HDC mem_dc = CreateCompatibleDC(src_dc);
  if (!mem_dc) {
    *err = ErrorInfo{"CreateCompatibleDC failed", "CaptureFromDc", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HBITMAP bmp =
      CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp || !bits) {
    DeleteDC(mem_dc);
    *err = ErrorInfo{"CreateDIBSection failed", "CaptureFromDc", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
  }

  HGDIOBJ old = SelectObject(mem_dc, bmp);
  BOOL ok =
      BitBlt(mem_dc, 0, 0, w, h, src_dc, src_x, src_y, SRCCOPY | CAPTUREBLT);
  if (!ok) {
    SelectObject(mem_dc, old);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    *err = ErrorInfo{"BitBlt failed", "CaptureFromDc", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
  }

  out->width = w;
  out->height = h;
  out->row_pitch = w * 4;
  out->origin_x = origin_x;
  out->origin_y = origin_y;
  out->bgra.assign(static_cast<uint8_t *>(bits),
                   static_cast<uint8_t *>(bits) +
                       static_cast<size_t>(out->row_pitch * h));

  SelectObject(mem_dc, old);
  DeleteObject(bmp);
  DeleteDC(mem_dc);
  return true;
}

} // namespace

bool CaptureWithGdi(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err) {
  const auto &method = ctx.method;

  if (method == "gdi-printwindow") {
    if (!ctx.window.has_value()) {
      *err = ErrorInfo{"gdi-printwindow requires window target",
                       "CaptureWithGdi", std::nullopt, std::nullopt};
      return false;
    }
    const auto &w = ctx.window.value();
    int width = Width(w.rect);
    int height = Height(w.rect);

    HDC win_dc = GetWindowDC(w.hwnd);
    if (!win_dc) {
      *err = ErrorInfo{"GetWindowDC failed", "CaptureWithGdi", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      return false;
    }
    HDC mem_dc = CreateCompatibleDC(win_dc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bmp =
        CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
      if (bmp)
        DeleteObject(bmp);
      DeleteDC(mem_dc);
      ReleaseDC(w.hwnd, win_dc);
      *err = ErrorInfo{"CreateDIBSection failed", "CaptureWithGdi",
                       std::nullopt, static_cast<uint32_t>(GetLastError())};
      return false;
    }

    HGDIOBJ old = SelectObject(mem_dc, bmp);
    BOOL ok = PrintWindow(w.hwnd, mem_dc, PW_RENDERFULLCONTENT);
    if (!ok) {
      *err = ErrorInfo{"PrintWindow failed", "CaptureWithGdi", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      SelectObject(mem_dc, old);
      DeleteObject(bmp);
      DeleteDC(mem_dc);
      ReleaseDC(w.hwnd, win_dc);
      return false;
    }

    out->width = width;
    out->height = height;
    out->row_pitch = width * 4;
    out->origin_x = w.rect.left;
    out->origin_y = w.rect.top;
    out->bgra.assign(static_cast<uint8_t *>(bits),
                     static_cast<uint8_t *>(bits) +
                         static_cast<size_t>(out->row_pitch * height));

    SelectObject(mem_dc, old);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    ReleaseDC(w.hwnd, win_dc);
    return true;
  }

  if (method == "gdi-bitblt-client") {
    if (!ctx.window.has_value()) {
      *err = ErrorInfo{"gdi-bitblt-client requires window target",
                       "CaptureWithGdi", std::nullopt, std::nullopt};
      return false;
    }
    const auto &w = ctx.window.value();
    HDC src = GetDC(w.hwnd);
    if (!src) {
      *err = ErrorInfo{"GetDC(hwnd) failed", "CaptureWithGdi", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      return false;
    }
    int ww = Width(w.client_rect_screen);
    int hh = Height(w.client_rect_screen);
    bool ok = CaptureFromDc(src, 0, 0, ww, hh, w.client_rect_screen.left,
                            w.client_rect_screen.top, out, err);
    ReleaseDC(w.hwnd, src);
    return ok;
  }

  if (method == "gdi-bitblt-windowdc") {
    if (!ctx.window.has_value()) {
      *err = ErrorInfo{"gdi-bitblt-windowdc requires window target",
                       "CaptureWithGdi", std::nullopt, std::nullopt};
      return false;
    }
    const auto &w = ctx.window.value();
    HDC src = GetWindowDC(w.hwnd);
    if (!src) {
      *err = ErrorInfo{"GetWindowDC failed", "CaptureWithGdi", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      return false;
    }
    int ww = Width(w.rect);
    int hh = Height(w.rect);
    bool ok =
        CaptureFromDc(src, 0, 0, ww, hh, w.rect.left, w.rect.top, out, err);
    ReleaseDC(w.hwnd, src);
    return ok;
  }

  if (method == "gdi-bitblt-screen") {
    HDC src = GetDC(nullptr);
    if (!src) {
      *err = ErrorInfo{"GetDC(NULL) failed", "CaptureWithGdi", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      return false;
    }

    Rect r = ctx.capture_rect_screen;
    int ww = Width(r);
    int hh = Height(r);
    bool ok =
        CaptureFromDc(src, r.left, r.top, ww, hh, r.left, r.top, out, err);
    ReleaseDC(nullptr, src);
    return ok;
  }

  *err = ErrorInfo{"unknown gdi method", "CaptureWithGdi", std::nullopt,
                   std::nullopt};
  return false;
}

} // namespace sc
