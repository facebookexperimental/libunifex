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

#if !UNIFEX_NO_COROUTINES

#  include <unifex/async_pass.hpp>
#  include <unifex/async_scope.hpp>
#  include <unifex/let_value.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/task.hpp>
#  include <unifex/then.hpp>
#  include <unifex/timed_single_thread_context.hpp>
#  include <unifex/when_any.hpp>

#  include <iostream>

namespace {
using namespace unifex;

timed_single_thread_context timer;

class CallingService {
public:
  void start() {
    scope_.detached_spawn_on(context_.get_scheduler(), service());
    scope_.detached_spawn_on(context_.get_scheduler(), agent());
  }

  auto shutdown() { return scope_.cleanup(); }

  // This is the simulated API intended to process UI event "Call". It is
  // synchronous and may be called repeatedly at an unpredictable rate, faster
  // than the underlying service can process.
  // The API guarantees that any new call will hang up an active one if any, and
  // replace any previously placed but not yet started call w/o letting the
  // service see it to avoid thrashing.
  void placeCall(std::string_view to);

private:
  task<void> service();
  task<void> agent();

  async_scope scope_;
  single_thread_context context_;
  async_pass<std::string> userCallRequest_;
  async_pass<std::string> agentCallRequest_;
  async_pass<> hangupRequest_;
};

task<void> CallingService::service() {
  using namespace std::chrono_literals;
  static constexpr auto kCallDuration{500ms};
  static constexpr auto kHangupDuration{100ms};

  for (;;) {
    // Service will only accept a new call when the previous one has been
    // finished and cleanup completed. The agentCallRequest_ provides an
    // essential guarantee to the agent() that the service has accepted the
    // call.
    auto to = co_await agentCallRequest_.async_accept();
    std::cout << "Calling " << to << std::endl;
    co_await when_any(
        // The hangupRequest_ could be a simple event, but the essential
        // requirement is that it is cancellable by normal call completion.
        // Unfortunately the manual_reset_async_event does not satisfy it.
        hangupRequest_.async_accept() | let_value([&to]() {
          std::cout << "Hung up on " << to << std::endl;
          return schedule_after(timer.get_scheduler(), kHangupDuration);
        }),
        schedule_after(timer.get_scheduler(), kCallDuration) | then([&to]() {
          std::cout << "Call with " << to << " ended" << std::endl;
        }));
  }
}

task<void> CallingService::agent() {
  std::string callee;
  for (;;) {
    // On each iteration of the loop, either
    // (1) callee is set to the value obtained from userCallRequest_ (possibly
    // replacing an old, unused, one), or
    // (2) callee value is successfully accepted by the service() and callee is
    // reset
    auto userRequest =
        // The userCallRequest_ can be replaced with a queue with placeCall()
        // serving as producer. This will not break the loop invariant.
        userCallRequest_.async_accept() | then([&callee](auto to) {
          if (auto previous = std::exchange(callee, std::move(to));
              !previous.empty()) {
            std::cout << "Cancelled earlier call to " << previous << std::endl;
          }
        });

    if (callee.empty()) {
      co_await std::move(userRequest);
    } else {
      co_await when_any(
          std::move(userRequest),
          agentCallRequest_.async_call(callee) |
              then([&callee]() { callee.clear(); }));
    }
  }
}

void CallingService::placeCall(std::string_view to) {
  std::cout << "Trying to call " << to << std::endl;

  if (hangupRequest_.try_call()) {
    std::cout << "Hanging up previously placed call" << std::endl;
  }

  // Agent responds immediately so sync_wait() blocks only for a short time
  // necessary to switch contexts. A queue can serve the same purpose in place
  // of async_pass.
  sync_wait(userCallRequest_.async_call(std::string{to}));
}

task<void> callClient(CallingService& service) {
  using namespace std::chrono_literals;
  service.placeCall("Alice");
  // cout> Trying to call Alice
  co_await schedule_after(timer.get_scheduler(), 300ms);
  // cout> Calling Alice
  service.placeCall("Bob");
  // cout> Trying to call Bob
  // cout> Hanging up previously placed call
  service.placeCall("Charlie");
  // cout> Trying to call Charlie
  // cout> Cancelled earlier call to Bob
  co_await schedule_after(timer.get_scheduler(), 600ms);
  // cout> Calling Charlie
}

}  // namespace

#endif

int main() {
#if !UNIFEX_NO_COROUTINES
  CallingService service;
  service.start();
  unifex::sync_wait(callClient(service));
  unifex::sync_wait(service.shutdown());
#endif
}
