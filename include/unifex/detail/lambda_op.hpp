/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#pragma once

#include <unifex/type_traits.hpp>

#include <type_traits>

namespace unifex::_lambda_op {

// Probe types for detecting event-dispatch lambdas.
// A lambda (auto event, auto* self) that branches on event.is_start /
// event.is_stop is callable with these probes.
struct _start_probe {
  static constexpr bool is_start = true;
  static constexpr bool is_stop = false;
};

struct _stop_probe {
  static constexpr bool is_start = false;
  static constexpr bool is_stop = true;
};

// Detection: a callable is event-dispatchable if it is not callable
// with zero arguments but IS callable with start/stop probes.
// The 2-arg check uses the probe types themselves as the self-pointer
// type (not void*) to avoid try_complete<void> instantiation failures
// when is_invocable_v triggers body instantiation for return-type
// deduction on generic lambdas.
template <typename T>
inline constexpr bool _is_event_dispatchable_v = !std::is_invocable_v<T&> &&
    (std::is_invocable_v<T&, _start_probe> ||
     std::is_invocable_v<T&, _start_probe, _start_probe*>) &&
    (std::is_invocable_v<T&, _stop_probe> ||
     std::is_invocable_v<T&, _stop_probe, _stop_probe*>);

// Unified operation state aggregate for callables returned by
// create_raw_sender factories. Wraps a callable with start() (and
// stop() for event-dispatch lambdas). Being an aggregate, supports
// non-moveable callables (e.g. lambdas capturing std::atomic) when
// constructed from a prvalue via guaranteed copy elision.
//
// The IsEventDispatch parameter is auto-deduced from the callable type.
template <
    typename Callable,
    bool IsEventDispatch = _is_event_dispatchable_v<Callable>>
struct _op;

// Plain callable: operator()() → start()
template <typename Callable>
struct _op<Callable, false> {
  UNIFEX_NO_UNIQUE_ADDRESS Callable callable_;
  void start() noexcept { callable_(); }
};

// Event-dispatch lambda: operator()(event[, self*]) → start() + stop()
template <typename Lambda>
struct _op<Lambda, true> {
  Lambda lambda_;

  void start() noexcept {
    if constexpr (std::is_invocable_v<Lambda&, _start_probe, _op*>) {
      lambda_(_start_probe{}, this);
    } else {
      lambda_(_start_probe{});
    }
  }

  void stop() noexcept {
    if constexpr (std::is_invocable_v<Lambda&, _stop_probe, _op*>) {
      lambda_(_stop_probe{}, this);
    } else {
      lambda_(_stop_probe{});
    }
  }
};

}  // namespace unifex::_lambda_op
