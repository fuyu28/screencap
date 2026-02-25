#include "capture.h"
#include "cli.h"
#include "crop.h"
#include "encode_wic_png.h"
#include "image_stats.h"
#include "logging.h"
#include "monitor_enum.h"
#include "window_enum.h"

#include <shellapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace sc {

namespace {

struct RunResult {
  bool ok = false;
  int exit_code = 1;
  ErrorInfo err;
  std::string json;
};

struct BootstrapOptions {
  std::string log_dir = "./logs";
  LogLevel log_level = LogLevel::kInfo;
  std::string command = "unknown";
  bool json = false;
};

BootstrapOptions PreParseBootstrap(int argc, char **argv) {
  BootstrapOptions b;
  if (argc >= 2) {
    b.command = argv[1];
    if (b.command == "list" && argc >= 3) {
      b.command = std::string("list_") + argv[2];
    }
  }
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--log-dir" && i + 1 < argc) {
      b.log_dir = argv[++i];
    } else if (a == "--log-level" && i + 1 < argc) {
      b.log_level = ParseLogLevel(argv[++i]);
    } else if (a == "--json") {
      b.json = true;
    }
  }
  return b;
}

std::string RectJson(const Rect &r) {
  std::ostringstream oss;
  oss << "{\"left\":" << r.left << ",\"top\":" << r.top
      << ",\"right\":" << r.right << ",\"bottom\":" << r.bottom << '}';
  return oss.str();
}

std::string CropRectJson(const CropRect &r) {
  std::ostringstream oss;
  oss << "{\"x\":" << r.x << ",\"y\":" << r.y << ",\"w\":" << r.w
      << ",\"h\":" << r.h << '}';
  return oss.str();
}

bool ApplyDpiMode(DpiMode requested, std::string *applied, Logger *logger) {
  auto set_system = [&]() {
    BOOL ok = SetProcessDPIAware();
    (void)ok;
    *applied = "system";
    return true;
  };

  if (requested == DpiMode::kSystem) {
    return set_system();
  }

  HRESULT hr =
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
          ? S_OK
          : HRESULT_FROM_WIN32(GetLastError());
  if (SUCCEEDED(hr)) {
    *applied = "per-monitor-v2";
    return true;
  }
  if (logger) {
    logger->Log(
        LogLevel::kWarn,
        "SetProcessDpiAwarenessContext(PMv2) failed, fallback to system");
  }
  return set_system();
}

std::string ErrorJson(const ErrorInfo &err) {
  std::ostringstream oss;
  oss << "{\"message\":\"" << JsonEscape(err.message) << "\",\"where\":\""
      << JsonEscape(err.where) << "\"";
  if (err.hresult.has_value()) {
    oss << ",\"hresult\":\"" << ToHex32(err.hresult.value()) << "\"";
  }
  if (err.win32_error.has_value()) {
    oss << ",\"win32_error\":" << err.win32_error.value();
  }
  oss << '}';
  return oss.str();
}

std::string WindowsJsonArray(const std::vector<WindowInfo> &ws) {
  std::ostringstream oss;
  oss << '[';
  for (size_t i = 0; i < ws.size(); ++i) {
    const auto &w = ws[i];
    if (i)
      oss << ',';
    oss << "{\"hwnd\":" << reinterpret_cast<uintptr_t>(w.hwnd)
        << ",\"pid\":" << w.pid << ",\"title\":\"" << JsonEscape(w.title)
        << "\",\"class\":\"" << JsonEscape(w.class_name)
        << "\",\"rect\":" << RectJson(w.rect)
        << ",\"visible\":" << (w.visible ? "true" : "false")
        << ",\"iconic\":" << (w.iconic ? "true" : "false")
        << ",\"cloaked\":" << (w.cloaked ? "true" : "false") << '}';
  }
  oss << ']';
  return oss.str();
}

std::string MonitorsJsonArray(const std::vector<MonitorInfo> &ms) {
  std::ostringstream oss;
  oss << '[';
  for (size_t i = 0; i < ms.size(); ++i) {
    const auto &m = ms[i];
    if (i)
      oss << ',';
    oss << "{\"index\":" << m.index << ",\"name\":\"" << JsonEscape(m.name)
        << "\",\"desktop\":" << RectJson(m.desktop)
        << ",\"primary\":" << (m.primary ? "true" : "false") << '}';
  }
  oss << ']';
  return oss.str();
}

