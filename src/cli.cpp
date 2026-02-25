#include "cli.h"

#include <cstdlib>
#include <sstream>

namespace sc {

namespace {

bool NeedValue(int i, int argc, const std::string &name, std::string *err) {
  if (i + 1 >= argc) {
    *err = "missing value for " + name;
    return false;
  }
  return true;
}

bool ParseInt(const std::string &s, int *out) {
  char *end = nullptr;
  long v = strtol(s.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  *out = static_cast<int>(v);
  return true;
}

bool ParseU64(const std::string &s, uint64_t *out) {
  char *end = nullptr;
  unsigned long long v = strtoull(s.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  *out = static_cast<uint64_t>(v);
  return true;
}

DpiMode ParseDpiMode(const std::string &s) {
  if (s == "auto")
    return DpiMode::kAuto;
  if (s == "system")
    return DpiMode::kSystem;
  return DpiMode::kPerMonitorV2;
}

CropMode ParseCropMode(const std::string &s) {
  if (s == "window")
    return CropMode::kWindow;
  if (s == "client")
    return CropMode::kClient;
  if (s == "dwm-frame")
    return CropMode::kDwmFrame;
  if (s == "manual")
    return CropMode::kManual;
  return CropMode::kNone;
}

} // namespace

ParseResult ParseArgs(int argc, char **argv) {
  ParseResult r;
  if (argc <= 1) {
    r.show_help = true;
    r.ok = true;
    return r;
  }

  ParsedArgs out;
  out.raw_args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i)
    out.raw_args.push_back(argv[i]);

  int i = 1;
  std::string cmd = argv[i++];
  if (cmd == "cap") {
    out.command = CommandType::kCap;
  } else if (cmd == "list") {
    if (i >= argc) {
      r.error = "list needs subcommand: windows|monitors";
      return r;
    }
    std::string sub = argv[i++];
    if (sub == "windows")
      out.command = CommandType::kListWindows;
    else if (sub == "monitors")
      out.command = CommandType::kListMonitors;
    else {
      r.error = "unknown list subcommand: " + sub;
      return r;
    }
  } else if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    r.show_help = true;
    r.ok = true;
    return r;
  } else {
    r.error = "unknown command: " + cmd;
    return r;
  }

