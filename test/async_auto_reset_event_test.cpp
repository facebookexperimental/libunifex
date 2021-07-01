// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <unifex/async_auto_reset_event.hpp>
#include <unifex/inline_scheduler.hpp>
#include <unifex/next_adapt_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>

using event = unifex::async_auto_reset_event;

struct AsyncAutoResetEventTests : testing::Test {};

namespace {

template <typename Stream>
auto countElements(Stream&& stream) noexcept {
  return unifex::sync_wait(unifex::reduce_stream(
      (Stream&)stream, 0, [](int count) noexcept { return ++count; }));
}

}  // namespace

TEST_F(AsyncAutoResetEventTests, canConstructAnEvent) {
  event evt;
}

TEST_F(
    AsyncAutoResetEventTests,
    reducingStream_thatIsImmediatelySetDone_producesNoSums) {
  event evt;

  evt.set_done();

  auto result = countElements(evt.stream());

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 0);
}

TEST_F(AsyncAutoResetEventTests, queueNext_respondsToStopRequests) {
  event evt;

  unifex::inplace_stop_source stopSource;

  stopSource.request_stop();

  auto result = countElements(unifex::next_adapt_stream(
      evt.stream(), [&stopSource](auto&& next) noexcept {
        return unifex::with_query_value(
            std::move(next), unifex::get_stop_token, stopSource.get_token());
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 0);
}

TEST_F(AsyncAutoResetEventTests, reducingStream_thatHasAValue_generatesASum) {
  event evt;

  evt.set();

  auto result = unifex::sync_wait(
      unifex::reduce_stream(evt.stream(), 0, [&](int count) noexcept {
        evt.set_done();
        return ++count;
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 1);
}

TEST_F(
    AsyncAutoResetEventTests, reducingStream_thatWasBornReady_generatesASum) {
  event evt{true};

  auto result = unifex::sync_wait(
      unifex::reduce_stream(evt.stream(), 0, [&](int count) noexcept {
        evt.set_done();
        return ++count;
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 1);
}

TEST_F(
    AsyncAutoResetEventTests,
    callingSet_afterResettingTheEvent_createsAnotherStreamElement) {
  event evt;

  evt.set();

  auto result = unifex::sync_wait(
      unifex::reduce_stream(evt.stream(), 0, [&](int count) noexcept {
        if (count < 2) {
          evt.set();
        } else {
          evt.set_done();
        }
        return ++count;
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 3);
}

TEST_F(AsyncAutoResetEventTests, repeatedCallsToSet_areIdempotent) {
  event evt;

  evt.set();
  evt.set();

  auto result = unifex::sync_wait(
      unifex::reduce_stream(evt.stream(), 0, [&](int count) noexcept {
        if (count < 2) {
          evt.set();
          evt.set();
        } else {
          evt.set_done();
        }
        return ++count;
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 3);
}

TEST_F(
    AsyncAutoResetEventTests,
    eventNext_respondsToStopRequests_afterProducingElements) {
  event evt;

  unifex::inplace_stop_source stopSource;

  evt.set();

  auto adapt = [&] {
    return unifex::next_adapt_stream(
        evt.stream(), [&stopSource](auto&& next) noexcept {
          return unifex::with_query_value(
              std::move(next), unifex::get_stop_token, stopSource.get_token());
        });
  };

  auto result = unifex::sync_wait(
      unifex::reduce_stream(adapt(), 0, [&](int count) noexcept {
        if (count < 2) {
          evt.set();
        } else {
          stopSource.request_stop();
        }
        return ++count;
      }));

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 3);
}