RunResult RunListWindows(const ParsedArgs &parsed) {
  RunResult rr;
  auto ws = EnumerateWindows();
  rr.ok = true;
  rr.exit_code = 0;

  std::ostringstream oss;
  oss << "{\"ok\":true,\"command\":\"list windows\",\"timestamp\":\""
      << Iso8601NowLocal() << "\",\"windows\":" << WindowsJsonArray(ws) << '}';
  rr.json = oss.str();

  if (!parsed.common.json) {
    std::cout << "windows=" << ws.size() << "\n";
    for (const auto &w : ws) {
      std::cout << "hwnd=" << reinterpret_cast<uintptr_t>(w.hwnd)
                << " pid=" << w.pid << " title=" << w.title
                << " class=" << w.class_name << " rect=" << w.rect.left << ','
                << w.rect.top << ',' << w.rect.right << ',' << w.rect.bottom
                << " visible=" << w.visible << " iconic=" << w.iconic
                << " cloaked=" << w.cloaked << "\n";
    }
  }
  return rr;
}

RunResult RunListMonitors(const ParsedArgs &parsed) {
  RunResult rr;
  auto ms = EnumerateMonitors();
  rr.ok = true;
  rr.exit_code = 0;

  std::ostringstream oss;
  oss << "{\"ok\":true,\"command\":\"list monitors\",\"timestamp\":\""
      << Iso8601NowLocal() << "\",\"monitors\":" << MonitorsJsonArray(ms)
      << '}';
  rr.json = oss.str();

  if (!parsed.common.json) {
    std::cout << "monitors=" << ms.size() << "\n";
    for (const auto &m : ms) {
      std::cout << "index=" << m.index << " name=" << m.name
                << " rect=" << m.desktop.left << ',' << m.desktop.top << ','
                << m.desktop.right << ',' << m.desktop.bottom
                << " primary=" << m.primary << "\n";
    }
  }
  return rr;
}

Rect VirtualScreenRect() {
  int l = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int t = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return Rect{l, t, l + w, t + h};
}

