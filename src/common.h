#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace sc {

constexpr const char *kVersion = "0.1.0";

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct CropRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct Pad {
  int l = 0;
  int t = 0;
  int r = 0;
  int b = 0;
};

struct ErrorInfo {
  std::string message;
  std::string where;
  std::optional<uint32_t> hresult;
  std::optional<uint32_t> win32_error;
};

struct ImageBuffer {
  int width = 0;
  int height = 0;
  int row_pitch = 0;
  int origin_x = 0;
  int origin_y = 0;
  std::vector<uint8_t> bgra;
};

struct ImageStats {
  double black_ratio = 0.0;
  double transparent_ratio = 0.0;
  double avg_luma = 0.0;
};

inline Rect ToRect(const RECT &r) {
  return Rect{r.left, r.top, r.right, r.bottom};
}

inline RECT ToRECT(const Rect &r) {
  RECT rr{r.left, r.top, r.right, r.bottom};
  return rr;
}

inline int Width(const Rect &r) { return r.right - r.left; }
inline int Height(const Rect &r) { return r.bottom - r.top; }

inline bool IsValidRect(const Rect &r) { return Width(r) > 0 && Height(r) > 0; }

inline std::string ToHex32(uint32_t v) {
  char buf[16] = {};
  snprintf(buf, sizeof(buf), "0x%08X", v);
  return std::string(buf);
}

inline std::string HwndToString(HWND hwnd) {
  char buf[32] = {};
  snprintf(buf, sizeof(buf), "%llu",
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)));
  return std::string(buf);
}

inline std::string Utf8FromWide(const std::wstring &ws) {
  if (ws.empty()) {
    return {};
  }
  int n =
      WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                          nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

inline std::wstring WideFromUtf8(const std::string &s) {
  if (s.empty()) {
    return {};
  }
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                              nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      out.data(), n);
  return out;
}

std::string JsonEscape(const std::string &s);
std::string Iso8601NowLocal();
std::string BuildTimestampForFilename();

} // namespace sc