  while (i < argc) {
    std::string a = argv[i];

    if (a == "--log-dir") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.common.log_dir = argv[++i];
    } else if (a == "--log-level") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.common.log_level = ParseLogLevel(argv[++i]);
    } else if (a == "--json") {
      out.common.json = true;
    } else if (a == "--timeout-ms") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      if (!ParseInt(argv[++i], &out.common.timeout_ms)) {
        r.error = "invalid --timeout-ms";
        return r;
      }
    } else if (a == "--retry") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      if (!ParseInt(argv[++i], &out.common.retry)) {
        r.error = "invalid --retry";
        return r;
      }
    } else if (a == "--overwrite") {
      out.common.overwrite = true;
    } else if (a == "--dpi-mode") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.common.dpi_mode = ParseDpiMode(argv[++i]);
    } else if (out.command == CommandType::kCap && a == "--method") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.method = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--target") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      std::string v = argv[++i];
      if (v == "window")
        out.cap.target = TargetType::kWindow;
      else if (v == "screen")
        out.cap.target = TargetType::kScreen;
      else {
        r.error = "invalid --target";
        return r;
      }
    } else if (out.command == CommandType::kCap && a == "--out") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.out_path = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--stdout") {
      r.error = "--stdout is not supported in this version";
      return r;
    } else if (out.command == CommandType::kCap && a == "--hwnd") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        r.error = "invalid --hwnd";
        return r;
      }
      out.cap.window_query.hwnd = v;
    } else if (out.command == CommandType::kCap && a == "--pid") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      int v = 0;
      if (!ParseInt(argv[++i], &v)) {
        r.error = "invalid --pid";
        return r;
      }
      out.cap.window_query.pid = v;
    } else if (out.command == CommandType::kCap && a == "--foreground") {
      out.cap.window_query.foreground = true;
    } else if (out.command == CommandType::kCap && a == "--title") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.window_query.title = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--class") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.window_query.class_name = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--monitor") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.screen_query.monitor = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--virtual-screen") {
      out.cap.screen_query.virtual_screen = true;
    } else if (out.command == CommandType::kCap && a == "--crop") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.crop_mode = ParseCropMode(argv[++i]);
    } else if (out.command == CommandType::kCap && a == "--crop-rect") {
      if (i + 4 >= argc) {
        r.error = "--crop-rect needs 4 values";
        return r;
      }
      CropRect c{};
      if (!ParseInt(argv[++i], &c.x) || !ParseInt(argv[++i], &c.y) ||
          !ParseInt(argv[++i], &c.w) || !ParseInt(argv[++i], &c.h)) {
        r.error = "invalid --crop-rect";
        return r;
      }
      out.cap.crop_rect = c;
    } else if (out.command == CommandType::kCap && a == "--pad") {
      if (i + 4 >= argc) {
        r.error = "--pad needs 4 values";
        return r;
      }
      if (!ParseInt(argv[++i], &out.cap.pad.l) ||
          !ParseInt(argv[++i], &out.cap.pad.t) ||
          !ParseInt(argv[++i], &out.cap.pad.r) ||
          !ParseInt(argv[++i], &out.cap.pad.b)) {
        r.error = "invalid --pad";
        return r;
      }
    } else if (out.command == CommandType::kCap && a == "--format") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      out.cap.format = argv[++i];
    } else if (out.command == CommandType::kCap && a == "--force-alpha") {
      if (!NeedValue(i, argc, a, &r.error))
        return r;
      int v = 0;
      if (!ParseInt(argv[++i], &v) || v != 255) {
        r.error = "--force-alpha only supports 255";
        return r;
      }
      out.cap.force_alpha_255 = true;
    } else {
      r.error = "unknown option: " + a;
      return r;
    }
    ++i;
  }

  if (out.command == CommandType::kCap) {
    if (out.cap.method.empty()) {
      r.error = "cap needs --method";
      return r;
    }
    if (out.cap.out_path.empty()) {
      r.error = "cap needs --out";
      return r;
    }
    if (out.cap.format != "png") {
      r.error = "only --format png is supported";
      return r;
    }
    if (out.cap.target == TargetType::kWindow) {
      const bool has_window_target =
          out.cap.window_query.hwnd.has_value() ||
          out.cap.window_query.pid.has_value() ||
          out.cap.window_query.foreground ||
          out.cap.window_query.title.has_value() ||
          out.cap.window_query.class_name.has_value();
      if (!has_window_target) {
        r.error = "window target needs one of "
                  "--hwnd/--pid/--foreground/--title/--class";
        return r;
      }
    } else {
      if (!out.cap.screen_query.monitor.has_value() &&
          !out.cap.screen_query.virtual_screen) {
        r.error = "screen target needs --monitor or --virtual-screen";
        return r;
      }
    }
    if (out.cap.crop_mode == CropMode::kManual &&
        !out.cap.crop_rect.has_value()) {
      r.error = "manual crop needs --crop-rect";
      return r;
    }
  }

  r.ok = true;
  r.args = std::move(out);
  return r;
}

const char *DpiModeName(DpiMode mode) {
  switch (mode) {
  case DpiMode::kAuto:
    return "auto";
  case DpiMode::kPerMonitorV2:
    return "per-monitor-v2";
  case DpiMode::kSystem:
    return "system";
  }
  return "unknown";
}

const char *TargetTypeName(TargetType t) {
  return t == TargetType::kWindow ? "window" : "screen";
}

const char *CropModeName(CropMode m) {
  switch (m) {
  case CropMode::kNone:
    return "none";
  case CropMode::kWindow:
    return "window";
  case CropMode::kClient:
    return "client";
  case CropMode::kDwmFrame:
    return "dwm-frame";
  case CropMode::kManual:
    return "manual";
  }
  return "none";
}

std::string BuildHelpText() {
  std::ostringstream oss;
  oss << "screencap - Windows screenshot comparison CLI\n\n"
      << "Commands:\n"
      << "  cap\n"
      << "  list windows\n"
      << "  list monitors\n\n"
      << "Examples:\n"
      << "  screencap list windows --json\n"
      << "  screencap cap --method dxgi-monitor --target screen --monitor "
         "primary --out a.png\n";
  return oss.str();
}

} // namespace sc
