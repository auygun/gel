#include "third_party/kaliber/base/thread_pool.h"

#include "third_party/kaliber/base/log.h"

namespace base {

ThreadPool* ThreadPool::singleton = nullptr;

ThreadPool::ThreadPool() {
  DCHECK(!singleton);
  singleton = this;
}

ThreadPool::~ThreadPool() {
  Shutdown();
  singleton = nullptr;
}

void ThreadPool::Initialize(unsigned max_concurrency) {
  if (max_concurrency == 0) {
    max_concurrency = std::thread::hardware_concurrency();
    if (max_concurrency == 0)
      max_concurrency = 1;
  }

  while (max_concurrency--)
    threads_.emplace_back(&ThreadPool::WorkerMain, this);
}

void ThreadPool::Shutdown() {
  if (threads_.empty())
    return;

  quit_.store(true, std::memory_order_relaxed);
  semaphore_.release(threads_.size());

  for (auto& thread : threads_)
    thread.join();
  threads_.clear();
}

void ThreadPool::PostTask(Location from, Closure task, bool front) {
  task_runner_.PostTask(from, std::move(task), front);
  semaphore_.release();
}

void ThreadPool::PostTaskAndReply(Location from,
                                  Closure task,
                                  Closure reply,
                                  bool front) {
  task_runner_.PostTaskAndReply(from, std::move(task), std::move(reply), front);
  semaphore_.release();
}

std::shared_ptr<TaskRunner> ThreadPool::CreateSequencedTaskRunner() {
  auto sequenced = std::make_unique<SequencedTaskRunner>();
  sequenced->task_runner = std::make_shared<TaskRunner>();
  sequenced->task_runner->SetOnTaskPostedCallback(
      [this]() { semaphore_.release(); });
  auto result = sequenced->task_runner;
  {
    std::scoped_lock lock(sequenced_task_runners_lock_);
    sequenced_task_runners_.push_back(std::move(sequenced));
  }
  return result;
}

size_t ThreadPool::GetPendingTaskCount() const {
  size_t count = task_runner_.GetPendingTaskCount();
  std::scoped_lock lock(sequenced_task_runners_lock_);
  for (auto& sr : sequenced_task_runners_)
    count += sr->task_runner->GetPendingTaskCount();
  return count;
}

void ThreadPool::CancelTasks() {
  task_runner_.CancelTasks();
  std::scoped_lock lock(sequenced_task_runners_lock_);
  for (auto& sr : sequenced_task_runners_)
    sr->task_runner->CancelTasks();
}

void ThreadPool::WorkerMain() {
  for (;;) {
    semaphore_.acquire();
    if (quit_.load(std::memory_order_relaxed))
      return;

    // Snapshot sequenced task runners under the lock, then process them
    // without holding it. The shared_ptr copies keep runners alive even if
    // another worker's erase_if would otherwise remove them.
    struct RunnerRef {
      std::shared_ptr<TaskRunner> task_runner;
      std::mutex* processing_lock;
    };
    std::vector<RunnerRef> runners;
    {
      std::scoped_lock lock(sequenced_task_runners_lock_);

      // Remove entries whose external shared_ptr has been dropped
      // (use_count == 1 means only the pool's copy remains).
      std::erase_if(sequenced_task_runners_,
                    [](auto& sr) { return sr->task_runner.use_count() == 1; });

      runners.reserve(sequenced_task_runners_.size());
      for (auto& sr : sequenced_task_runners_)
        runners.push_back({sr->task_runner, &sr->processing_lock});
    }

    // Each runner uses a try_lock so that only one worker thread drains a
    // given runner at a time, ensuring its tasks execute sequentially.
    for (auto& r : runners) {
      std::unique_lock processing_lock(*r.processing_lock, std::try_to_lock);
      if (processing_lock)
        r.task_runner->RunTasks<Consumer::Sequenced>();
    }

    // Process normal tasks. Multiple workers can run these concurrently.
    task_runner_.RunTasks<Consumer::Multi>();
  }
}

}  // namespace base
