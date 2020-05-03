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
  delegating_context(int capacity) : capacity_{capacity} {}

  bool reserve() {
    auto lck = std::scoped_lock{m_};
    bool reservation_made = reservation_count_ < capacity_;
    if(reservation_made) {
      ++reservation_count_;
    }
    return reservation_made;
  }

  int get_count() {
    return run_count_;
  }

  void run() {
    run_count_++;
  }

  delegating_scheduler get_scheduler() noexcept;

  private:
  friend class delegating_sender;
  std::mutex m_;
  int reservation_count_{0};
  std::atomic<int> run_count_{0};
  int capacity_;
  timed_single_thread_context single_thread_context_;
};

template <typename DelegatedOperationState, typename LocalOperationState>
class delegating_operation final {
  public:
  inline void start() noexcept {
    if(target_op_) {
      // Start a delegated operation
      target_op_->start();
    }
    if(local_op_) {
      // Start immediately on the local context
      context_->run();
      local_op_->start();
    }
  }

  std::optional<DelegatedOperationState> target_op_;
  std::optional<LocalOperationState> local_op_;
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
    // Attempt to reserve a slot otherwise delegate to the downstream scheduler
    using op = delegating_operation<
        remove_cvref_t<decltype(unifex::connect(unifex::schedule(unifex::get_scheduler(std::as_const(receiver))), (Receiver&&)receiver))>,
        remove_cvref_t<decltype(unifex::connect(unifex::schedule(context_->single_thread_context_.get_scheduler()), (Receiver&&)receiver))>>;
    if(context_->reserve()) {
      auto local_op = unifex::connect(unifex::schedule(context_->single_thread_context_.get_scheduler()), (Receiver&&)receiver);
      return op{std::nullopt, std::move(local_op), context_};
    }

    auto target_scheduler = unifex::get_scheduler(std::as_const(receiver));
    auto target_op = unifex::connect(unifex::schedule(target_scheduler), (Receiver&&)receiver);
    return op{std::move(target_op), std::nullopt, context_};
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
  delegating_context inner_delegating_ctx{2};
  delegating_context outer_delegating_ctx{3};

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
          for_each(via_stream(outer_delegating_ctx.get_scheduler(),
                              transform_stream(via_stream(inner_delegating_ctx.get_scheduler(),
                                                transform_stream(range_stream{0, 10},
                                                                [](int value) {
                                                                  return value + 1;
                                                                })),
                                               [](int value) {
                                                 return value * value;
                                               })),
                   [](int value) { std::printf("got %i\n", value); }),
          []() { std::printf("done\n"); }),
      get_scheduler, ctx.get_scheduler()));

  std::printf("inner_delegating_ctx operations: %d\n", inner_delegating_ctx.get_count());
  std::printf("outer_delegating_ctx operations: %d\n", outer_delegating_ctx.get_count());

  return 0;
}
