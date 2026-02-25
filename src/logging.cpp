#include "logging.h"

#include <windows.h>
#include <winternl.h>

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace sc {

namespace {

using RtlGetVersionPtr = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);

bool IsEnabled(LogLevel min_level, LogLevel lv) {
  return static_cast<int>(lv) >= static_cast<int>(min_level);
}

std::string BaseNameNoExt(const std::string &c) {
  if (c.empty())
    return "unknown";
  return c;
}

} // namespace

bool Logger::Init(const std::string &log_dir_utf8,
                  const std::string &command_name, LogLevel level) {
  min_level_ = level;
  std::error_code ec;
  auto dir = std::filesystem::path(WideFromUtf8(log_dir_utf8));
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    return false;
  }

  const auto filename = BuildTimestampForFilename() + "_" +
                        std::to_string(GetCurrentProcessId()) + "_" +
                        BaseNameNoExt(command_name) + ".log";
  file_path_ = dir / WideFromUtf8(filename);
  out_.open(file_path_, std::ios::out | std::ios::binary);
  return out_.good();
}

void Logger::Log(LogLevel lv, const std::string &msg) {
  if (!IsEnabled(min_level_, lv)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (!out_.good()) {
    return;
  }
  out_ << '[' << Iso8601NowLocal() << "] [" << LogLevelName(lv) << "] " << msg
       << '\n';
  out_.flush();
}

LogLevel ParseLogLevel(const std::string &s) {
  if (s == "trace")
    return LogLevel::kTrace;
  if (s == "debug")
    return LogLevel::kDebug;
  if (s == "warn")
    return LogLevel::kWarn;
  if (s == "error")
    return LogLevel::kError;
  return LogLevel::kInfo;
}

const char *LogLevelName(LogLevel lv) {
  switch (lv) {
  case LogLevel::kTrace:
    return "trace";
  case LogLevel::kDebug:
    return "debug";
  case LogLevel::kInfo:
    return "info";
  case LogLevel::kWarn:
    return "warn";
  case LogLevel::kError:
    return "error";
  }
  return "info";
}

std::string GetBuildStamp() { return std::string(__DATE__) + " " + __TIME__; }

std::string GetOsVersionString() {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) {
    return "unknown";
  }
  auto fn = reinterpret_cast<RtlGetVersionPtr>(
      GetProcAddress(ntdll, "RtlGetVersion"));
  if (!fn) {
    return "unknown";
  }
  RTL_OSVERSIONINFOW osv{};
  osv.dwOSVersionInfoSize = sizeof(osv);
  if (fn(&osv) != 0) {
    return "unknown";
  }
  std::ostringstream oss;
  oss << "Windows " << osv.dwMajorVersion << '.' << osv.dwMinorVersion
      << " build " << osv.dwBuildNumber;
  return oss.str();
}

} // namespace sc
