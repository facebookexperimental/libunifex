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

#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>
#include <unifex/with_query_value.hpp>

#include <chrono>

using namespace unifex;
using namespace std::chrono_literals;

namespace {

class delegating_scheduler;

class delegating_context {
  public:
  void run() {
    task_count_++;
  }

  int get_count() {
    return task_count_;
  }

  delegating_scheduler get_scheduler() noexcept;

  private:
  std::atomic<int> task_count_{0};
};

template <typename OperationState>
class delegating_operation final {
  public:
  inline void start() noexcept {
    std::printf("start()\n");
    context_->run();
    target_op_.start();
  }

  OperationState target_op_;
  delegating_context* context_ = nullptr;
};

class delegating_sender {
  public:
  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  template <typename Receiver>
  auto connect(Receiver&& receiver) {
    auto target_scheduler = unifex::get_scheduler(std::as_const(receiver));
    auto target_op = unifex::connect(unifex::schedule(target_scheduler), (Receiver&&)receiver);
    std::printf("connect\n");
    return delegating_operation<
      remove_cvref_t<decltype(target_op)>>{
        std::move(target_op), context_};
  }

  delegating_context* context_ = nullptr;
};

class delegating_scheduler {
  public:
  auto schedule() const noexcept {
    return delegating_sender{context_};
  }

  delegating_context* context_ = nullptr;
};

delegating_scheduler delegating_context::get_scheduler() noexcept {
  return delegating_scheduler{this};
}
} // namespace

int main() {
  timed_single_thread_context ctx;
  delegating_context delegating_ctx;

  // Check that the schedule() operation can pick up the current
  // scheduler from the receiver which we inject by using 'with_query_value()'.
  sync_wait(with_query_value(schedule(), get_scheduler,
                             ctx.get_scheduler()));

  // Check that the schedule_after(d) operation can pick up the current
  // scheduler from the receiver.
  sync_wait(with_query_value(
      schedule_after(200ms), get_scheduler, ctx.get_scheduler()));

  // Check that this can propagate through multiple levels of
  // composed operations.
  sync_wait(with_query_value(
      transform(
          for_each(via_stream(delegating_ctx.get_scheduler(),
                              transform_stream(range_stream{0, 10},
                                               [](int value) {
                                                 return value * value;
                                               })),
                   [](int value) { std::printf("got %i\n", value); }),
          []() { std::printf("done\n"); }),
      get_scheduler, ctx.get_scheduler()));

  std::printf("delegating_ctx operations: %d\n", delegating_ctx.get_count());

  return 0;
}
