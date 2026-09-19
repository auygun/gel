#include "third_party/kaliber/base/task_runner.h"

#include <thread>

#include "third_party/kaliber/base/log.h"

namespace base {

namespace {

void PostTaskAndReplyRelay(Location from,
                           Closure task_cb,
                           Closure reply_cb,
                           std::shared_ptr<TaskRunner> destination,
                           bool front) {
  task_cb();

  if (reply_cb)
    destination->PostTask(from, std::move(reply_cb), front);
}

}  // namespace

// The task runner that belongs to the thread it's created in. Tasks to be run
// on a specific thread can be posted to this task runner.
// TaskRunner::GetThreadLocalTaskRunner()->RunTasks() is expected to be
// periodically called.
thread_local std::shared_ptr<TaskRunner> TaskRunner::thread_local_task_runner;

void TaskRunner::CreateThreadLocalTaskRunner() {
  DCHECK(!thread_local_task_runner);

  thread_local_task_runner = std::make_shared<TaskRunner>();
}

std::shared_ptr<TaskRunner> TaskRunner::GetThreadLocalTaskRunner() {
  return thread_local_task_runner;
}

void TaskRunner::PostTask(Location from, Closure task, bool front) {
  DCHECK(task) << LOCATION(from);

  task_count_.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock scoped_lock(lock_);
    if (front)
      queue_.emplace_front(from, std::move(task));
    else
      queue_.emplace_back(from, std::move(task));
  }
  if (on_task_posted_cb_)
    on_task_posted_cb_();
}

void TaskRunner::PostTaskAndReply(Location from,
                                  Closure task,
                                  Closure reply,
                                  bool front) {
  DCHECK(task) << LOCATION(from);
  DCHECK(reply) << LOCATION(from);
  DCHECK(thread_local_task_runner) << LOCATION(from);

  auto relay = std::bind(PostTaskAndReplyRelay, from, std::move(task),
                         std::move(reply), thread_local_task_runner, front);
  PostTask(from, std::move(relay), front);
}

void TaskRunner::CancelTasks() {
  cancelled_.store(true, std::memory_order_relaxed);
  std::scoped_lock scoped_lock(lock_);
  task_count_.fetch_sub(queue_.size(), std::memory_order_release);
  queue_.clear();
}

void TaskRunner::WaitForCompletion() {
  while (task_count_.load(std::memory_order_acquire) > 0)
    std::this_thread::yield();
}

template <>
void TaskRunner::RunTasks<Consumer::Multi>() {
  for (;;) {
    {
      Task task;
      {
        std::scoped_lock scoped_lock(lock_);
        if (queue_.empty())
          return;
        task.swap(queue_.front());
        queue_.pop_front();
      }

      auto [from, task_cb] = task;

#if 0
      DLOG(0) << __func__ << " from: " << LOCATION(from);
#endif

      task_cb();
    }
    task_count_.fetch_sub(1, std::memory_order_release);
  }
}

template <>
void TaskRunner::RunTasks<Consumer::Single>() {
  std::deque<Task> queue;
  {
    std::scoped_lock scoped_lock(lock_);
    if (queue_.empty())
      return;
    queue.swap(queue_);
  }

  while (!queue.empty()) {
    if (cancelled_.load(std::memory_order_relaxed)) {
      cancelled_.store(false, std::memory_order_relaxed);
      task_count_.fetch_sub(queue.size(), std::memory_order_release);
      break;
    }
    {
      auto [from, task_cb] = queue.front();
      queue.pop_front();

#if 0
      DLOG(0) << __func__ << " from: " << LOCATION(from);
#endif

      task_cb();
    }
    task_count_.fetch_sub(1, std::memory_order_release);
  }
}

template <>
void TaskRunner::RunTasks<Consumer::Sequenced>() {
  for (;;) {
    std::deque<Task> queue;
    {
      std::scoped_lock scoped_lock(lock_);
      if (queue_.empty())
        return;
      queue.swap(queue_);
    }

    while (!queue.empty()) {
      if (cancelled_.load(std::memory_order_relaxed)) {
        cancelled_.store(false, std::memory_order_relaxed);
        task_count_.fetch_sub(queue.size(), std::memory_order_release);
        break;
      }
      {
        auto [from, task_cb] = queue.front();
        queue.pop_front();

#if 0
        DLOG(0) << __func__ << " from: " << LOCATION(from);
#endif

        task_cb();
      }
      task_count_.fetch_sub(1, std::memory_order_release);
    }
  }
}

}  // namespace base
