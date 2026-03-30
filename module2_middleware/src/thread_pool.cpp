#include "thread_pool.hpp"

namespace middleware {

ThreadPool::ThreadPool(std::size_t num_threads) {
  workers_.reserve(num_threads);
  for (std::size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this] {
      for (;;) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(mtx_);
          cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty()) return;
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::shutdown() {
  {
    std::unique_lock<std::mutex> lock(mtx_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& w : workers_) {
    if (w.joinable()) w.join();
  }
}

std::size_t ThreadPool::pending() const {
  std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(mtx_));
  return tasks_.size();
}

} // namespace middleware
