#ifndef BASE_TASK_RUNNER_H
#define BASE_TASK_RUNNER_H

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <tuple>

#include "third_party/kaliber/base/closure.h"

namespace base {

namespace internal {

// Adapted from Chromium project.
// Adapts a function that produces a result via a return value to
// one that returns via an output parameter.
template <typename ReturnType>
void ReturnAsParamAdapter(std::function<ReturnType()> func,
                          std::shared_ptr<ReturnType> result) {
  *result = func();
}

// Adapts a ReturnType* result to a callback that expects a ReturnType.
template <typename ReturnType>
void ReplyAdapter(std::function<void(ReturnType)> callback,
                  std::shared_ptr<ReturnType> result) {
  callback(std::move(*result));
}

}  // namespace internal

enum class Consumer {
  // Tasks are consumed by multiple threads.
  Multi,
  // Tasks are consumed by a single thread. Prevents indefinite spinning if
  // tasks keep posting more tasks.
  Single,
  // Tasks are consumed by a single thread. Loops until the queue is fully
  // drained, including tasks posted during execution.
  Sequenced
};

// Runs queued tasks (in the form of Closure objects). All methods are
// thread-safe and can be called on any thread.
// Tasks run in FIFO order when consumed by a single thread. When consumed
// concurrently by multiple threads, it doesn't guarantee whether tasks overlap,
// or whether they run on a particular thread.
class TaskRunner {
 public:
  TaskRunner() = default;
  ~TaskRunner() = default;

  static void CreateThreadLocalTaskRunner();
  static std::shared_ptr<TaskRunner> GetThreadLocalTaskRunner();

  void PostTask(Location from, Closure task, bool front = false);

  void PostTaskAndReply(Location from,
                        Closure task,
                        Closure reply,
                        bool front = false);

  template <typename ReturnType>
  void PostTaskAndReplyWithResult(Location from,
                                  std::function<ReturnType()> task,
                                  std::function<void(ReturnType)> reply,
                                  bool front = false) {
    auto result = std::make_shared<ReturnType>();
    return PostTaskAndReply(
        from,
        std::bind(internal::ReturnAsParamAdapter<ReturnType>, std::move(task),
                  result),
        std::bind(internal::ReplyAdapter<ReturnType>, std::move(reply), result),
        front);
  }

  // Posts a task that deletes the given object.
  template <class T>
  void Delete(Location from, std::unique_ptr<T> object) {
    // std::function target must be copy-constructible
    std::shared_ptr<T> owned = std::move(object);
    PostTask(HERE, [owned]() {});
  }

  // The callback is invoked outside the lock from whichever thread calls
  // PostTask. It must be thread-safe.
  void SetOnTaskPostedCallback(Closure cb) {
    on_task_posted_cb_ = std::move(cb);
  }

  size_t GetPendingTaskCount() const {
    return task_count_.load(std::memory_order_acquire);
  }

  void CancelTasks();

  void WaitForCompletion();

  template <Consumer T>
  void RunTasks();

 private:
  using Task = std::tuple<Location, Closure>;

  std::deque<Task> queue_;
  mutable std::mutex lock_;
  std::atomic<size_t> task_count_{0};
  std::atomic<bool> cancelled_{false};
  Closure on_task_posted_cb_;

  static thread_local std::shared_ptr<TaskRunner> thread_local_task_runner;

  TaskRunner(TaskRunner const&) = delete;
  TaskRunner& operator=(TaskRunner const&) = delete;
};

}  // namespace base

#endif  // BASE_TASK_RUNNER_H
