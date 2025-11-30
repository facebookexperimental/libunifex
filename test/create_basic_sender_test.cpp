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
#  include <unifex/create_basic_sender.hpp>
#  include <unifex/let_value_with_stop_source.hpp>
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

struct Copyable {
  explicit Copyable(size_t& count) noexcept : count_(count) {}

  Copyable(const Copyable& rhs) noexcept : count_(rhs.count_) { ++count_; }

  Copyable& operator=(const Copyable& rhs) noexcept {
    std::construct_at(this, rhs);
    return *this;
  }

  size_t& count_;
};

struct Moveable {
  explicit Moveable(size_t& count) noexcept : count_(count) {}

  Moveable(Moveable&& rhs) noexcept : count_(rhs.count_) { ++count_; }

  Moveable& operator=(Moveable&& rhs) noexcept {
    std::construct_at(this, std::forward<Moveable>(rhs));
    return *this;
  }

  size_t& count_;
};

struct ThrowOnCopy {
  explicit ThrowOnCopy() noexcept {}

  ThrowOnCopy(const ThrowOnCopy& /* rhs */) {
    throw std::runtime_error("cannot copy");
  }

  ThrowOnCopy& operator=(const ThrowOnCopy& rhs) {
    std::construct_at(this, rhs);
    return *this;
  }
};

}  // namespace

struct create_basic_sender_test : public ::testing::Test {
  ~create_basic_sender_test() { sync_wait(scope.complete()); }

  template <typename Delay, typename Fn>
  void call_after(Delay delay, Fn&& fn) {
    scope.detached_spawn(
        schedule_after(timer.get_scheduler(), delay) |
        then(std::forward<Fn>(fn)));
  }

  template <typename Delay, typename Fn>
  timer_callback safe_call_after(Delay delay, Fn&& fn) {
    auto result{
        std::make_shared<timer_callback_holder<Fn>>(std::forward<Fn>(fn))};
    call_after(delay, [cb{std::weak_ptr{result}}]() noexcept(noexcept(fn())) {
      if (auto callback{cb.lock()}) {
        (*callback)();
      }
    });
    return result;
  }

  async_scope scope;
  single_thread_context context;
  timed_single_thread_context timer;
};
TEST_F(create_basic_sender_test, set_value_sync) {
  EXPECT_EQ(1234, sync_wait(create_basic_sender<int>([](auto event, auto& op) {
              if constexpr (event.is_start) {
                op.set_value(1234);
              }
            })));
}

TEST_F(create_basic_sender_test, set_error_sync) {
  EXPECT_THROW(
      sync_wait(create_basic_sender<int>([](auto event, auto& op) {
        if constexpr (event.is_start) {
          op.set_error(std::make_exception_ptr(std::runtime_error("fail")));
        }
      })),
      std::runtime_error);
}

TEST_F(create_basic_sender_test, set_value) {
  EXPECT_EQ(
      1234, sync_wait(create_basic_sender<int>([this](auto event, auto& op) {
        if constexpr (event.is_start) {
          call_after(100ms, safe_callback<>(op));
        } else if constexpr (event.is_callback) {
          op.set_value(1234);
        }
      })));
}

TEST_F(create_basic_sender_test, non_cancellable_set_value) {
  EXPECT_EQ(
      1234,
      sync_wait(stop_when(
          create_basic_sender<int>(
              [this](auto event, auto& op) {
                if constexpr (event.is_start) {
                  call_after(500ms, safe_callback<>(op));
                } else if constexpr (event.is_callback) {
                  op.set_value(1234);
                }
              },
              with_sender_traits<S{.sends_done = false}>),
          schedule_after(timer.get_scheduler(), 100ms))));
}

TEST_F(create_basic_sender_test, set_error) {
  EXPECT_THROW(
      sync_wait(create_basic_sender<int>([this](auto event, auto& op) {
        if constexpr (event.is_start) {
          call_after(100ms, safe_errback<>(op));
        } else if constexpr (event.is_errback) {
          op.set_error(std::make_exception_ptr(std::runtime_error("fail")));
        }
      })),
      std::runtime_error);
}

TEST_F(create_basic_sender_test, non_cancellable_set_error) {
  EXPECT_THROW(
      sync_wait(stop_when(
          create_basic_sender<int>(
              [this](auto event, auto& op) {
                if constexpr (event.is_start) {
                  call_after(500ms, safe_errback<>(op));
                } else if constexpr (event.is_errback) {
                  op.set_error(
                      std::make_exception_ptr(std::runtime_error("fail")));
                }
              },
              with_sender_traits<S{.sends_done = false}>),
          schedule_after(timer.get_scheduler(), 100ms))),
      std::runtime_error);
}

