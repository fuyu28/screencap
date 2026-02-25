#include "window_enum.h"

#include <dwmapi.h>

#include <algorithm>
#include <cctype>
#include <tuple>

namespace sc {

namespace {

std::string GetWindowTextUtf8(HWND hwnd) {
  int len = GetWindowTextLengthW(hwnd);
  std::wstring ws(static_cast<size_t>(len), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd, ws.data(), len + 1);
  }
  return Utf8FromWide(ws);
}

std::string GetClassNameUtf8(HWND hwnd) {
  wchar_t buf[256] = {};
  int len = GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
  return Utf8FromWide(std::wstring(buf, buf + len));
}

Rect GetClientRectScreen(HWND hwnd) {
  RECT cr{};
  if (!GetClientRect(hwnd, &cr)) {
    return {};
  }
  POINT p1{cr.left, cr.top};
  POINT p2{cr.right, cr.bottom};
  ClientToScreen(hwnd, &p1);
  ClientToScreen(hwnd, &p2);
  return Rect{p1.x, p1.y, p2.x, p2.y};
}

Rect GetDwmFrameRect(HWND hwnd, const Rect& fallback) {
  RECT r{};
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) {
    return ToRect(r);
  }
  return fallback;
}

int Area(const Rect& r) { return std::max(0, Width(r)) * std::max(0, Height(r)); }

bool ContainsI(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
  };
  return lower(hay).find(lower(needle)) != std::string::npos;
}

}  // namespace

std::vector<WindowInfo> EnumerateWindows() {
  std::vector<WindowInfo> out;
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* vec = reinterpret_cast<std::vector<WindowInfo>*>(lparam);

        WindowInfo w;
        w.hwnd = hwnd;
        GetWindowThreadProcessId(hwnd, &w.pid);
        w.title = GetWindowTextUtf8(hwnd);
        w.class_name = GetClassNameUtf8(hwnd);
        RECT r{};
        GetWindowRect(hwnd, &r);
        w.rect = ToRect(r);
        w.client_rect_screen = GetClientRectScreen(hwnd);
        w.dwm_frame_rect = GetDwmFrameRect(hwnd, w.rect);
        w.visible = IsWindowVisible(hwnd) != FALSE;
        w.iconic = IsIconic(hwnd) != FALSE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
          w.cloaked = cloaked != 0;
        }
        vec->push_back(std::move(w));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}

bool ResolveWindowTarget(const TargetWindowQuery& query, const std::vector<WindowInfo>& all, WindowInfo* out,
                         std::string* reason, Logger* logger, ErrorInfo* err) {
  if (query.hwnd.has_value()) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(query.hwnd.value()));
    for (const auto& w : all) {
      if (w.hwnd == hwnd) {
        *out = w;
        *reason = "matched by --hwnd";
        return true;
      }
    }
    err->message = "window not found by --hwnd";
    err->where = "ResolveWindowTarget";
    return false;
  }

  if (query.foreground) {
    HWND fg = GetForegroundWindow();
    for (const auto& w : all) {
      if (w.hwnd == fg) {
        *out = w;
        *reason = "matched by --foreground";
        return true;
      }
    }
    err->message = "foreground window not found";
    err->where = "ResolveWindowTarget";
    return false;
  }

  std::vector<const WindowInfo*> candidates;
  for (const auto& w : all) {
    if (query.pid.has_value() && static_cast<int>(w.pid) != query.pid.value()) continue;
    if (query.title.has_value() && !ContainsI(w.title, query.title.value())) continue;
    if (query.class_name.has_value() && w.class_name != query.class_name.value()) continue;
    candidates.push_back(&w);
  }

  if (candidates.empty()) {
    err->message = "no matching windows";
    err->where = "ResolveWindowTarget";
    return false;
  }

  auto rank = [](const WindowInfo* w) {
    int s1 = (w->visible && !w->iconic && !w->cloaked) ? 1 : 0;
    int s2 = (GetAncestor(w->hwnd, GA_ROOT) == w->hwnd) ? 1 : 0;
    int s3 = Area(w->rect);
    return std::tuple<int, int, int>(s1, s2, s3);
  };

  std::sort(candidates.begin(), candidates.end(), [&](const WindowInfo* a, const WindowInfo* b) { return rank(a) > rank(b); });

  *out = *candidates.front();
  *reason = "matched by filters, selected by priority(visible&&!iconic&&!cloaked > root > max area)";
  if (logger) {
    logger->Log(LogLevel::kInfo, "ResolveWindowTarget candidates=" + std::to_string(candidates.size()));
  }
  return true;
}

}  // namespace sc
