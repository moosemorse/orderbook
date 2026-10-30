#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "test_helpers.hpp"

// destroying a book must wake and join the prune thread straight away, not at the 16:00 cutoff
TEST(Lifetime, DestructorReturnsPromptly)
{
  auto done = std::make_shared<std::promise<void>>();
  auto finished = done->get_future();

  // detached so a hung join() fails this test instead of hanging the run
  std::thread{[done]
              {
                for (int i = 0; i < 20'000; ++i)
                {
                  OrderBook book;
                  std::this_thread::sleep_for(std::chrono::microseconds{i % 50});
                }
                done->set_value();
              }}
      .detach();

  ASSERT_EQ(finished.wait_for(std::chrono::seconds{60}), std::future_status::ready)
      << "an OrderBook destructor never returned";
}