RunResult RunCap(const ParsedArgs &parsed, Logger *logger,
                 const std::string &dpi_applied) {
  RunResult rr;
  const auto start = std::chrono::steady_clock::now();

  auto windows = EnumerateWindows();
  auto monitors = EnumerateMonitors();

  CaptureContext ctx;
  ctx.method = parsed.cap.method;
  ctx.cap = parsed.cap;
  ctx.common = parsed.common;

  std::string resolve_reason;
  if (parsed.cap.target == TargetType::kWindow ||
      parsed.cap.method.find("window") != std::string::npos ||
      parsed.cap.method.find("printwindow") != std::string::npos ||
      parsed.cap.method.find("client") != std::string::npos ||
      parsed.cap.method.find("windowdc") != std::string::npos) {
    WindowInfo w;
    ErrorInfo err;
    if (!ResolveWindowTarget(parsed.cap.window_query, windows, &w,
                             &resolve_reason, logger, &err)) {
      rr.err = err;
      rr.exit_code = 1;
      return rr;
    }
    ctx.window = w;
    if (logger) {
      logger->Log(LogLevel::kInfo,
                  "resolved window hwnd=" + HwndToString(w.hwnd) +
                      " pid=" + std::to_string(w.pid) + " title=" + w.title +
                      " class=" + w.class_name +
                      " rect=" + std::to_string(w.rect.left) + "," +
                      std::to_string(w.rect.top) + "," +
                      std::to_string(w.rect.right) + "," +
                      std::to_string(w.rect.bottom) +
                      " visible=" + (w.visible ? "1" : "0") +
                      " iconic=" + (w.iconic ? "1" : "0") + " cloaked=" +
                      (w.cloaked ? "1" : "0") + " reason=" + resolve_reason);
    }
  }

  if (parsed.cap.target == TargetType::kScreen ||
      parsed.cap.method.find("monitor") != std::string::npos ||
      parsed.cap.method == "dxgi-window") {
    if (parsed.cap.screen_query.virtual_screen) {
      ctx.capture_rect_screen = VirtualScreenRect();
    } else if (parsed.cap.screen_query.monitor.has_value()) {
      auto mon =
          FindMonitorByToken(monitors, parsed.cap.screen_query.monitor.value());
      if (!mon.has_value()) {
        rr.err = ErrorInfo{"monitor not found", "RunCap", std::nullopt,
                           std::nullopt};
        rr.exit_code = 1;
        return rr;
      }
      ctx.monitor = mon.value();
      ctx.capture_rect_screen = mon->desktop;
    } else if (ctx.window.has_value()) {
      HMONITOR h =
          MonitorFromWindow(ctx.window->hwnd, MONITOR_DEFAULTTONEAREST);
      for (const auto &m : monitors) {
        if (m.hmon == h) {
          ctx.monitor = m;
          ctx.capture_rect_screen = m.desktop;
          break;
        }
      }
    }
    if (logger && ctx.monitor.has_value()) {
      const auto &m = ctx.monitor.value();
      logger->Log(LogLevel::kInfo,
                  "resolved monitor index=" + std::to_string(m.index) +
                      " rect=" + std::to_string(m.desktop.left) + "," +
                      std::to_string(m.desktop.top) + "," +
                      std::to_string(m.desktop.right) + "," +
                      std::to_string(m.desktop.bottom) +
                      " primary=" + (m.primary ? "1" : "0"));
    }
  }

  if (!IsValidRect(ctx.capture_rect_screen) && ctx.window.has_value()) {
    ctx.capture_rect_screen = ctx.window->rect;
  }

  ImageBuffer img;
  ErrorInfo cap_err;
  int adapter_index = -1;
  int output_index = -1;
  bool cap_ok = false;

  for (int attempt = 0; attempt <= parsed.common.retry; ++attempt) {
    if (parsed.cap.method.rfind("gdi-", 0) == 0) {
      cap_ok = CaptureWithGdi(ctx, &img, &cap_err);
    } else if (parsed.cap.method.rfind("dxgi-", 0) == 0) {
      cap_ok =
          CaptureWithDxgi(ctx, &img, &adapter_index, &output_index, &cap_err);
    } else if (parsed.cap.method.rfind("wgc-", 0) == 0) {
      cap_ok = CaptureWithWgc(ctx, &img, &cap_err);
    } else {
      cap_err =
          ErrorInfo{"unknown method", "RunCap", std::nullopt, std::nullopt};
      cap_ok = false;
    }

    if (cap_ok)
      break;
    if (logger) {
      logger->Log(LogLevel::kWarn,
                  "capture attempt failed attempt=" + std::to_string(attempt) +
                      " where=" + cap_err.where);
    }
  }

  if (!cap_ok) {
    rr.err = cap_err;
    rr.exit_code = 1;
    return rr;
  }

  if (logger) {
    if (parsed.cap.method.rfind("dxgi-", 0) == 0) {
      logger->Log(LogLevel::kInfo,
                  "DXGI adapter_index=" + std::to_string(adapter_index) +
                      " output_index=" + std::to_string(output_index) +
                      " frame_size=" + std::to_string(img.width) + "x" +
                      std::to_string(img.height) +
                      " row_pitch=" + std::to_string(img.row_pitch));
    }
  }

  Rect img_rect{img.origin_x, img.origin_y, img.origin_x + img.width,
                img.origin_y + img.height};
  CropMode crop_mode = parsed.cap.crop_mode;
  if (crop_mode == CropMode::kNone && parsed.cap.method == "dxgi-window") {
    crop_mode = CropMode::kWindow;
  }
  ErrorInfo crop_err;
  Rect crop_rect = ResolveCropRectScreen(
      crop_mode, parsed.cap.crop_rect,
      ctx.window.has_value() ? &ctx.window.value() : nullptr, img_rect,
      parsed.cap.pad, &crop_err);
  if (!IsValidRect(crop_rect) ||
      !CropImageInPlace(crop_rect, &img, &crop_err)) {
    rr.err = crop_err;
    rr.exit_code = 1;
    return rr;
  }

  ImageStats stats = ComputeImageStats(img);
  if (logger) {
    logger->Log(
        LogLevel::kInfo,
        "image_stats black_ratio=" + std::to_string(stats.black_ratio) +
            " transparent_ratio=" + std::to_string(stats.transparent_ratio));
  }

  ErrorInfo save_err;
  if (!SavePngWic(img, WideFromUtf8(parsed.cap.out_path),
                  parsed.common.overwrite, &save_err)) {
    rr.err = save_err;
    rr.exit_code = 1;
    return rr;
  }

  const auto end = std::chrono::steady_clock::now();
  const auto duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());

  CropRect crop_out{img.origin_x, img.origin_y, img.width, img.height};

  std::ostringstream js;
  js << "{\"ok\":true,\"command\":\"cap\",\"method\":\""
     << JsonEscape(parsed.cap.method) << "\",\"target\":\""
     << TargetTypeName(parsed.cap.target) << "\",\"out_path\":\""
     << JsonEscape(parsed.cap.out_path)
     << "\",\"format\":\"png\",\"timestamp\":\"" << Iso8601NowLocal()
     << "\",\"duration_ms\":" << duration_ms << ",\"dpi_mode\":\""
     << JsonEscape(dpi_applied) << "\"";

  if (ctx.window.has_value()) {
    const auto &w = ctx.window.value();
    js << ",\"window\":{\"hwnd\":" << reinterpret_cast<uintptr_t>(w.hwnd)
       << ",\"pid\":" << w.pid << ",\"title\":\"" << JsonEscape(w.title)
       << "\",\"class\":\"" << JsonEscape(w.class_name)
       << "\",\"rect\":" << RectJson(w.rect)
       << ",\"client_rect_screen\":" << RectJson(w.client_rect_screen)
       << ",\"visible\":" << (w.visible ? "true" : "false")
       << ",\"iconic\":" << (w.iconic ? "true" : "false")
       << ",\"cloaked\":" << (w.cloaked ? "true" : "false") << '}';
  }

  if (ctx.monitor.has_value()) {
    const auto &m = ctx.monitor.value();
    js << ",\"monitor\":{\"index\":" << m.index
       << ",\"desktop\":" << RectJson(m.desktop)
       << ",\"primary\":" << (m.primary ? "true" : "false") << '}';
  }

  js << ",\"crop\":{\"mode\":\"" << CropModeName(crop_mode)
     << "\",\"rect\":" << CropRectJson(crop_out)
     << ",\"pad\":{\"l\":" << parsed.cap.pad.l << ",\"t\":" << parsed.cap.pad.t
     << ",\"r\":" << parsed.cap.pad.r << ",\"b\":" << parsed.cap.pad.b << "}}";

  js << ",\"image_stats\":{\"black_ratio\":" << stats.black_ratio
     << ",\"transparent_ratio\":" << stats.transparent_ratio
     << ",\"avg_luma\":" << stats.avg_luma << "},\"error\":null}";

  rr.ok = true;
  rr.exit_code = 0;
  rr.json = js.str();
  if (logger) {
    logger->Log(LogLevel::kInfo,
                "result=success out_path=" + parsed.cap.out_path +
                    " duration_ms=" + std::to_string(duration_ms));
  }
  return rr;
}