TEST_F(create_basic_sender_test, set_done) {
  bool stopped{false};
  EXPECT_FALSE(sync_wait(stop_when(
      on(context.get_scheduler(),
         create_basic_sender<int>([this, &stopped](auto event, auto& op) {
           if constexpr (event.is_start) {
             call_after(500ms, safe_callback<>(op));
           } else if constexpr (event.is_callback) {
             op.set_value(1234);
           } else if constexpr (event.is_stop) {
             stopped = true;
             op.set_done();
           }
         })),
      schedule_after(timer.get_scheduler(), 100ms))));

  EXPECT_TRUE(stopped);
}

TEST_F(create_basic_sender_test, set_done_with_unsafe_cb_and_context) {
  EXPECT_FALSE(sync_wait(stop_when(
      on(context.get_scheduler(),
         create_basic_sender<int>(
             [this](auto event, auto& op) mutable {
               if constexpr (event.is_start) {
                 auto [arg, callback] = unsafe_callback<>(op).opaque();
                 op.context() = safe_call_after(
                     500ms, [arg, callback]() noexcept { (*callback)(arg); });
               } else if constexpr (event.is_callback) {
                 op.set_value(1234);
               } else if constexpr (event.is_stop) {
                 op.set_done();
               }
             },
             []() noexcept { return timer_callback{}; })),
      schedule_after(timer.get_scheduler(), 100ms))));
}

TEST_F(create_basic_sender_test, context_factory_with_receiver) {
  EXPECT_EQ(
      1234,
      sync_wait(
          on(context.get_scheduler(),
             create_basic_sender<int>(
                 [this](auto event, auto& op) {
                   EXPECT_TRUE(op.context());
                   if constexpr (event.is_start) {
                     call_after(100ms, safe_callback<>(op));
                   } else if constexpr (event.is_callback) {
                     op.set_value(1234);
                   }
                 },
                 [this](auto& receiver) noexcept {
                   return context.get_scheduler() == get_scheduler(receiver);
                 }))));
}

TEST_F(create_basic_sender_test, lock_factory) {
  EXPECT_EQ(
      1234,
      sync_wait(
          create_basic_sender<int>(
              [this](auto event, auto& op) {
                if constexpr (event.is_start) {
                  call_after(100ms, safe_callback<>(op));
                } else if constexpr (event.is_callback) {
                  op.set_value(1234);
                }
              },
              []() noexcept { return std::mutex{}; },
              [](std::mutex& mutex) noexcept {
                return std::lock_guard{mutex};
              })));
}

TEST_F(create_basic_sender_test, lock_factory_global) {
  std::mutex mutex;
  EXPECT_EQ(
      1234,
      sync_wait(
          create_basic_sender<int>(
              [this](auto event, auto& op) {
                if constexpr (event.is_start) {
                  call_after(100ms, safe_callback<>(op));
                } else if constexpr (event.is_callback) {
                  op.set_value(1234);
                }
              },
              []() noexcept { return std::tuple{}; },
              [&mutex]() noexcept { return std::lock_guard{mutex}; })));
}

TEST_F(create_basic_sender_test, early_cancellation) {
  bool started{false}, stopped{false};
  EXPECT_FALSE(sync_wait(let_value_with_stop_source(
      [&started, &stopped](auto& stop_source) mutable noexcept {
        stop_source.request_stop();
        return create_basic_sender<int>(
            [&started, &stopped](auto event, auto& /* op */) {
              if constexpr (event.is_start) {
                started = true;
              } else if constexpr (event.is_stop) {
                // Should not execute
                stopped = true;
              }
            });
      })));

  EXPECT_FALSE(started);
  EXPECT_FALSE(stopped);  // cancelled before start, stopped event not needed
}

TEST_F(create_basic_sender_test, late_callback) {
  bool stopped{false}, had_callback{false};
  EXPECT_FALSE(sync_wait(stop_when(
      on(context.get_scheduler(),
         create_basic_sender<int>([this, &stopped, &had_callback](
                                      auto event, auto& op) {
           if constexpr (event.is_start) {
             call_after(500ms, safe_callback<>(op, [&had_callback]() noexcept {
                          had_callback = true;
                        }));
           } else if constexpr (event.is_callback) {
             op.set_value(1234);
           } else if constexpr (event.is_stop) {
             stopped = true;
             op.set_done();
           }
         })),
      schedule_after(timer.get_scheduler(), 100ms))));

  EXPECT_TRUE(stopped);

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));

  EXPECT_TRUE(had_callback);
}

