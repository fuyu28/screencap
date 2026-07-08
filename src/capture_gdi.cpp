#include "capture.h"

#include <cstring>
#include <functional>

namespace sc {

namespace {

// Shared DC/DIB scaffolding for all GDI capture variants: creates a 32bpp
// top-down DIB of w x h, runs `blit` to fill it, then copies the pixels into
// `out`. `blit` reports its own error and returns false on failure.
bool CaptureViaDib(HDC ref_dc, int w, int h, int origin_x, int origin_y,
                   const std::function<bool(HDC mem_dc, ErrorInfo *)> &blit,
                   ImageBuffer *out, ErrorInfo *err) {
  HDC mem_dc = CreateCompatibleDC(ref_dc);
  if (!mem_dc) {
    return FailWin32(err, "CreateCompatibleDC failed", "CaptureViaDib");
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
    FailWin32(err, "CreateDIBSection failed", "CaptureViaDib");
    if (bmp)
      DeleteObject(bmp);
    DeleteDC(mem_dc);
    return false;
  }

  HGDIOBJ old = SelectObject(mem_dc, bmp);
  const bool ok = blit(mem_dc, err);
  if (ok) {
    out->width = w;
    out->height = h;
    out->row_pitch = w * 4;
    out->origin_x = origin_x;
    out->origin_y = origin_y;
    out->bgra.assign(static_cast<uint8_t *>(bits),
                     static_cast<uint8_t *>(bits) +
                         static_cast<size_t>(out->row_pitch * h));
  }

  SelectObject(mem_dc, old);
  DeleteObject(bmp);
  DeleteDC(mem_dc);
  return ok;
}

bool CaptureFromDc(HDC src_dc, int src_x, int src_y, int w, int h, int origin_x,
                   int origin_y, ImageBuffer *out, ErrorInfo *err) {
  return CaptureViaDib(
      src_dc, w, h, origin_x, origin_y,
      [&](HDC mem_dc, ErrorInfo *e) {
        if (!BitBlt(mem_dc, 0, 0, w, h, src_dc, src_x, src_y,
                    SRCCOPY | CAPTUREBLT)) {
          return FailWin32(e, "BitBlt failed", "CaptureFromDc");
        }
        return true;
      },
      out, err);
}

} // namespace

bool CaptureWithGdi(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err) {
  const auto &method = ctx.cap.method;

  if (method == "gdi-printwindow") {
    if (!ctx.window.has_value()) {
      return Fail(err, "gdi-printwindow requires window target",
                  "CaptureWithGdi");
    }
    const auto &w = ctx.window.value();

    HDC win_dc = GetWindowDC(w.hwnd);
    if (!win_dc) {
      return FailWin32(err, "GetWindowDC failed", "CaptureWithGdi");
    }
    bool ok = CaptureViaDib(
        win_dc, Width(w.rect), Height(w.rect), w.rect.left, w.rect.top,
        [&](HDC mem_dc, ErrorInfo *e) {
          if (!PrintWindow(w.hwnd, mem_dc, PW_RENDERFULLCONTENT)) {
            return FailWin32(e, "PrintWindow failed", "CaptureWithGdi");
          }
          return true;
        },
        out, err);
    ReleaseDC(w.hwnd, win_dc);
    return ok;
  }

  if (method == "gdi-bitblt-client") {
    if (!ctx.window.has_value()) {
      return Fail(err, "gdi-bitblt-client requires window target",
                  "CaptureWithGdi");
    }
    const auto &w = ctx.window.value();
    HDC src = GetDC(w.hwnd);
    if (!src) {
      return FailWin32(err, "GetDC(hwnd) failed", "CaptureWithGdi");
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
      return Fail(err, "gdi-bitblt-windowdc requires window target",
                  "CaptureWithGdi");
    }
    const auto &w = ctx.window.value();
    HDC src = GetWindowDC(w.hwnd);
    if (!src) {
      return FailWin32(err, "GetWindowDC failed", "CaptureWithGdi");
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
      return FailWin32(err, "GetDC(NULL) failed", "CaptureWithGdi");
    }

    Rect r = ctx.capture_rect_screen;
    int ww = Width(r);
    int hh = Height(r);
    bool ok =
        CaptureFromDc(src, r.left, r.top, ww, hh, r.left, r.top, out, err);
    ReleaseDC(nullptr, src);
    return ok;
  }

  return Fail(err, "unknown gdi method", "CaptureWithGdi");
}

} // namespace sc
