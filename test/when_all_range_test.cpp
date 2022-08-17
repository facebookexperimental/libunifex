// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <unifex/allocate.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/finally.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>

#include <unifex/when_all_range.hpp>

using namespace unifex;

class WhenAllRangeTests : public ::testing::Test {};

namespace {
constexpr auto times_three = [](int x) noexcept {
  return then(just(x), [](int val) noexcept { return val * 3; });
};

template <typename StopToken, typename Callback>
auto make_stop_callback(StopToken stoken, Callback callback) {
  using stop_callback_t = typename StopToken::template callback_type<Callback>;

  return stop_callback_t{stoken, std::move(callback)};
}

struct _cancel_only_sender {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;
  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;
  static constexpr bool sends_done = true;

  template <typename Receiver>
  struct operation {
    struct cancel_operation {
      operation& op_;

      void operator()() noexcept { op_.set_done(); }
    };
    using receiver_t = remove_cvref_t<Receiver>;
    using receiver_stop_token_t = stop_token_type_t<Receiver&>;

    receiver_t receiver_;
    receiver_stop_token_t stoken_ = _get_stop_token::get_stop_token(receiver_);
    cancel_operation cancel_{*this};
    decltype(make_stop_callback(stoken_, cancel_)) callback_ =
        make_stop_callback(stoken_, cancel_);

    operation(receiver_t receiver) : receiver_(std::move(receiver)) {}
    // not movable or copyable
    operation(operation&& op) = delete;

    void start() noexcept {}

    void set_done() noexcept { unifex::set_done(std::move(receiver_)); }
  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) const& noexcept {
    return operation<Receiver>{static_cast<Receiver&&>(receiver)};
  }
};

inline const struct _fn {
  template <typename... Values>
  constexpr auto operator()() const noexcept {
    return _cancel_only_sender{};
  }
} cancel_only_sender{};

}  // namespace

TEST_F(WhenAllRangeTests, givenReceiver_whenAllValue_thenReceivedValue) {
  // ----- WHEN -----
  std::vector<decltype(times_three(0))> works;

  for (int i = 0; i < 10; i++) {
    works.push_back(times_three(i));
  }

  auto result = sync_wait(when_all_range(std::move(works)));

  // ----- THEN ----- (convential)
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.value().size(), 10);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(result.value()[i], i * 3);
  }
}

TEST_F(WhenAllRangeTests, givenReceiver_whenError_thenReceivedError) {
  auto make_work = [](int x) {
    return then(just(x), [](int val) {
      if (val == 5) {
        throw std::exception{};
      }
      return val * 3;
    });
  };

  std::vector<decltype(make_work(0))> works;

  for (int i = 0; i < 10; i++) {
    works.push_back(make_work(i));
  }
  EXPECT_THROW(
      { auto result = sync_wait(when_all_range(std::move(works))); },
      std::exception);
}

TEST_F(
    WhenAllRangeTests, givenReceiver_whenZeroSender_thenImmediatelyReceives) {
  // ----- WHEN -----
  auto make_work = [] {
    return then(just(), [] {});
  };
  std::vector<decltype(make_work())> works;  // empty
  // Start the structured `operation`
  auto result = sync_wait(when_all_range(std::move(works)));
  // ----- THEN ----- (convential)
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST_F(WhenAllRangeTests, senderAsIterator) {
  std::vector<decltype(times_three(0))> works;
  for (int i = 0; i < 10; i++) {
    works.push_back(times_three(i));
  }
  auto result = sync_wait(when_all_range(works.begin(), works.end()));
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.value().size(), 10);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(result.value()[i], i * 3);
  }
}

TEST_F(WhenAllRangeTests, noCopy) {
  auto make_work = [](int x) noexcept {
    return let_value_with_stop_source(
        [x](auto&) noexcept { return times_three(x); });
  };
  std::vector<decltype(make_work(0))> work;
  for (int i = 0; i < 10; i++) {
    work.push_back(make_work(i));
  }
  auto result = sync_wait(when_all_range(std::move(work)));
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.value().size(), 10);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(result.value()[i], i * 3);
  }
}

TEST_F(WhenAllRangeTests, ErrorCancelsRest) {
  try {
    std::vector<any_sender_of<>> work;

    work.emplace_back(finally(  // arm #1: use allocate() to trigger ASAN
        allocate(when_all_range(std::vector{cancel_only_sender()})) |
            then([](auto) {}),
        just()));

    work.emplace_back(  // arm #2: immediately throw to trigger cancellation of
                        // arm #1
        just_from([]() { throw 1; }));

    sync_wait(when_all_range(std::move(work)));
  } catch (...) {
  }
}
