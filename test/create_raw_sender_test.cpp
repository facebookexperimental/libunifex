/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES && __cplusplus >= 201911L

#  include <unifex/async_scope.hpp>
#  include <unifex/create_raw_sender.hpp>
#  include <unifex/get_stop_token.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/timed_single_thread_context.hpp>

#  include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono_literals;
using namespace testing;

namespace {

#  if defined(__clang_major__) && __clang_major__ < 18
// https://github.com/llvm/llvm-project/issues/58434
using S = _make_traits::sender_traits_literal;
#  else
#    define S
#  endif

template <typename Receiver>
struct stop_callback {
  explicit stop_callback(Receiver& receiver) noexcept : receiver_(receiver) {}

  void operator()() const noexcept { unifex::set_done(std::move(receiver_)); }

  Receiver& receiver_;
};

struct timer_callback_base {
  virtual ~timer_callback_base() noexcept {}
};

template <typename Fn>
struct timer_callback_holder : public timer_callback_base {
  explicit timer_callback_holder(Fn&& fn) noexcept
    : fn_(std::forward<Fn>(fn)) {}

  void operator()() noexcept(noexcept(fn_())) { fn_(); }

  Fn fn_;
};

using timer_callback = std::shared_ptr<timer_callback_base>;

template <typename Receiver>
struct opstate {
  void start() noexcept { set_value(std::move(receiver_), 1234); }
  Receiver receiver_;
};

}  // namespace

struct create_raw_sender_test : public ::testing::Test {
  ~create_raw_sender_test() { sync_wait(scope.complete()); }

  template <typename Delay, typename Fn>
  timer_callback call_after(Delay delay, Fn&& fn) {
    auto result{
        std::make_shared<timer_callback_holder<Fn>>(std::forward<Fn>(fn))};
    scope.detached_spawn(
        schedule_after(timer.get_scheduler(), delay) |
        then([cb{std::weak_ptr{result}}]() noexcept(noexcept(fn())) {
          if (auto callback{cb.lock()}) {
            (*callback)();
          }
        }));
    return result;
  }

  async_scope scope;
  single_thread_context ctx;
  timed_single_thread_context timer;
};

TEST_F(create_raw_sender_test, set_value_sync) {
  EXPECT_EQ(1234, sync_wait(create_raw_sender<int>([](auto&& receiver) {
              return [receiver{std::forward<decltype(receiver)>(
                         receiver)}]() mutable noexcept {
                set_value(std::move(receiver), 1234);
              };
            })));
}

TEST_F(create_raw_sender_test, set_error_sync) {
  EXPECT_THROW(
      sync_wait(create_raw_sender<int>([](auto&& receiver) {
        return [receiver{std::forward<decltype(receiver)>(
                   receiver)}]() mutable noexcept {
          set_error(
              std::move(receiver),
              std::make_exception_ptr(std::runtime_error("fail")));
        };
      })),
      std::runtime_error);
}

TEST_F(create_raw_sender_test, explicit_opstate) {
  EXPECT_EQ(1234, sync_wait(create_raw_sender<int>([](auto&& receiver) {
              using receiver_t = std::decay_t<decltype(receiver)>;
              return opstate<receiver_t>{{std::forward<receiver_t>(receiver)}};
            })));
}

TEST_F(create_raw_sender_test, set_value) {
  EXPECT_EQ(1234, sync_wait(create_raw_sender<int>([this](auto&& receiver) {
              return [this,
                      receiver{std::forward<decltype(receiver)>(receiver)},
                      cb{timer_callback{}}]() mutable noexcept {
                cb = call_after(100ms, [&receiver]() noexcept {
                  set_value(std::move(receiver), 1234);
                });
              };
            })));
}

TEST_F(create_raw_sender_test, set_error) {
  EXPECT_THROW(
      sync_wait(create_raw_sender<int>([this](auto&& receiver) {
        return [this,
                receiver{std::forward<decltype(receiver)>(receiver)},
                cb{timer_callback{}}]() mutable noexcept {
          cb = call_after(100ms, [&receiver]() noexcept {
            set_error(
                std::move(receiver),
                std::make_exception_ptr(std::runtime_error("fail")));
          });
        };
      })),
      std::runtime_error);
}

TEST_F(create_raw_sender_test, select_traits) {
  auto sender{create_raw_sender<int>(
      [](auto&&) {
        return []() noexcept {
        };
      },
      with_sender_traits<S{.sends_done = false}>)};
  EXPECT_FALSE(decltype(sender)::sends_done);
}

TEST_F(create_raw_sender_test, set_done) {
  EXPECT_FALSE(sync_wait(stop_when(
      on(ctx.get_scheduler(), create_raw_sender<int>([this](auto&& receiver) {
           using receiver_t = std::decay_t<decltype(receiver)>;
           using stop_callback_t = stop_token_type_t<
               receiver_t>::template callback_type<stop_callback<receiver_t>>;

           return [this,
                   receiver{std::forward<decltype(receiver)>(receiver)},
                   stop{std::optional<stop_callback_t>{}},
                   cb{timer_callback{}}]() mutable noexcept {
             stop.emplace(get_stop_token(receiver), stop_callback{receiver});
             cb = call_after(500ms, [&receiver, &stop]() noexcept {
               stop.reset();
               set_value(std::move(receiver), 1234);
             });
           };
         })),
      schedule_after(timer.get_scheduler(), 100ms))));
}

#endif
