#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace middleware {

// A fixed-size thread pool that accepts any callable via enqueue().
// Gracefully drains the queue on destruction.
class ThreadPool {
public:
  explicit ThreadPool(std::size_t num_threads);
  ~ThreadPool();

  // Disable copy/move – the pool manages OS threads.
  ThreadPool(const ThreadPool&)            = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Submit a callable; returns a std::future for the result.
  // Throws std::runtime_error if the pool has already been stopped.
  template <typename F, typename... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>>;

  // Number of worker threads
  std::size_t size() const { return workers_.size(); }

  // Approximate number of pending tasks (not thread-safe, advisory only)
  std::size_t pending() const;

  // Finish all queued tasks and stop workers (called automatically by dtor)
  void shutdown();

private:
  std::vector<std::thread>          workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex                        mtx_;
  std::condition_variable           cv_;
  bool                              stop_{false};
};

// ---------------------------------------------------------------------------
// Template implementation
// ---------------------------------------------------------------------------
template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
  using ReturnType = std::invoke_result_t<F, Args...>;

  auto task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<ReturnType> future = task->get_future();

  {
    std::unique_lock<std::mutex> lock(mtx_);
    if (stop_) {
      throw std::runtime_error("ThreadPool: enqueue() called after shutdown");
    }
    tasks_.emplace([task]() { (*task)(); });
  }
  cv_.notify_one();
  return future;
}

} // namespace middleware
