#include "third_party/kaliber/base/log.h"

#if defined(OS_ANDROID)
#include <android/log.h>
#elif defined(OS_WIN)
#include <windows.h>
#include <format>
#else
#include <cstdio>
#endif
#include <cstdlib>
#include <string>

namespace base {

namespace {

int g_max_log_verbosity_level = 0;

}  // namespace

// This is never instantiated, it's just used for EAT_STREAM_PARAMETERS to have
// an object of the correct type on the LHS of the unused part of the ternary
// operator.
std::ostream* LogMessage::swallow_stream;

int GlobalMaxLogVerbosityLevel() {
  return g_max_log_verbosity_level;
}

LogMessage::LogMessage(const char* file, int line, int verbosity_level)
    : file_(file), line_(line), verbosity_level_(verbosity_level) {}

LogMessage::~LogMessage() {
  stream_ << '\n';
  std::string message(stream_.str());
  std::string filename(file_);
  size_t last_slash_pos = filename.find_last_of("\\/");
  if (last_slash_pos != std::string::npos)
    filename = filename.substr(last_slash_pos + 1);
#if defined(OS_ANDROID)
  __android_log_print(ANDROID_LOG_ERROR, "kaliber", "%d [%s:%d] %s",
                      verbosity_level_, filename.c_str(), line_,
                      message.c_str());
#elif defined(OS_WIN)
  OutputDebugStringA(
      std::format("{} [{}:{}] {}", verbosity_level_, filename, line_, message)
          .c_str());
#else
  printf("%d [%s:%d] %s", verbosity_level_, filename.c_str(), line_,
         message.c_str());
#endif
}

// static
LogAbort LogAbort::Check(const char* file, int line, const char* expr) {
  LogAbort instance(new LogMessage(file, line, 0));
  instance.GetLog().stream() << "CHECK(" << expr << ") ";
  return instance;
}

// static
LogAbort LogAbort::DCheck(const char* file, int line, const char* expr) {
  LogAbort instance(new LogMessage(file, line, 0));
  instance.GetLog().stream() << "DCHECK(" << expr << ") ";
  return instance;
}

// static
LogAbort LogAbort::NotReached(const char* file, int line) {
  LogAbort instance(new LogMessage(file, line, 0));
  instance.GetLog().stream() << "NOTREACHED() ";
  return instance;
}

LogAbort::LogAbort(LogMessage* log) : log_(log) {}

LogAbort::~LogAbort() {
  delete log_;
  std::abort();
}

}  // namespace base
