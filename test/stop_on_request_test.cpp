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
#include <unifex/stop_when.hpp>

#include <unifex/defer.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/on.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/stop_on_request.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/when_all_range.hpp>

#include <array>
#include <exception>
#include <optional>

#include <gtest/gtest.h>

TEST(StopOnRequest, MultiThreadedCancellations) {
  constexpr size_t iterations = 10;
  constexpr size_t numSources = 5;
  for (size_t i = 0; i < iterations; i++) {
    std::array<unifex::single_thread_context, numSources> threads{};
    std::array<unifex::inplace_stop_source, numSources> stopSources{};
    auto makeTask = [&](size_t index) noexcept {
      auto& thread = threads[index];
      auto* stopSource = &stopSources[index];
      return unifex::on(
          thread.get_scheduler(), unifex::just_from([stopSource]() noexcept {
            stopSource->request_stop();
          }));
    };
    std::vector<decltype(makeTask(0))> tasks;
    for (size_t j = 0; j < numSources; j++) {
      tasks.push_back(makeTask(j));
    }
    bool wasCancelled = false;
    auto cancellationSender = std::apply(
        [&](auto&... stopSources) noexcept {
          return unifex::stop_on_request(stopSources.get_token()...) |
              unifex::let_done([&]() {
                   wasCancelled = true;
                   return unifex::just();
                 });
        },
        stopSources);
    unifex::sync_wait(unifex::when_all(
        unifex::when_all_range(std::move(tasks)),
        std::move(cancellationSender)));
    EXPECT_TRUE(wasCancelled);
  }
}

// Dummy stop source and token class to test callback construction exception
// handling
namespace {
template <typename test_stop_token, typename F>
struct test_stop_callback final {
  explicit test_stop_callback(test_stop_token, F&&) { throw std::exception(); }
};

struct test_stop_token {
  template <typename F>
  using callback_type = test_stop_callback<test_stop_token, F>;
};
}  // namespace

TEST(StopOnRequest, UnstoppableReceiverWithExternalStopSource) {
  bool wasCancelled = false;
  unifex::inplace_stop_source externalStopSource;

  unifex::sync_wait(unifex::when_all(
      unifex::stop_on_request(externalStopSource.get_token()) |
          unifex::let_done([&]() {
            wasCancelled = true;
            return unifex::just();
          }),
      unifex::defer([&]() {
        externalStopSource.request_stop();
        return unifex::just();
      })));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, NoExternalStopSourceCancelledByReceiver) {
  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    return unifex::when_all(
        unifex::stop_on_request() | unifex::let_done([&]() {
          wasCancelled = true;
          return unifex::just();
        }),
        unifex::defer([&]() {
          stopSource.request_stop();
          return unifex::just();
        }));
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, SingleExternalStopSourceCancelledBySource) {
  unifex::inplace_stop_source externalStopSource;

  bool wasCancelled = false;
  unifex::sync_wait(unifex::when_all(
      unifex::stop_on_request(externalStopSource.get_token()) |
          unifex::let_done([&]() {
            wasCancelled = true;
            return unifex::just();
          }),
      unifex::defer([&]() {
        externalStopSource.request_stop();
        return unifex::just();
      })));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, SingleStopSourceCancelledByReceiver) {
  unifex::inplace_stop_source externalStopSource;

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    return unifex::when_all(
        unifex::stop_on_request(externalStopSource.get_token()) |
            unifex::let_done([&]() {
              wasCancelled = true;
              return unifex::just();
            }),
        unifex::defer([&]() {
          stopSource.request_stop();
          return unifex::just();
        }));
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, SingleStopSourceCancelledBySourceAndReceiver) {
  unifex::inplace_stop_source externalStopSource;

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    return unifex::when_all(
        unifex::stop_on_request(externalStopSource.get_token()) |
            unifex::let_done([&]() {
              wasCancelled = true;
              return unifex::just();
            }),
        unifex::defer([&]() {
          stopSource.request_stop();
          externalStopSource.request_stop();
          return unifex::just();
        }));
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, ReceiverCancelledBeforeConstruction) {
  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    stopSource.request_stop();
    return unifex::stop_on_request() | unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, StopSourceCancelledBeforeConstruction) {
  unifex::inplace_stop_source externalStopSource;
  externalStopSource.request_stop();

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto&) {
    return unifex::stop_on_request(externalStopSource.get_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, SingleExternalStopSourceCancellationBeforeConstruction) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;
  externalStopSource1.request_stop();

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto&) {
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));
  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, MultipleExternalStopSourceCancellationsBeforeConstruction) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;
  unifex::inplace_stop_source externalStopSource3;
  externalStopSource2.request_stop();
  externalStopSource3.request_stop();

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto&) {
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token(),
               externalStopSource3.get_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));
  EXPECT_TRUE(wasCancelled);
}

TEST(
    StopOnRequest,
    ReceiverCancellationWithMultipleExternalStopSourcesBeforeConstruction) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    stopSource.request_stop();
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));
  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, ReceiverAndStopSourceCancellationsBeforeConstruction) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;
  externalStopSource1.request_stop();

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    stopSource.request_stop();
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));
  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, StopAfterComplete) {
  unifex::inplace_stop_source externalStopSource;

  bool wasCancelled = false;

  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    return unifex::when_all(
        unifex::stop_on_request(externalStopSource.get_token()) |
            unifex::let_done([&]() {
              wasCancelled = true;
              return unifex::just();
            }),
        unifex::defer([&]() {
          stopSource.request_stop();
          return unifex::just();
        }));
  }));

  externalStopSource.request_stop();

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, SingleCallbackConstructionErrorHandling) {
  EXPECT_THROW(
      { unifex::sync_wait(unifex::stop_on_request(test_stop_token())); },
      std::exception);
}

TEST(StopOnRequest, MultipleCallbackConstructionErrorHandling_First) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;

  EXPECT_THROW(
      {
        unifex::sync_wait(unifex::stop_on_request(
            test_stop_token(),
            externalStopSource1.get_token(),
            externalStopSource2.get_token()));
      },
      std::exception);
}

TEST(StopOnRequest, MultipleCallbackConstructionErrorHandling_Last) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;

  EXPECT_THROW(
      {
        unifex::sync_wait(unifex::stop_on_request(
            externalStopSource1.get_token(),
            externalStopSource2.get_token(),
            test_stop_token()));
      },
      std::exception);
}

TEST(StopOnRequest, MultipleCallbackConstructionErrorsHandling) {
  unifex::inplace_stop_source externalStopSource;

  EXPECT_THROW(
      {
        unifex::sync_wait(unifex::stop_on_request(
            externalStopSource.get_token(),
            test_stop_token(),
            test_stop_token()));
      },
      std::exception);
}

TEST(StopOnRequest, StopSourceCancellationBeforeCallbackConstructionError) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;

  externalStopSource2.request_stop();

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto&) {
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token(),
               test_stop_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));

  EXPECT_TRUE(wasCancelled);
}

TEST(StopOnRequest, ReceiverCancellationBeforeCallbackConstructionError) {
  unifex::inplace_stop_source externalStopSource1;
  unifex::inplace_stop_source externalStopSource2;

  bool wasCancelled = false;
  unifex::sync_wait(unifex::let_value_with_stop_source([&](auto& stopSource) {
    stopSource.request_stop();
    return unifex::stop_on_request(
               externalStopSource1.get_token(),
               externalStopSource2.get_token(),
               test_stop_token()) |
        unifex::let_done([&]() {
             wasCancelled = true;
             return unifex::just();
           });
  }));

  EXPECT_TRUE(wasCancelled);
}
