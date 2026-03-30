#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>

#include "thread_pool.hpp"

using namespace middleware;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Basic functionality
// ---------------------------------------------------------------------------

TEST(ThreadPoolTest, ExecutesAllTasks) {
  ThreadPool pool(4);
  std::atomic<int> counter{0};

  std::vector<std::future<void>> futures;
  for (int i = 0; i < 100; ++i) {
    futures.push_back(pool.enqueue([&counter]{ counter.fetch_add(1); }));
  }
  for (auto& f : futures) f.get();

  EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolTest, ReturnValuesAreCorrect) {
  ThreadPool pool(2);

  auto f1 = pool.enqueue([]{ return 42; });
  auto f2 = pool.enqueue([]{ return std::string("hello"); });

  EXPECT_EQ(f1.get(), 42);
  EXPECT_EQ(f2.get(), "hello");
}

TEST(ThreadPoolTest, ConcurrentSumIsCorrect) {
  constexpr int N = 1000;
  ThreadPool pool(8);

  std::vector<std::future<int>> futures;
  futures.reserve(N);
  for (int i = 0; i < N; ++i) {
    futures.push_back(pool.enqueue([i]{ return i; }));
  }

  int total = 0;
  for (auto& f : futures) total += f.get();

  EXPECT_EQ(total, N * (N-1) / 2);
}

TEST(ThreadPoolTest, ExceptionPropagatesThroughFuture) {
  ThreadPool pool(2);

  auto f = pool.enqueue([]() -> int {
    throw std::runtime_error("intentional");
  });

  EXPECT_THROW(f.get(), std::runtime_error);
}

TEST(ThreadPoolTest, ShutdownPreventsNewEnqueues) {
  ThreadPool pool(2);
  pool.shutdown();

  EXPECT_THROW(pool.enqueue([]{}), std::runtime_error);
}

TEST(ThreadPoolTest, SizeReportsCorrectly) {
  ThreadPool pool(5);
  EXPECT_EQ(pool.size(), 5u);
}

// ---------------------------------------------------------------------------
// Stress test: many tasks, high concurrency
// ---------------------------------------------------------------------------

TEST(ThreadPoolTest, StressTest) {
  constexpr int TASKS   = 5000;
  constexpr int THREADS = 16;

  ThreadPool pool(THREADS);
  std::atomic<int> done{0};

  std::vector<std::future<void>> futures;
  futures.reserve(TASKS);

  for (int i = 0; i < TASKS; ++i) {
    futures.push_back(pool.enqueue([&done]{
      // Small busy-work to stress scheduling
      volatile int x = 0;
      for (int j = 0; j < 100; ++j) x += j;
      (void)x;
      done.fetch_add(1);
    }));
  }

  for (auto& f : futures) f.get();
  EXPECT_EQ(done.load(), TASKS);
}

// ---------------------------------------------------------------------------
// Pending count is advisory but should not crash
// ---------------------------------------------------------------------------

TEST(ThreadPoolTest, PendingDoesNotCrash) {
  ThreadPool pool(1);
  // Enqueue a slow task to build up a queue
  pool.enqueue([]{ std::this_thread::sleep_for(20ms); });
  for (int i = 0; i < 10; ++i) pool.enqueue([]{});

  std::size_t p = pool.pending();
  EXPECT_GE(p, 0u);  // always true; just verifying no crash
}
