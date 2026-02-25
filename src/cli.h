#pragma once

#include "common.h"
#include "logging.h"

#include <optional>
#include <string>
#include <vector>

namespace sc {

enum class CommandType { kHelp, kCap, kListWindows, kListMonitors };
enum class DpiMode { kAuto, kPerMonitorV2, kSystem };
enum class TargetType { kWindow, kScreen };
enum class CropMode { kNone, kWindow, kClient, kDwmFrame, kManual };

struct CommonOptions {
  std::string log_dir = "./logs";
  LogLevel log_level = LogLevel::kInfo;
  bool json = false;
  int timeout_ms = 700;
  int retry = 0;
  bool overwrite = false;
  DpiMode dpi_mode = DpiMode::kPerMonitorV2;
};

struct TargetWindowQuery {
  std::optional<uint64_t> hwnd;
  std::optional<int> pid;
  bool foreground = false;
  std::optional<std::string> title;
  std::optional<std::string> class_name;
};

struct TargetScreenQuery {
  std::optional<std::string> monitor; // index or primary
  bool virtual_screen = false;
};

struct CapOptions {
  std::string method;
  TargetType target = TargetType::kWindow;
  std::string out_path;
  std::string format = "png";
  TargetWindowQuery window_query;
  TargetScreenQuery screen_query;
  CropMode crop_mode = CropMode::kNone;
  std::optional<CropRect> crop_rect;
  Pad pad{};
  bool force_alpha_255 = false;
};

struct ParsedArgs {
  CommandType command = CommandType::kHelp;
  CommonOptions common;
  CapOptions cap;
  std::vector<std::string> raw_args;
};

struct ParseResult {
  bool ok = false;
  bool show_help = false;
  ParsedArgs args;
  std::string error;
};

ParseResult ParseArgs(int argc, char **argv);
const char *DpiModeName(DpiMode mode);
const char *TargetTypeName(TargetType t);
const char *CropModeName(CropMode m);
std::string BuildHelpText();

} // namespace sc
