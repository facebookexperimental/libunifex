#include <unifex/async_manual_reset_event.hpp>
#include <unifex/async_scope.hpp>
#include <unifex/detach_on_cancel.hpp>
#include <unifex/get_stop_token.hpp>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <variant>

#include <unifex/allocate.hpp>
#include <unifex/finally.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>

#include <gtest/gtest.h>

namespace {

struct mock_receiver_with_exception {
  template <typename... Values>
  void set_value(Values&&...) {
    throw std::runtime_error("Test error");
  }

  void set_error(std::exception_ptr) noexcept {
    error_called->store(true, std::memory_order_relaxed);
  }

  void set_done() noexcept {}

  std::atomic<bool>* error_called;
};

template <typename Sender, typename Scheduler>
auto with_scheduler(Sender&& sender, Scheduler&& sched) {
  return unifex::with_query_value(
      std::move(sender), unifex::get_scheduler, sched);
}
}  // namespace

struct detach_on_cancel_test : testing::Test {};

TEST_F(detach_on_cancel_test, set_value) {
  auto result = unifex::sync_wait(unifex::detach_on_cancel(unifex::just(42)));
  ASSERT_TRUE(result);
  EXPECT_EQ(42, *result);
}

TEST_F(detach_on_cancel_test, set_done) {
  auto result =
      unifex::sync_wait(unifex::detach_on_cancel(unifex::just_done()));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_error) {
  EXPECT_THROW(
      unifex::sync_wait(unifex::detach_on_cancel(
          unifex::just_error(std::runtime_error("Test error")))),
      std::runtime_error);
}

TEST_F(detach_on_cancel_test, set_value_after_cancellation) {
  auto result = unifex::sync_wait(
      unifex::let_value_with_stop_source([](auto& stop_source) {
        stop_source.request_stop();
        return unifex::detach_on_cancel(unifex::just(42));
      }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_done_after_cancellation) {
  auto result = unifex::sync_wait(
      unifex::let_value_with_stop_source([](auto& stop_source) {
        stop_source.request_stop();
        return unifex::detach_on_cancel(unifex::just_done());
      }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_error_after_cancellation) {
  auto result = unifex::sync_wait(
      unifex::let_value_with_stop_source([](auto& stop_source) {
        stop_source.request_stop();
        return unifex::detach_on_cancel(
            unifex::just_error(std::runtime_error("Test error")));
      }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_value_during_cancellation) {
  auto result = unifex::sync_wait(
      unifex::let_value_with_stop_source([](auto& stop_source) {
        return unifex::detach_on_cancel(unifex::sequence(
            unifex::just_from([&]() { stop_source.request_stop(); }),
            unifex::just(42)));
      }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_value_sets_error) {
  std::atomic<bool> error_called{false};
  auto opState = unifex::connect(
      unifex::detach_on_cancel(unifex::just(42)),
      mock_receiver_with_exception{&error_called});
  unifex::start(opState);
  EXPECT_TRUE(error_called.load(std::memory_order_relaxed));
}

TEST_F(detach_on_cancel_test, cancellation_and_completion_race) {
  static constexpr uint16_t max_iterations{10000};
  std::atomic<uint16_t> count{0};
  unifex::single_thread_context set_value_context;
  unifex::single_thread_context cancel_context;
  for (uint16_t i{0}; i < max_iterations; ++i) {
    unifex::sync_wait(
        unifex::let_value_with_stop_source([&](auto& source) noexcept {
          return unifex::when_all(
              unifex::finally(
                  unifex::on(
                      set_value_context.get_scheduler(), unifex::just(42)),
                  unifex::just_from([&]() noexcept {
                    count.fetch_add(1, std::memory_order_relaxed);
                  })),
              unifex::on(
                  cancel_context.get_scheduler(),
                  unifex::just_from(
                      [&]() noexcept { source.request_stop(); })));
        }));
    EXPECT_EQ(i + 1, count.load(std::memory_order_relaxed));
  }
  EXPECT_EQ(max_iterations, count.load(std::memory_order_relaxed));
}

TEST_F(detach_on_cancel_test, error_types_propagate) {
  using namespace unifex;
  using error_types =
      sender_error_types_t<decltype(detach_on_cancel(just())), type_list>;
  using v = typename error_types::template apply<std::variant>;

  EXPECT_GE(std::variant_size<v>::value, 1);
}

TEST_F(detach_on_cancel_test, cancel_inline) {
  unifex::async_manual_reset_event e1, e2;
  unifex::async_scope scope;
  unifex::single_thread_context main;
  // detached 1
  scope.detached_spawn_on(
      main.get_scheduler(),
      // finally() and allocate() to trigger ASAN
      unifex::finally(
          unifex::detach_on_cancel(unifex::allocate(unifex::detach_on_cancel(
              with_scheduler(e1.async_wait(), main.get_scheduler())))),
          unifex::just()));
  // detached 2
  scope.detached_spawn_on(
      main.get_scheduler(), unifex::allocate(unifex::just_from([&]() noexcept {
        e2.set();  // allow scope to cleanup()
      })));

  unifex::sync_wait(unifex::sequence(
      e2.async_wait(),
      // cancel attached work
      scope.cleanup(),
      // allow detached sender completion
      unifex::just_from([&]() noexcept { e1.set(); })));
}

TEST_F(detach_on_cancel_test, async_wait) {
  unifex::async_manual_reset_event e1, e2;
  unifex::async_scope scope;
  unifex::single_thread_context main;

  // spawn eagerly
  scope.detached_spawn_on(
      main.get_scheduler(),
      // finally() and allocate() to trigger ASAN
      unifex::finally(
          unifex::allocate(unifex::let_done(
              unifex::stop_when(
                  unifex::detach_on_cancel(
                      with_scheduler(e1.async_wait(), main.get_scheduler())),
                  unifex::detach_on_cancel(unifex::detach_on_cancel(
                      with_scheduler(e2.async_wait(), main.get_scheduler())))),
              []() noexcept { return unifex::just(); })),
          unifex::just()));

  // allow spawned future completion
  e1.set();
  unifex::sync_wait(scope.complete());
  // allow detached sender completion
  e2.set();
}
