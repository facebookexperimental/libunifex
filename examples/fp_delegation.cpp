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

#include <unifex/for_each.hpp>
#include <unifex/manual_event_loop.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>
#include <unifex/with_query_value.hpp>

#include <atomic>
#include <chrono>
#include <variant>

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

template<typename T>
struct FuncWrapper {
  T func_;
  operator std::invoke_result_t<T>() {
    return func_();
  }
};

template <typename DelegatedOperationState, typename LocalOperationState>
class delegating_operation final {
  public:
  template<class InitFunc>
  delegating_operation(InitFunc&& func, delegating_context* context) :
    op_{
      std::in_place_type_t<remove_cvref_t<decltype(func())>>{},
      FuncWrapper<InitFunc>{std::forward<InitFunc>(func)}},
    context_{context} {
  }


  inline void start() noexcept {
    if(std::holds_alternative<DelegatedOperationState>(op_)) {
      // Start a delegated operation
      std::get<DelegatedOperationState>(op_).start();
    } else {
      // Start immediately on the local context
      context_->run();
      std::get<LocalOperationState>(op_).start();;
    }
  }

  std::variant<DelegatedOperationState, LocalOperationState> op_;
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

  static constexpr bool sends_done = true;

  template<typename OpType>
  struct LocalContextType {
    OpType op_;

    inline void start() noexcept {
      op_.start();
    }
  };

  using SchedSender =
      schedule_result_t<
          decltype(UNIFEX_DECLVAL(timed_single_thread_context&).get_scheduler())>;

  template <typename Receiver>
  using operation =
      delegating_operation<
          connect_result_t<
              schedule_result_t<get_scheduler_result_t<Receiver>>,
              Receiver>,
          LocalContextType<connect_result_t<SchedSender, Receiver>>>;

  template <typename Receiver>
  operation<Receiver> connect(Receiver&& receiver) {
    // Attempt to reserve a slot otherwise delegate to the downstream scheduler
    using LC = LocalContextType<connect_result_t<SchedSender, Receiver>>;
    using op = operation<Receiver>;

    if(context_->reserve()) {
      auto local_op = [&receiver, context = context_]() mutable {
        return LC{unifex::connect(
          unifex::schedule(context->single_thread_context_.get_scheduler()),
          (Receiver&&)receiver)};};
      return op{std::move(local_op), context_};
    }

    auto target_op = [&receiver]() mutable {
      return unifex::connect(
        unifex::schedule(
          unifex::get_scheduler(std::as_const(receiver))),
          (Receiver&&)receiver);
    };

    return op{std::move(target_op), context_};
  }

  delegating_context* context_ = nullptr;
};

class delegating_scheduler {
  public:
  auto schedule() const noexcept {
    return delegating_sender{context_};
  }

  [[maybe_unused]] friend bool operator==(delegating_scheduler, delegating_scheduler) noexcept {
    return true;
  }
  [[maybe_unused]] friend bool operator!=(delegating_scheduler, delegating_scheduler) noexcept {
    return false;
  }

  delegating_context* context_ = nullptr;
};

delegating_scheduler delegating_context::get_scheduler() noexcept {
  return delegating_scheduler{this};
}
} // namespace

int main() {
  delegating_context inner_delegating_ctx{2};
  delegating_context outer_delegating_ctx{3};

  // Try inner context, then outer context, delegating to ctx if necessary
  sync_wait(
      then(
          for_each(
              via_stream(
                  outer_delegating_ctx.get_scheduler(),
                  transform_stream(
                      via_stream(
                          inner_delegating_ctx.get_scheduler(),
                          transform_stream(
                              range_stream{0, 10},
                              [](int value) { return value + 1; })),
                          [](int value) { return value * value; })),
              [](int value) { std::printf("got %i\n", value); }),
          []() { std::printf("done\n"); }));

  std::printf("inner_delegating_ctx operations: %d\n", inner_delegating_ctx.get_count());
  std::printf("outer_delegating_ctx operations: %d\n", outer_delegating_ctx.get_count());

  return 0;
}