#  if 0
TEST_F(create_basic_sender_test, callback_and_late_errback_with_args) {
  size_t copied{0}, moved{0}, late_errback{false};
    EXPECT_EQ(
        1234,
  sync_wait(
      create_basic_sender<int>([this, &copied, &moved, &late_errback](
                                   auto event, auto& op, auto&&... args) {
        if constexpr (event.is_start) {
          auto callback = safe_callback<int, const Copyable&, Moveable&&>(op);
          call_after(
              100ms,
              [&copied, &moved, callback{std::move(callback)}]() noexcept {
                callback(1234, Copyable{copied}, Moveable{moved});
              });

              auto errback =
                  safe_errback<int>(op, [&late_errback](int code) noexcept {
                    EXPECT_EQ(5678, code);
                    late_errback = true;
                  });
              call_after(500ms, [errback{std::move(errback)}]() noexcept {
                errback(5678);
              });
        } else if constexpr (event.is_callback) {
          auto [result, cp, mv] = std::tuple<decltype(args)...>{
              std::forward<decltype(args)>(args)...};
          op.set_value(result);
          (void)cp;
          (void)mv;
        } else if constexpr (event.is_errback) {
              auto [code] = std::tuple<decltype(args)...>{
                  std::forward<decltype(args)>(args)...};
              (void)code;
              FAIL();
            }
      })));

  EXPECT_EQ(0, copied);
  EXPECT_EQ(0, moved);

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));

  EXPECT_TRUE(late_errback);
}
#  endif

namespace {

struct Body {
  create_basic_sender_test& test;
  size_t& copied;
  size_t& moved;
  bool& late_errback;

  void start(auto& op) {
    auto callback = safe_callback<int, const Copyable&, Moveable&&>(op);
    test.call_after(100ms, [this, callback{std::move(callback)}]() noexcept {
      callback(1234, Copyable{copied}, Moveable{moved});
    });

    bool& late{late_errback};
    auto errback = safe_errback<int>(op, [&late](int code) noexcept {
      EXPECT_EQ(5678, code);
      late = true;
    });
    test.call_after(
        500ms, [errback{std::move(errback)}]() noexcept { errback(5678); });
  }

  void callback(
      auto& op, int result, const Copyable& /* cp */, Moveable&& /* mv */) {
    op.set_value(result);
  }

  void errback(auto& /* op */, int /* code */) { FAIL(); }
};

}  // namespace

TEST_F(create_basic_sender_test, callback_and_late_errback_methods_with_args) {
  size_t copied{0}, moved{0};
  bool late_errback{false};

  EXPECT_EQ(
      1234,
      sync_wait(
          create_basic_sender<int>(Body{*this, copied, moved, late_errback})));

  EXPECT_EQ(0, copied);
  EXPECT_EQ(0, moved);

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));

  EXPECT_TRUE(late_errback);
}

TEST_F(create_basic_sender_test, opaque_callback_no_fallback) {
  std::optional<basic_sender_opaque_callback<int>> safe;

  EXPECT_FALSE(sync_wait(stop_when(
      create_basic_sender<int>([this,
                                &safe](auto event, auto& op, auto&&... args) {
        if constexpr (event.is_start) {
          safe.emplace(safe_callback<int>(op).opaque());
          call_after(
              500ms, [ctx{safe->context()}, cb{safe->callback()}]() noexcept {
                (*cb)(ctx, 1234);
              });
        } else if constexpr (event.is_callback) {
          auto [result] = std::tuple{args...};
          op.set_value(result);
        } else if constexpr (event.is_stop) {
          op.set_done();
        }
      }),
      schedule_after(timer.get_scheduler(), 100ms))));

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));
}

TEST_F(create_basic_sender_test, opaque_callback_exact_type_fallback) {
  int late_result{0};
  auto fallback{[&late_result](int result) { late_result = result; }};
  std::optional<
      basic_sender_opaque_callback_with_fallback<decltype(fallback), int>>
      safe;

  EXPECT_FALSE(sync_wait(stop_when(
      create_basic_sender<int>([this, &safe, &fallback](
                                   auto event, auto& op, auto&&... args) {
        if constexpr (event.is_start) {
          safe.emplace(safe_callback<int>(op, fallback).opaque());
          call_after(
              500ms, [ctx{safe->context()}, cb{safe->callback()}]() noexcept {
                (*cb)(ctx, 1234);
              });
        } else if constexpr (event.is_callback) {
          auto [result] = std::tuple{args...};
          op.set_value(result);
        } else if constexpr (event.is_stop) {
          op.set_done();
        }
      }),
      schedule_after(timer.get_scheduler(), 100ms))));

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));
  EXPECT_EQ(1234, late_result);
}

