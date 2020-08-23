/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/executor_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/std_concepts.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
  struct inline_executor {
    UNIFEX_TEMPLATE(typename Fn)
      (requires invocable<Fn&>)
    void execute(Fn fn) const {
      fn();
    }
    [[maybe_unused]] friend bool operator==(inline_executor, inline_executor) noexcept {
      return true;
    }
    [[maybe_unused]] friend bool operator!=(inline_executor, inline_executor) noexcept {
      return false;
    }
  };

#if !UNIFEX_NO_EXCEPTIONS
  struct throwing_executor {
    UNIFEX_TEMPLATE(typename Fn)
      (requires invocable<Fn&>)
    void execute(Fn fn) const {
      throw std::runtime_error("sorry, charlie");
    }
    [[maybe_unused]] friend bool operator==(throwing_executor, throwing_executor) noexcept {
      return true;
    }
    [[maybe_unused]] friend bool operator!=(throwing_executor, throwing_executor) noexcept {
      return false;
    }
  };
#endif // !UNIFEX_NO_EXCEPTIONS

  class inline_sender : sender_base {
    template <typename Receiver>
    struct _op {
      Receiver r_;
      void start() & noexcept {
        UNIFEX_TRY {
          set_value((Receiver&&) r_);
        } UNIFEX_CATCH(...) {
          set_error((Receiver&&) r_, std::current_exception());
        }
      }
    };
  public:
    UNIFEX_TEMPLATE(typename Receiver)
      (requires receiver_of<Receiver>)
    _op<Receiver> connect(Receiver r) const {
      return _op<Receiver>{(Receiver&&) r};
    }
  };
}

TEST(P0443, execute_with_executor) {
  int i = 0;
  execute(inline_executor{}, [&]() { ++i; });
  EXPECT_EQ(1, i);
}

TEST(P0443, execute_with_sender) {
  int i = 0;
  execute(inline_sender{}, [&]() { ++i; });
  EXPECT_EQ(1, i);
}

TEST(P0443, connect_with_executor) {
  int i = 0;
  struct _receiver {
    int *p;
    void set_value() && noexcept {
      ++(*p);
    }
    void set_error(std::exception_ptr) && noexcept {}
    void set_done() && noexcept {}
  };
  auto op = connect(inline_executor{}, _receiver{&i});
  start(op);
  EXPECT_EQ(1, i);
}

#if !UNIFEX_NO_EXCEPTIONS
TEST(P0443, connect_with_throwing_executor) {
  int i = 0;
  struct _receiver {
    int *p;
    void set_value() && noexcept {
      *p += 1;
    }
    void set_error(std::exception_ptr) && noexcept {
      *p += 2;
    }
    void set_done() && noexcept {
      *p += 4;
    }
  };
  auto op = connect(throwing_executor{}, _receiver{&i});
  try {
    start(op);
  } catch (...) {}
  EXPECT_EQ(4, i);
}
#endif // !UNIFEX_NO_EXCEPTIONS

TEST(P0443, schedule_with_executor) {
  int i = 0;
  struct _receiver {
    int *p;
    void set_value() && noexcept {
      *p += 1;
    }
    void set_error(std::exception_ptr) && noexcept {
      *p += 2;
    }
    void set_done() && noexcept {
      *p += 4;
    }
  };
  submit(schedule(inline_executor{}), _receiver{&i});
  EXPECT_EQ(1, i);
}

TEST(P0443, Pipeable) {
  int i = 0;
  struct _receiver {
    int *p;
    void set_value() && noexcept {
      *p += 1;
    }
    void set_error(std::exception_ptr) && noexcept {
      *p += 2;
    }
    void set_done() && noexcept {
      *p += 4;
    }
  };
  schedule(inline_executor{})
    | submit(_receiver{&i});
  EXPECT_EQ(1, i);
}
