#ifndef BASE_THREAD_POOL_H
#define BASE_THREAD_POOL_H

#include <atomic>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

#include "third_party/kaliber/base/closure.h"
#include "third_party/kaliber/base/task_runner.h"

namespace base {

// Feed the ThreadPool tasks (in the form of Closure objects) and they will be
// called on any thread from the pool.
class ThreadPool {
 public:
  ThreadPool();
  ~ThreadPool();

  static ThreadPool& Get() { return *singleton; }

  void Initialize(unsigned max_concurrency = 0);

  void Shutdown();

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
    task_runner_.PostTaskAndReplyWithResult(from, std::move(task),
                                            std::move(reply), front);
    semaphore_.release();
  }

  std::shared_ptr<TaskRunner> CreateSequencedTaskRunner();

  size_t GetPendingTaskCount() const;

  void CancelTasks();

 private:
  std::vector<std::thread> threads_;

  std::counting_semaphore<> semaphore_{0};
  std::atomic<bool> quit_{false};

  base::TaskRunner task_runner_;

  struct SequencedTaskRunner {
    std::shared_ptr<TaskRunner> task_runner;
    // Ensures only one worker thread processes this runner's tasks at a time,
    // preserving sequential execution order.
    std::mutex processing_lock;
  };

  std::vector<std::unique_ptr<SequencedTaskRunner>> sequenced_task_runners_;
  mutable std::mutex sequenced_task_runners_lock_;

  static ThreadPool* singleton;

  void WorkerMain();

  ThreadPool(ThreadPool const&) = delete;
  ThreadPool& operator=(ThreadPool const&) = delete;
};

}  // namespace base

#endif  // BASE_THREAD_POOL_H