TEST_F(create_basic_sender_test, opaque_callback_type_erased_fallback) {
  int late_result{0};
  using safe_callback_t =
      basic_sender_opaque_callback_with_fallback<std::function<void(int)>, int>;

  std::optional<safe_callback_t> safe;

  EXPECT_FALSE(sync_wait(stop_when(
      create_basic_sender<int>([this, &safe, &late_result](
                                   auto event, auto& op, auto&&... args) {
        if constexpr (event.is_start) {
          safe.emplace(safe_callback<int>(op, [&late_result](int result) {
                         late_result = result;
                       }).template opaque<safe_callback_t>());
          call_after(
              500ms, [ctx{safe->context()}, cb{safe->callback()}]() noexcept {
                (*cb)(ctx, 1234);
              });
        } else if constexpr (event.is_callback) {
          auto [result] = std::tuple{args...};
          op.set_value(result);
        } else if constexpr (event.is_stop) {
          op.set_done();
        }
      }),
      schedule_after(timer.get_scheduler(), 100ms))));

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));
  EXPECT_EQ(1234, late_result);
}

TEST_F(create_basic_sender_test, opaque_callback_ptr_fallback) {
  using safe_callback_t =
      basic_sender_opaque_callback_with_fallback<void (*)(int), int>;

  std::optional<safe_callback_t> safe;

  EXPECT_FALSE(sync_wait(stop_when(
      create_basic_sender<int>([this,
                                &safe](auto event, auto& op, auto&&... args) {
        if constexpr (event.is_start) {
          safe.emplace(safe_callback<int>(op, [](int result) {
                         EXPECT_EQ(1234, result);
                       }).template opaque<safe_callback_t>());
          call_after(
              500ms, [ctx{safe->context()}, cb{safe->callback()}]() noexcept {
                (*cb)(ctx, 1234);
              });
        } else if constexpr (event.is_callback) {
          auto [result] = std::tuple{args...};
          op.set_value(result);
        } else if constexpr (event.is_stop) {
          op.set_done();
        }
      }),
      schedule_after(timer.get_scheduler(), 100ms))));

  sync_wait(schedule_after(timer.get_scheduler(), 500ms));
}

TEST_F(create_basic_sender_test, non_affine_set_value) {
  auto threadId{std::this_thread::get_id()};
  size_t copied{0}, moved{0};
  EXPECT_TRUE(sync_wait(
      create_basic_sender<const Copyable&, Moveable&&, const ThrowOnCopy&>(
          [this, &copied, &moved](auto event, auto& op) {
            if constexpr (event.is_start) {
              call_after(100ms, safe_callback<>(op));
            } else if constexpr (event.is_callback) {
              op.set_value(Copyable{copied}, Moveable{moved}, ThrowOnCopy{});
            }
          }) |
      then([threadId](
               const Copyable& /* cp */,
               Moveable&& /* mv */,
               const ThrowOnCopy& /* tc */) noexcept {
        EXPECT_NE(threadId, std::this_thread::get_id());
      })));

  EXPECT_EQ(0, copied);
  EXPECT_EQ(0, moved);
}

TEST_F(create_basic_sender_test, affine_set_value) {
  auto threadId{std::this_thread::get_id()};
  size_t copied{0}, moved{0};
  EXPECT_TRUE(sync_wait(
      create_basic_sender<
          const Copyable&,
          Moveable&&,
          const Copyable*,
          const Moveable*>(
          [this, &copied, &moved](auto event, auto& op) {
            if constexpr (event.is_start) {
              call_after(100ms, safe_callback<>(op));
            } else if constexpr (event.is_callback) {
              Copyable cp{copied};
              Moveable mv{moved};
              op.set_value(cp, std::move(mv), &cp, &mv);
            }
          },
          with_sender_traits<S{.is_always_scheduler_affine = true}>) |
      then([threadId](
               const Copyable& cp,
               Moveable&& mv,
               const Copyable* pc,
               const Moveable* pm) noexcept {
        EXPECT_NE(&cp, pc);
        EXPECT_NE(&mv, pm);
        EXPECT_EQ(threadId, std::this_thread::get_id());
      })));

  EXPECT_EQ(1, copied);
  EXPECT_EQ(1, moved);
}

TEST_F(create_basic_sender_test, affine_set_value_failure) {
  bool returned{false};
  EXPECT_THROW(
      sync_wait(
          create_basic_sender<const ThrowOnCopy&>(
              [this](auto event, auto& op) {
                if constexpr (event.is_start) {
                  call_after(100ms, safe_callback<>(op));
                } else if constexpr (event.is_callback) {
                  op.set_value(ThrowOnCopy{});
                }
              },
              with_sender_traits<S{.is_always_scheduler_affine = true}>) |
          then([&returned](const ThrowOnCopy& /* tc */) noexcept {
            returned = true;
          })),
      std::runtime_error);

  EXPECT_FALSE(returned);
}

#endif
