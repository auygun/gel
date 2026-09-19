#ifndef BASE_CLOSURE_H
#define BASE_CLOSURE_H

#include <functional>
#include <memory>
#include <string>
#include <tuple>

#ifdef _DEBUG

#define HERE std::make_tuple(__func__, __FILE__, __LINE__)

// Helper for logging location info, e.g. LOG(0) << LOCATION(from)
#define LOCATION(from)                                                 \
  std::get<0>(from) << "() [" << [](std::string path) -> std::string { \
    size_t last_slash_pos = path.find_last_of("\\/");                  \
    if (last_slash_pos != std::string::npos)                           \
      path = path.substr(last_slash_pos + 1);                          \
    return path;                                                       \
  }(std::get<1>(from)) << ":"                                          \
                       << std::get<2>(from) << "]"

#else

#define HERE nullptr
#define LOCATION(from) ""

#endif

namespace base {

using Closure = std::function<void()>;

#ifdef _DEBUG

// Provides location info (function name, file name and line number) where of a
// Closure was constructed.
using Location = std::tuple<std::string, std::string, int>;

#else

using Location = std::nullptr_t;

#endif

// Bind a method to an object with a std::weak_ptr.
template <typename Class, typename ReturnType, typename... Args>
std::function<ReturnType(Args...)> BindWeak(ReturnType (Class::*func)(Args...),
                                            std::weak_ptr<Class> weak_ptr) {
  return [func, weak_ptr](Args... args) {
    if (auto ptr = weak_ptr.lock())
      std::invoke(func, ptr, args...);
  };
}

}  // namespace base

#endif  // BASE_CLOSURE_H
