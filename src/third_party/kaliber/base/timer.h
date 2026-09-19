#ifndef BASE_TIMER_H
#define BASE_TIMER_H

#include <chrono>
#include <thread>

namespace base {

class ElapsedTimer {
 public:
  ElapsedTimer() { time_ = std::chrono::high_resolution_clock::now(); }

  // Return seconds passed since creating the object.
  double Elapsed() const {
    auto current_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = current_time - time_;
    return diff.count();
  }

 private:
  std::chrono::time_point<std::chrono::high_resolution_clock> time_;
};

// Not thread-safe. Must be used from a single thread.
class DeltaTimer {
 public:
  DeltaTimer() { time_ = std::chrono::high_resolution_clock::now(); }

  // Return seconds passed since the last call to this function.
  double Delta() {
    auto current_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = current_time - time_;
    time_ = current_time;
    return diff.count();
  }

 private:
  std::chrono::time_point<std::chrono::high_resolution_clock> time_;
};

inline void Sleep(double seconds) {
  std::this_thread::sleep_for(
      std::chrono::duration<double, std::milli>(seconds * 1000));
}

}  // namespace base

#endif  // BASE_TIMER_H
