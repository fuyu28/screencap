#include "common.h"

#include <iomanip>
#include <sstream>

namespace sc {

std::string JsonEscape(const std::string& s) {
  std::ostringstream oss;
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (c < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        } else {
          oss << c;
        }
        break;
    }
  }
  return oss.str();
}

std::string Iso8601NowLocal() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm local_tm{};
  localtime_s(&local_tm, &t);

  TIME_ZONE_INFORMATION tzi{};
  DWORD tzid = GetTimeZoneInformation(&tzi);
  LONG bias = tzi.Bias;
  if (tzid == TIME_ZONE_ID_STANDARD) {
    bias += tzi.StandardBias;
  } else if (tzid == TIME_ZONE_ID_DAYLIGHT) {
    bias += tzi.DaylightBias;
  }
  int offset_minutes = -bias;
  char sign = offset_minutes >= 0 ? '+' : '-';
  int abs_minutes = offset_minutes >= 0 ? offset_minutes : -offset_minutes;
  int off_h = abs_minutes / 60;
  int off_m = abs_minutes % 60;

  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count() << sign
      << std::setw(2) << std::setfill('0') << off_h << ':' << std::setw(2) << std::setfill('0') << off_m;
  return oss.str();
}

std::string BuildTimestampForFilename() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm local_tm{};
  localtime_s(&local_tm, &t);

  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y%m%d_%H%M%S") << '_' << std::setw(3) << std::setfill('0') << ms.count();
  return oss.str();
}

}  // namespace sc
