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
#include <unifex/blocking.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>
#include <optional>
#include <cassert>

namespace unifex {
namespace _sync_wait {

template <typename T>
struct promise {
  promise() {}

  ~promise() {
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

template <typename T>
struct _receiver {
  struct type {
    using receiver = type;

    promise<T>& promise_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      std::lock_guard lock{ promise_.mutex_ };
      try {
        promise_.value_.construct((Values&&)values...);
        promise_.state_ = promise<T>::state::value;
      }
      catch (...) {
        promise_.exception_.construct(std::current_exception());
        promise_.state_ = promise<T>::state::error;
      }
      promise_.cv_.notify_one();
    }

    void set_error(std::exception_ptr err) && noexcept {
      std::lock_guard lock{ promise_.mutex_ };
      promise_.exception_.construct(std::move(err));
      promise_.state_ = promise<T>::state::error;
      promise_.cv_.notify_one();
    }

    template <typename Error>
    void set_error(Error&& e) && noexcept {
      std::move(*this).set_error(std::make_exception_ptr((Error&&)e));
    }

    void set_done() && noexcept {
      std::lock_guard lock{ promise_.mutex_ };
      promise_.state_ = promise<T>::state::done;
      promise_.cv_.notify_one();
    }
  };
};

template <typename T>
using receiver = typename _receiver<T>::type;

template<typename T>
struct thread_unsafe_promise {
  thread_unsafe_promise() noexcept {}

  ~thread_unsafe_promise() {
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

template<typename T>
struct _thread_unsafe_receiver {
  struct type {
    using thread_unsafe_receiver = type;

    thread_unsafe_promise<T>& promise_;

    template <typename... Values>
    void set_value(Values&&... values) && noexcept {
      try {
        promise_.value_.construct((Values&&)values...);
        promise_.state_ = thread_unsafe_promise<T>::state::value;
      }
      catch (...) {
        promise_.exception_.construct(std::current_exception());
        promise_.state_ = thread_unsafe_promise<T>::state::error;
      }
    }

    void set_error(std::exception_ptr err) && noexcept {
      promise_.exception_.construct(std::move(err));
      promise_.state_ = thread_unsafe_promise<T>::state::error;
    }

    template <typename Error>
    void set_error(Error&& e) && noexcept {
      std::move(*this).set_error(std::make_exception_ptr((Error&&)e));
    }

    void set_done() && noexcept {
      promise_.state_ = thread_unsafe_promise<T>::state::done;
    }
  };
};

template<typename T>
using thread_unsafe_receiver = typename _thread_unsafe_receiver<T>::type;

} // namespace _sync_wait

namespace _sync_wait_cpo {
  struct _fn {
    template <
        typename Sender,
        typename Result = single_value_result_t<std::remove_cvref_t<Sender>>>
    auto operator()(Sender&& sender) const
        -> std::optional<Result> {
      auto blockingResult = blocking(sender);
      if (blockingResult == blocking_kind::always ||
          blockingResult == blocking_kind::always_inline) {
        using promise_t = _sync_wait::thread_unsafe_promise<Result>;
        promise_t promise;

        auto operation = connect(
          (Sender&&)sender,
          _sync_wait::thread_unsafe_receiver<Result>{promise});

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
        using promise_t = _sync_wait::promise<Result>;
        promise_t promise;

        // Store state for the operation on the stack.
        auto operation = connect(
            ((Sender &&) sender),
            _sync_wait::receiver<Result>{promise});

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
  };
} // namespace _sync_wait_cpo

inline constexpr _sync_wait_cpo::_fn sync_wait {};

namespace _sync_wait_r_cpo {
  template <typename Result>
  struct _fn {
    template <typename Sender>
    decltype(auto) operator()(Sender&& sender) const {
      return sync_wait.operator()<
        Sender, non_void_t<wrap_reference_t<decay_rvalue_t<Result>>>>(
          (Sender&&)sender);
    }
  };
} // namespace _sync_wait_r_cpo

template <typename Result>
inline constexpr _sync_wait_r_cpo::_fn<Result> sync_wait_r {};

} // namespace unifex
