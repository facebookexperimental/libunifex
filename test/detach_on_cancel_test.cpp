#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <unifex/detach_on_cancel.hpp>
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_manual_reset_event.hpp>

#include <gtest/gtest.h>
#include <unifex/inline_scheduler.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/sequence.hpp>
#include <unifex/just_from.hpp>
#include <unifex/single_thread_context.hpp>

namespace {

  struct mock_receiver_with_exception {
    mock_receiver_with_exception() {};

    template <typename... Values>
    void set_value(Values&&...) {
      throw std::runtime_error("Test error");
    }

    void set_error(std::exception_ptr) noexcept {
      set_error_called->store(true, std::memory_order_relaxed);
    }

    void set_done() noexcept {}

    std::unique_ptr<std::atomic<bool>> set_error_called = std::make_unique<std::atomic<bool>>(false);
  };

  struct mock_receiver_with_count {
    mock_receiver_with_count(std::atomic<uint16_t>* count, unifex::inplace_stop_source* stop_source) :
                                      count(count),
                                      stop_source(stop_source) {};

    template <typename... Values>
    void set_value(Values&&...) {
      count->fetch_add(1, std::memory_order_relaxed);
    }

    void set_error(std::exception_ptr) noexcept {}

    void set_done() noexcept {
      count->fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<uint16_t>* count;
    unifex::inplace_stop_source* stop_source;

    friend unifex::inplace_stop_token tag_invoke(unifex::tag_t<unifex::get_stop_token>, const mock_receiver_with_count& r) noexcept {
      return r.stop_source->get_token();
    }
  };
}

struct detach_on_cancel_test : testing::Test {
  mock_receiver_with_exception receiver_with_exception;
  std::atomic<bool>& mock_receiver_with_exception_set_error_called = *receiver_with_exception.set_error_called;

  std::atomic<uint16_t> count = 0;
  unifex::inplace_stop_source stop_source;
  mock_receiver_with_count receiver_with_count{&count, &stop_source};
};

TEST_F(detach_on_cancel_test, set_value) {
  auto result = unifex::sync_wait(unifex::detach_on_cancel(unifex::just(42)));
  ASSERT_TRUE(result);
  EXPECT_EQ(42, *result);
}

TEST_F(detach_on_cancel_test, set_done) {
  auto result = unifex::sync_wait(unifex::detach_on_cancel(unifex::just_done()));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_error) {
  EXPECT_THROW(unifex::sync_wait(unifex::detach_on_cancel(unifex::just_error(std::runtime_error("Test error")))), std::runtime_error);
}

TEST_F(detach_on_cancel_test, set_value_after_cancellation) {
  auto result = unifex::sync_wait(unifex::let_value_with_stop_source([](auto& stop_source) {
    stop_source.request_stop();
    return unifex::detach_on_cancel(unifex::just(42));
  }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_done_after_cancellation) {
  auto result = unifex::sync_wait(unifex::let_value_with_stop_source([](auto& stop_source) {
    stop_source.request_stop();
    return unifex::detach_on_cancel(unifex::just_done());
  }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_error_after_cancellation) {
  auto result = unifex::sync_wait(unifex::let_value_with_stop_source([](auto& stop_source) {
    stop_source.request_stop();
    return unifex::detach_on_cancel(unifex::just_error(std::runtime_error("Test error")));
  }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_value_during_cancellation) {
  auto result = unifex::sync_wait(unifex::let_value_with_stop_source([](auto& stop_source) {
    return unifex::detach_on_cancel(unifex::sequence(unifex::just_from([&](){
      stop_source.request_stop();
    }),
    unifex::just(42)));
  }));
  ASSERT_FALSE(result);
}

TEST_F(detach_on_cancel_test, set_value_sets_error) {
  auto opState = unifex::connect(unifex::detach_on_cancel(unifex::just(42)), std::move(receiver_with_exception));
  unifex::start(opState);
  EXPECT_TRUE(mock_receiver_with_exception_set_error_called.load());
}

TEST_F(detach_on_cancel_test, cancellation_and_completion_race) {
  uint16_t max_iterations = 10000;
  unifex::single_thread_context set_value_context;
  auto set_value_scheduler = set_value_context.get_scheduler();
  unifex::single_thread_context cancel_context;
  auto cancel_scheduler = cancel_context.get_scheduler();
  for (uint16_t i = 0; i < max_iterations; i++) {
    auto opState = unifex::connect(unifex::detach_on_cancel(unifex::on(set_value_scheduler,
        unifex::just(42))), std::move(receiver_with_count));
    unifex::start(opState);
    unifex::sync_wait(unifex::on(cancel_scheduler, unifex::just_from([&]() {stop_source.request_stop();})));
  }

  EXPECT_EQ(max_iterations, count.load());

}
