#pragma once

#include "cli.h"
#include "common.h"
#include "monitor_enum.h"
#include "window_enum.h"

namespace sc {

struct CaptureContext {
  std::string method;
  CapOptions cap;
  CommonOptions common;
  std::optional<WindowInfo> window;
  std::optional<MonitorInfo> monitor;
  Rect capture_rect_screen;
};

bool CaptureWithGdi(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err);
bool CaptureWithDxgi(const CaptureContext &ctx, ImageBuffer *out,
                     int *out_adapter_index, int *out_output_index,
                     ErrorInfo *err);
bool CaptureWithWgc(const CaptureContext &ctx, ImageBuffer *out,
                    ErrorInfo *err);

} // namespace sc
