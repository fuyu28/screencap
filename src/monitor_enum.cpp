#include "monitor_enum.h"

#include <algorithm>

namespace sc {

std::vector<MonitorInfo> EnumerateMonitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto *vec = reinterpret_cast<std::vector<MonitorInfo> *>(lp);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(h, &mi))
          return TRUE;

        MonitorInfo info;
        info.hmon = h;
        info.index = static_cast<int>(vec->size());
        info.name = Utf8FromWide(mi.szDevice);
        info.desktop = ToRect(mi.rcMonitor);
        info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        vec->push_back(std::move(info));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}

std::optional<MonitorInfo>
FindMonitorByToken(const std::vector<MonitorInfo> &monitors,
                   const std::string &token) {
  if (token == "primary") {
    auto it = std::find_if(monitors.begin(), monitors.end(),
                           [](const MonitorInfo &m) { return m.primary; });
    if (it != monitors.end())
      return *it;
    return std::nullopt;
  }

  int idx = -1;
  try {
    idx = std::stoi(token);
  } catch (...) {
    return std::nullopt;
  }

  auto it = std::find_if(monitors.begin(), monitors.end(),
                         [&](const MonitorInfo &m) { return m.index == idx; });
  if (it != monitors.end())
    return *it;
  return std::nullopt;
}

} // namespace sc