void LogStartup(Logger *logger, const ParsedArgs *parsed,
                const std::string &dpi_mode) {
  if (!logger)
    return;
  logger->Log(LogLevel::kInfo, "version=" + std::string(kVersion));
  logger->Log(LogLevel::kInfo, "build=" + GetBuildStamp());
  logger->Log(LogLevel::kInfo, "os=" + GetOsVersionString());
  logger->Log(LogLevel::kInfo, "dpi_mode=" + dpi_mode);
  if (parsed) {
    std::ostringstream oss;
    oss << "argv=";
    for (size_t i = 0; i < parsed->raw_args.size(); ++i) {
      if (i)
        oss << ' ';
      oss << parsed->raw_args[i];
    }
    logger->Log(LogLevel::kInfo, oss.str());
  }
}

std::string BuildFailureJson(const std::string &command,
                             const std::string &method,
                             const std::string &target,
                             const std::string &out_path,
                             const std::string &dpi_mode, int duration_ms,
                             const ErrorInfo &err) {
  std::ostringstream oss;
  oss << "{\"ok\":false,\"command\":\"" << JsonEscape(command)
      << "\",\"method\":\"" << JsonEscape(method) << "\",\"target\":\""
      << JsonEscape(target) << "\",\"out_path\":\"" << JsonEscape(out_path)
      << "\",\"format\":\"png\",\"timestamp\":\"" << Iso8601NowLocal()
      << "\",\"duration_ms\":" << duration_ms << ",\"dpi_mode\":\""
      << JsonEscape(dpi_mode)
      << "\",\"window\":null,\"monitor\":null,\"crop\":null"
      << ",\"image_stats\":null,\"error\":" << ErrorJson(err) << '}';
  return oss.str();
}

