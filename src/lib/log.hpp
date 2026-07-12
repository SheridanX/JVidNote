#pragma once

#include <cstdio>
#include <source_location>

// ---- 默认日志等级（可通过 -DJVIDNOTE_LOG_LEVEL=N 覆盖） ----
#ifndef JVIDNOTE_LOG_LEVEL
#define JVIDNOTE_LOG_LEVEL 0
#endif

namespace JVNote
{

enum LogLevel
{
  LOG_LEVEL_PRINT = 0,
  LOG_LEVEL_INFO  = 1,
  LOG_LEVEL_WARN  = 2,
  LOG_LEVEL_ERROR = 3,
  LOG_LEVEL_OFF   = 4
};

// 编译期日志等级
inline constexpr LogLevel g_log_level = static_cast<LogLevel>(JVIDNOTE_LOG_LEVEL);

// 终端格式
inline constexpr const char* kLogBold   = "\033[1m";
inline constexpr const char* kLogRed    = "\033[31m";
inline constexpr const char* kLogGreen  = "\033[32m";
inline constexpr const char* kLogYellow = "\033[33m";
inline constexpr const char* kLogReset  = "\033[0m";

namespace detail
{

template <typename... Args>
void vfprint(FILE* stream, const char* fmt, Args&&... args)
{
  if constexpr (sizeof...(args) > 0) {
    std::fprintf(stream, fmt, std::forward<Args>(args)...);
  } else {
    std::fputs(fmt, stream);
  }
}

} // namespace detail

// ============================================================
// Log — 日志对象，构造时自动捕获调用点 source_location
// ============================================================
struct Log
{
  std::source_location loc;

  // 显式构造函数确保 source_location::current() 在调用点求值
  Log(std::source_location loc = std::source_location::current())
      : loc(loc)
  {
  }

  // ---- print  — 正常打印，加粗输出 ----
  template <typename... Args>
  void print(const char* fmt, Args&&... args) const
  {
    if constexpr (g_log_level <= LOG_LEVEL_PRINT) {
      std::fputs(kLogBold, stdout);
      detail::vfprint(stdout, fmt, std::forward<Args>(args)...);
      std::fputs(kLogReset, stdout);
      std::fputc('\n', stdout);
    }
  }

  // ---- info  — 绿色，带文件、函数、行号 ----
  template <typename... Args>
  void info(const char* fmt, Args&&... args) const
  {
    if constexpr (g_log_level <= LOG_LEVEL_INFO) {
      std::fprintf(stdout, "%s[INFO] %s:%d %s: ",
                   kLogGreen, loc.file_name(), loc.line(), loc.function_name());
      detail::vfprint(stdout, fmt, std::forward<Args>(args)...);
      std::fputs(kLogReset, stdout);
      std::fputc('\n', stdout);
    }
  }

  // ---- warn  — 黄色，带文件、函数、行号 ----
  template <typename... Args>
  void warn(const char* fmt, Args&&... args) const
  {
    if constexpr (g_log_level <= LOG_LEVEL_WARN) {
      std::fprintf(stdout, "%s[WARN] %s:%d %s: ",
                   kLogYellow, loc.file_name(), loc.line(), loc.function_name());
      detail::vfprint(stdout, fmt, std::forward<Args>(args)...);
      std::fputs(kLogReset, stdout);
      std::fputc('\n', stdout);
    }
  }

  // ---- error — 红色，带文件、函数、行号，输出到 stderr ----
  template <typename... Args>
  void error(const char* fmt, Args&&... args) const
  {
    if constexpr (g_log_level <= LOG_LEVEL_ERROR) {
      std::fprintf(stderr, "%s[ERROR] %s:%d %s: ",
                   kLogRed, loc.file_name(), loc.line(), loc.function_name());
      detail::vfprint(stderr, fmt, std::forward<Args>(args)...);
      std::fputs(kLogReset, stderr);
      std::fputc('\n', stderr);
    }
  }
};

} // namespace JVNote
