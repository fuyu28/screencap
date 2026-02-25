#pragma once

#include "common.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace sc {

enum class LogLevel {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
};

class Logger {
 public:
  bool Init(const std::string& log_dir_utf8, const std::string& command_name, LogLevel level);
  void Log(LogLevel level, const std::string& msg);
  const std::filesystem::path& file_path() const { return file_path_; }

 private:
  std::mutex mu_;
  std::ofstream out_;
  std::filesystem::path file_path_;
  LogLevel min_level_ = LogLevel::kInfo;
};

LogLevel ParseLogLevel(const std::string& s);
const char* LogLevelName(LogLevel lv);

std::string GetBuildStamp();
std::string GetOsVersionString();

}  // namespace sc