bool LoadClipboardDibToImage(ImageBuffer *out, ErrorInfo *err) {
  const UINT format =
      IsClipboardFormatAvailable(CF_DIBV5)
          ? CF_DIBV5
          : (IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0);
  if (format == 0) {
    *err = ErrorInfo{"DIB format not found in clipboard", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }

  HANDLE h = GetClipboardData(format);
  if (!h) {
    *err = ErrorInfo{"GetClipboardData(DIB) failed", "LoadClipboardDibToImage",
                     std::nullopt, static_cast<uint32_t>(GetLastError())};
    return false;
  }

  const SIZE_T total = GlobalSize(h);
  if (total < sizeof(BITMAPINFOHEADER)) {
    *err = ErrorInfo{"clipboard DIB payload too small", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }

  const auto *ptr = static_cast<const uint8_t *>(GlobalLock(h));
  if (!ptr) {
    *err = ErrorInfo{"GlobalLock failed", "LoadClipboardDibToImage",
                     std::nullopt, static_cast<uint32_t>(GetLastError())};
    return false;
  }

  const auto *bih = reinterpret_cast<const BITMAPINFOHEADER *>(ptr);
  if (bih->biSize < sizeof(BITMAPINFOHEADER)) {
    GlobalUnlock(h);
    *err = ErrorInfo{"invalid BITMAPINFOHEADER size", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }
  if (bih->biBitCount != 32) {
    GlobalUnlock(h);
    *err = ErrorInfo{"clipboard DIB bitcount not 32", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }
  if (bih->biCompression != BI_RGB && bih->biCompression != BI_BITFIELDS) {
    GlobalUnlock(h);
    *err = ErrorInfo{"unsupported DIB compression", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }

  const int width = bih->biWidth;
  const int height = (bih->biHeight < 0) ? -bih->biHeight : bih->biHeight;
  if (width <= 0 || height <= 0) {
    GlobalUnlock(h);
    *err = ErrorInfo{"invalid DIB dimensions", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }

  const bool top_down = bih->biHeight < 0;
  SIZE_T header_bytes = bih->biSize;
  if (bih->biCompression == BI_BITFIELDS) {
    header_bytes += 12;
  }
  const SIZE_T src_pitch = static_cast<SIZE_T>(width) * 4;
  const SIZE_T pixel_bytes = src_pitch * static_cast<SIZE_T>(height);
  if (header_bytes + pixel_bytes > total) {
    GlobalUnlock(h);
    *err = ErrorInfo{"DIB payload truncated", "LoadClipboardDibToImage",
                     std::nullopt, std::nullopt};
    return false;
  }

  const uint8_t *src_base = ptr + header_bytes;
  out->width = width;
  out->height = height;
  out->row_pitch = width * 4;
  out->origin_x = 0;
  out->origin_y = 0;
  out->bgra.resize(static_cast<size_t>(out->row_pitch * out->height));

  for (int y = 0; y < height; ++y) {
    const int src_y = top_down ? y : (height - 1 - y);
    const uint8_t *src = src_base + static_cast<SIZE_T>(src_y) * src_pitch;
    uint8_t *dst =
        out->bgra.data() + static_cast<size_t>(y * out->row_pitch);
    memcpy(dst, src, static_cast<size_t>(out->row_pitch));
  }

  GlobalUnlock(h);
  return true;
}

bool WaitClipboardImageAfterSequence(DWORD seq, int timeout_ms,
                                     ImageBuffer *out, ErrorInfo *err) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (GetClipboardSequenceNumber() <= seq) {
      Sleep(40);
      continue;
    }

    for (int i = 0; i < 20; ++i) {
      if (OpenClipboard(nullptr)) {
        ErrorInfo e;
        const bool ok = LoadClipboardDibToImage(out, &e);
        CloseClipboard();
        if (ok) {
          return true;
        }
        if (e.where != "LoadClipboardDibToImage" ||
            e.message != "DIB format not found in clipboard") {
          *err = e;
          return false;
        }
      }
      Sleep(20);
    }
  }

  *err = ErrorInfo{"clipboard image timeout", "WaitClipboardImageAfterSequence",
                   std::nullopt, std::nullopt};
  return false;
}

RunResult RunCapByScreenClip(const ParsedArgs &parsed, Logger *logger,
                             const std::string &dpi_applied) {
  RunResult rr;
  const auto start = std::chrono::steady_clock::now();

  const DWORD seq = GetClipboardSequenceNumber();
  HINSTANCE si = ShellExecuteW(nullptr, L"open", L"ms-screenclip:", nullptr,
                               nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<intptr_t>(si) <= 32) {
    rr.err = ErrorInfo{"ShellExecute ms-screenclip failed", "RunCapByScreenClip",
                       std::nullopt, std::nullopt};
    rr.exit_code = 1;
    return rr;
  }
  if (logger) {
    logger->Log(LogLevel::kInfo, "launched ms-screenclip");
  }

  ImageBuffer img;
  ErrorInfo clip_err;
  const int clip_timeout_ms = std::max(parsed.common.timeout_ms, 15000);
  if (!WaitClipboardImageAfterSequence(seq, clip_timeout_ms, &img, &clip_err)) {
    rr.err = clip_err;
    rr.exit_code = 1;
    return rr;
  }

  if (parsed.cap.force_alpha_255) {
    for (size_t i = 3; i < img.bgra.size(); i += 4) {
      img.bgra[i] = 0xFF;
    }
  }

  ImageStats stats = ComputeImageStats(img);
  if (logger) {
    logger->Log(LogLevel::kInfo,
                "screenclip image_stats black_ratio=" +
                    std::to_string(stats.black_ratio) +
                    " transparent_ratio=" +
                    std::to_string(stats.transparent_ratio));
  }

  ErrorInfo save_err;
  if (!SavePngWic(img, WideFromUtf8(parsed.cap.out_path),
                  parsed.common.overwrite, &save_err)) {
    rr.err = save_err;
    rr.exit_code = 1;
    return rr;
  }

  const auto end = std::chrono::steady_clock::now();
  const auto duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());

  CropRect crop_out{img.origin_x, img.origin_y, img.width, img.height};
  std::ostringstream js;
  js << "{\"ok\":true,\"command\":\"cap\",\"method\":\""
     << JsonEscape(parsed.cap.method)
     << "\",\"target\":\"" << TargetTypeName(parsed.cap.target)
     << "\",\"out_path\":\"" << JsonEscape(parsed.cap.out_path)
     << "\",\"format\":\"png\",\"timestamp\":\"" << Iso8601NowLocal()
     << "\",\"duration_ms\":" << duration_ms << ",\"dpi_mode\":\""
     << JsonEscape(dpi_applied)
     << "\",\"window\":null,\"monitor\":null,\"crop\":{\"mode\":\"screenclip\""
     << ",\"rect\":" << CropRectJson(crop_out)
     << ",\"pad\":{\"l\":0,\"t\":0,\"r\":0,\"b\":0}}";
  js << ",\"image_stats\":{\"black_ratio\":" << stats.black_ratio
     << ",\"transparent_ratio\":" << stats.transparent_ratio
     << ",\"avg_luma\":" << stats.avg_luma << "},\"error\":null}";

  rr.ok = true;
  rr.exit_code = 0;
  rr.json = js.str();
  return rr;
}

bool WaitForHotkey(const ParsedArgs &parsed, Logger *logger, ErrorInfo *err) {
  if (!parsed.cap.hotkey_enabled) {
    return true;
  }

  constexpr int kHotkeyId = 0x5343;
  if (!RegisterHotKey(nullptr, kHotkeyId, parsed.cap.hotkey_modifiers,
                      parsed.cap.hotkey_vk)) {
    *err = ErrorInfo{"RegisterHotKey failed", "WaitForHotkey", std::nullopt,
                     static_cast<uint32_t>(GetLastError())};
    return false;
  }

  if (logger) {
    logger->Log(LogLevel::kInfo, "hotkey waiting spec=" + parsed.cap.hotkey_spec);
  }
  if (!parsed.common.json) {
    std::cout << "waiting hotkey: " << parsed.cap.hotkey_spec << "\n";
  }

  MSG msg{};
  bool ok = false;
  while (true) {
    const BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
    if (gm == -1) {
      *err = ErrorInfo{"GetMessage failed", "WaitForHotkey", std::nullopt,
                       static_cast<uint32_t>(GetLastError())};
      break;
    }
    if (gm == 0) {
      *err = ErrorInfo{"message loop ended before hotkey", "WaitForHotkey",
                       std::nullopt, std::nullopt};
      break;
    }
    if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
      ok = true;
      break;
    }
  }

  UnregisterHotKey(nullptr, kHotkeyId);
  if (ok && !parsed.common.json) {
    std::cout << "hotkey pressed\n";
  }
  return ok;
}

} // namespace

} // namespace sc

