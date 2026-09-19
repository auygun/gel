#ifndef BASE_LOG_H
#define BASE_LOG_H

#include <sstream>

// Adapted from Chromium's logging implementation.

// Macros for logging that are active in both debug and release builds. The way
// to log things is to stream things to LOG.
// LOG_IF can be used for conditional logging.
// CHECK(condition) terminates the process if the condition is false.
// NOTREACHED annotates unreachable codepaths and terminates the process if
// reached.
#define LOG(verbosity_level)      \
  LAZY_STREAM(                    \
      LOG_IS_ON(verbosity_level), \
      ::base::LogMessage(__FILE__, __LINE__, verbosity_level).stream())
#define LOG_IF(verbosity_level, condition)       \
  LAZY_STREAM(                                   \
      LOG_IS_ON(verbosity_level) && (condition), \
      ::base::LogMessage(__FILE__, __LINE__, verbosity_level).stream())
#define CHECK(condition)                                              \
  LAZY_STREAM(!(condition),                                           \
              ::base::LogAbort::Check(__FILE__, __LINE__, #condition) \
                  .GetLog()                                           \
                  .stream())

#define NOTREACHED() \
  ::base::LogAbort::NotReached(__FILE__, __LINE__).GetLog().stream()

// Macros for logging which are active only in debug builds.
#ifdef _DEBUG
#define DLOG(verbosity_level)     \
  LAZY_STREAM(                    \
      LOG_IS_ON(verbosity_level), \
      ::base::LogMessage(__FILE__, __LINE__, verbosity_level).stream())
#define DLOG_IF(verbosity_level, condition)      \
  LAZY_STREAM(                                   \
      LOG_IS_ON(verbosity_level) && (condition), \
      ::base::LogMessage(__FILE__, __LINE__, verbosity_level).stream())
#define DCHECK(condition)                                              \
  LAZY_STREAM(!(condition),                                            \
              ::base::LogAbort::DCheck(__FILE__, __LINE__, #condition) \
                  .GetLog()                                            \
                  .stream())
#else
// "debug mode" logging is compiled away to nothing for release builds.
#define DLOG(verbosity_level) EAT_STREAM_PARAMETERS
#define DLOG_IF(verbosity_level, condition) EAT_STREAM_PARAMETERS
#define DCHECK(condition) EAT_STREAM_PARAMETERS
#endif

// Helper macro which avoids evaluating the arguments to a stream if
// the condition doesn't hold.
#define LAZY_STREAM(condition, stream) \
  !(condition) ? (void)0 : ::base::LogMessage::Voidify() & (stream)

// Avoid any pointless instructions to be emitted by the compiler.
#define EAT_STREAM_PARAMETERS \
  LAZY_STREAM(false, *::base::LogMessage::swallow_stream)

#if defined(MAX_LOG_VERBOSITY_LEVEL)
#define LOG_IS_ON(verbosity_level) \
  ((verbosity_level) <= MAX_LOG_VERBOSITY_LEVEL)
#else
#define LOG_IS_ON(verbosity_level) \
  ((verbosity_level) <= ::base::GlobalMaxLogVerbosityLevel())
#endif

namespace base {

int GlobalMaxLogVerbosityLevel();

class LogMessage {
 public:
  class Voidify {
   public:
    Voidify() = default;

    // This has to be an operator with a precedence lower than << but
    // higher than ?:
    void operator&(std::ostream&) {}
  };

  LogMessage(const char* file, int line, int verbosity_level);
  ~LogMessage();

  std::ostream& stream() { return stream_; }

  static std::ostream* swallow_stream;

 protected:
  const char* file_;
  int line_;
  int verbosity_level_;
  std::ostringstream stream_;
};

class LogAbort {
 public:
  ~LogAbort();

  static LogAbort Check(const char* file, int line, const char* expr);
  static LogAbort DCheck(const char* file, int line, const char* expr);
  static LogAbort NotReached(const char* file, int line);

  LogMessage& GetLog() { return *log_; }

 private:
  LogMessage* log_;

  LogAbort(LogMessage* log);
};

}  // namespace base

#endif  // BASE_LOG_H
