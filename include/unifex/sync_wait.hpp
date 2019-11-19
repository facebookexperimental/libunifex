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
#pragma once

#include <unifex/manual_lifetime.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>
#include <optional>
#include <cassert>

namespace unifex {

namespace detail {
template <typename T>
struct sync_wait_promise {
  sync_wait_promise() {}

  ~sync_wait_promise() {
    if (state_ == state::value) {
      value_.destruct();
    } else if (state_ == state::error) {
      exception_.destruct();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  union {
    manual_lifetime<T> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };

  enum class state { incomplete, done, value, error };
  state state_ = state::incomplete;
};

template <typename T, typename StopToken>
struct sync_wait_receiver {
  sync_wait_promise<T>& promise_;
  StopToken stopToken_;

  template <typename... Values>
      void value(Values&&... values) && noexcept {
    std::lock_guard lock{promise_.mutex_};
    try {
      promise_.value_.construct((Values &&) values...);
      promise_.state_ = sync_wait_promise<T>::state::value;
    } catch (...) {
      promise_.exception_.construct(std::current_exception());
      promise_.state_ = sync_wait_promise<T>::state::error;
    }
    promise_.cv_.notify_one();
  }

  void error(std::exception_ptr err) && noexcept {
    std::lock_guard lock{promise_.mutex_};
    promise_.exception_.construct(std::move(err));
    promise_.state_ = sync_wait_promise<T>::state::error;
    promise_.cv_.notify_one();
  }

  template <typename Error>
      void error(Error&& e) && noexcept {
    std::move(*this).error(std::make_exception_ptr((Error &&) e));
  }

  void done() && noexcept {
    std::lock_guard lock{promise_.mutex_};
    promise_.state_ = sync_wait_promise<T>::state::done;
    promise_.cv_.notify_one();
  }

  friend const StopToken& tag_invoke(
      tag_t<get_stop_token>, const sync_wait_receiver& r) noexcept {
    return r.stopToken_;
  }
};

template<typename T>
struct thread_unsafe_sync_wait_promise {
  thread_unsafe_sync_wait_promise() noexcept {}

  ~thread_unsafe_sync_wait_promise() {
    if (state_ == state::value) {
      value_.destruct();
    } else if (state_ == state::error) {
      exception_.destruct();
    }
  }

  union {
    manual_lifetime<T> value_;
    manual_lifetime<std::exception_ptr> exception_;
  };

  enum class state { incomplete, done, value, error };
  state state_ = state::incomplete;
};

template<typename T, typename StopToken>
struct thread_unsafe_sync_wait_receiver {
  thread_unsafe_sync_wait_promise<T>& promise_;
  StopToken stopToken_;

  template <typename... Values>
  void value(Values&&... values) && noexcept {
    try {
      promise_.value_.construct((Values &&) values...);
      promise_.state_ = thread_unsafe_sync_wait_promise<T>::state::value;
    } catch (...) {
      promise_.exception_.construct(std::current_exception());
      promise_.state_ = thread_unsafe_sync_wait_promise<T>::state::error;
    }
  }

  void error(std::exception_ptr err) && noexcept {
    promise_.exception_.construct(std::move(err));
    promise_.state_ = thread_unsafe_sync_wait_promise<T>::state::error;
  }

  template <typename Error>
  void error(Error&& e) && noexcept {
    std::move(*this).error(std::make_exception_ptr((Error &&) e));
  }

  void done() && noexcept {
    promise_.state_ = thread_unsafe_sync_wait_promise<T>::state::done;
  }

  friend const StopToken& tag_invoke(
      tag_t<get_stop_token>, const thread_unsafe_sync_wait_receiver& r) noexcept {
    return r.stopToken_;
  }
};

} // namespace detail

template <
    typename Sender,
    typename StopToken = unstoppable_token,
    typename Result = single_value_result_t<std::remove_cvref_t<Sender>>>
auto sync_wait(Sender&& sender, StopToken&& stopToken = {})
    -> std::optional<Result> {
  auto blockingResult = blocking(sender);
  if (blockingResult == blocking_kind::always ||
      blockingResult == blocking_kind::always_inline) {
    using promise_t = detail::thread_unsafe_sync_wait_promise<Result>;
    promise_t promise;

    auto operation = connect(
      (Sender&&)sender,
      detail::thread_unsafe_sync_wait_receiver<Result, StopToken&&>{
        promise, (StopToken&&)stopToken});

    start(operation);

    assert(promise.state_ != promise_t::state::incomplete);

    switch (promise.state_) {
      case promise_t::state::done:
        return std::nullopt;
      case promise_t::state::value:
        return std::move(promise.value_).get();
      case promise_t::state::error:
        std::rethrow_exception(promise.exception_.get());
      default:
        std::terminate();
    }
  } else {
    using promise_t = detail::sync_wait_promise<Result>;
    promise_t promise;

    // Store state for the operation on the stack.
    auto operation = connect(
        ((Sender &&) sender),
        detail::sync_wait_receiver<Result, StopToken&&>{
          promise, (StopToken&&)stopToken});

    start(operation);

    std::unique_lock lock{promise.mutex_};
    promise.cv_.wait(
        lock, [&] { return promise.state_ != promise_t::state::incomplete; });

    switch (promise.state_) {
      case promise_t::state::done:
        return std::nullopt;
      case promise_t::state::value:
        return std::move(promise.value_).get();
      case promise_t::state::error:
        std::rethrow_exception(promise.exception_.get());
      default:
        std::terminate();
    }
  }
}

template <
  typename Result,
  typename Sender,
  typename StopToken = unstoppable_token>
decltype(auto) sync_wait_r(Sender&& sender, StopToken&& stopToken = {}) {
  return sync_wait<
    Sender, StopToken, non_void_t<wrap_reference_t<decay_rvalue_t<Result>>>>(
      (Sender&&)sender, (StopToken&&)stopToken);
}

} // namespace unifex