int main(int argc, char **argv) {
  using namespace sc;

  auto boot = PreParseBootstrap(argc, argv);
  Logger logger;
  logger.Init(boot.log_dir, boot.command, boot.log_level);

  ParseResult parsed = ParseArgs(argc, argv);

  std::string dpi_applied = "unknown";
  DpiMode requested_dpi =
      parsed.ok ? parsed.args.common.dpi_mode : DpiMode::kPerMonitorV2;
  ApplyDpiMode(requested_dpi, &dpi_applied, &logger);

  LogStartup(&logger, parsed.ok ? &parsed.args : nullptr, dpi_applied);

  if (!parsed.ok) {
    logger.Log(LogLevel::kError, "parse error: " + parsed.error);
    if (boot.json) {
      ErrorInfo err{parsed.error, "ParseArgs", std::nullopt, std::nullopt};
      std::cout << BuildFailureJson("unknown", "", "", "", dpi_applied, 0, err)
                << '\n';
    } else {
      std::cerr << "Error: " << parsed.error << "\n\n" << BuildHelpText();
    }
    return 2;
  }

  if (parsed.show_help || parsed.args.command == CommandType::kHelp) {
    std::cout << BuildHelpText();
    return 0;
  }

  ParsedArgs run_args = parsed.args;
  RunResult rr;
  if (parsed.args.command == CommandType::kListWindows) {
    rr = RunListWindows(parsed.args);
  } else if (parsed.args.command == CommandType::kListMonitors) {
    rr = RunListMonitors(parsed.args);
  } else {
    if (run_args.cap.hotkey_enabled) {
      ErrorInfo wait_err;
      if (!WaitForHotkey(run_args, &logger, &wait_err)) {
        rr.err = wait_err;
        rr.exit_code = 1;
      } else {
        rr = RunCapByScreenClip(run_args, &logger, dpi_applied);
      }
    } else {
      rr = RunCap(run_args, &logger, dpi_applied);
    }
  }

  if (rr.ok) {
    logger.Log(LogLevel::kInfo, "result=success");
    if (parsed.args.common.json) {
      std::cout << rr.json << '\n';
    } else if (parsed.args.command == CommandType::kCap) {
      std::cout << "ok: " << parsed.args.cap.out_path << '\n';
    }
    return rr.exit_code;
  }

  logger.Log(LogLevel::kError, "result=failure where=" + rr.err.where +
                                   " message=" + rr.err.message);
  if (parsed.args.common.json || parsed.args.command == CommandType::kCap) {
    std::cout << BuildFailureJson(
                     parsed.args.command == CommandType::kCap ? "cap" : "list",
                     parsed.args.cap.method,
                     TargetTypeName(parsed.args.cap.target),
                     parsed.args.cap.out_path, dpi_applied, 0, rr.err)
              << '\n';
  } else {
    std::cerr << "Error: " << rr.err.message << " (" << rr.err.where << ")\n";
  }
  return rr.exit_code;
}
