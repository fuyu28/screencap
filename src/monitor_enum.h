#pragma once

#include "common.h"

#include <optional>
#include <string>
#include <vector>

namespace sc {

struct MonitorInfo {
  HMONITOR hmon = nullptr;
  int index = -1;
  std::string name;
  Rect desktop;
  bool primary = false;
};

std::vector<MonitorInfo> EnumerateMonitors();
std::optional<MonitorInfo> FindMonitorByToken(const std::vector<MonitorInfo>& monitors, const std::string& token);

}  // namespace sc
