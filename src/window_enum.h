#pragma once

#include "cli.h"
#include "common.h"
#include "logging.h"

#include <string>
#include <vector>

namespace sc {

struct WindowInfo {
  HWND hwnd = nullptr;
  DWORD pid = 0;
  std::string title;
  std::string class_name;
  Rect rect;
  Rect client_rect_screen;
  Rect dwm_frame_rect;
  bool visible = false;
  bool iconic = false;
  bool cloaked = false;
};

std::vector<WindowInfo> EnumerateWindows();
bool ResolveWindowTarget(const TargetWindowQuery &query,
                         const std::vector<WindowInfo> &all, WindowInfo *out,
                         std::string *reason, Logger *logger, ErrorInfo *err);

} // namespace sc
