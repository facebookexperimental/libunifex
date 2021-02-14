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

#include <unifex/config.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/spin_wait.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/type_index.hpp>

#include <atomic>
#include <thread>
#include <cstdint>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

class inplace_stop_source;
class inplace_stop_token;
template <typename F>
class inplace_stop_callback;

class inplace_stop_callback_base {
 public:
  void execute() noexcept {
    this->execute_(this);
  }

#ifndef NDEBUG
  char const* type_name() const noexcept {
    return type_name_;
  }
#endif

 protected:
  using execute_fn = void(inplace_stop_callback_base* cb) noexcept;

#ifndef NDEBUG
  explicit inplace_stop_callback_base(inplace_stop_source* source, execute_fn* execute, char const* type_name) noexcept
      : source_(source), execute_(execute), type_name_(type_name) {}
#else
  explicit inplace_stop_callback_base(inplace_stop_source* source, execute_fn* execute) noexcept
      : source_(source), execute_(execute) {}
#endif

  void register_callback() noexcept;

  friend inplace_stop_source;

  inplace_stop_source* source_;
  execute_fn* execute_;
  inplace_stop_callback_base* next_ = nullptr;
  inplace_stop_callback_base** prevPtr_ = nullptr;
  bool* removedDuringCallback_ = nullptr;
  std::atomic<bool> callbackCompleted_{false};
#ifndef NDEBUG
  char const* type_name_ = nullptr;
#endif
};

class inplace_stop_source {
 public:
  inplace_stop_source() noexcept = default;

  ~inplace_stop_source();

  inplace_stop_source(const inplace_stop_source&) = delete;
  inplace_stop_source(inplace_stop_source&&) = delete;
  inplace_stop_source& operator=(inplace_stop_source&&) = delete;
  inplace_stop_source& operator=(const inplace_stop_source&) = delete;

  bool request_stop() noexcept;

  inplace_stop_token get_token() noexcept;

  bool stop_requested() const noexcept {
    return (state_.load(std::memory_order_acquire) & stop_requested_flag) != 0;
  }

 private:
  friend inplace_stop_token;
  friend inplace_stop_callback_base;
  template <typename F>
  friend class inplace_stop_callback;

  std::uint8_t lock() noexcept;
  void unlock(std::uint8_t oldState) noexcept;

  bool try_lock_unless_stop_requested(bool setStopRequested) noexcept;

  bool try_add_callback(inplace_stop_callback_base* callback) noexcept;

  void remove_callback(inplace_stop_callback_base* callback) noexcept;

  static constexpr std::uint8_t stop_requested_flag = 1;
  static constexpr std::uint8_t locked_flag = 2;

  std::atomic<std::uint8_t> state_{0};
  inplace_stop_callback_base* callbacks_ = nullptr;
  std::thread::id notifyingThreadId_;
};

class inplace_stop_token {
 public:
  template <typename F>
  using callback_type = inplace_stop_callback<F>;

  inplace_stop_token() noexcept : source_(nullptr) {}

  inplace_stop_token(const inplace_stop_token& other) noexcept = default;

  inplace_stop_token(inplace_stop_token&& other) noexcept
      : source_(std::exchange(other.source_, {})) {}

  inplace_stop_token& operator=(const inplace_stop_token& other) noexcept = default;

  inplace_stop_token& operator=(inplace_stop_token&& other) noexcept {
    source_ = std::exchange(other.source_, nullptr);
    return *this;
  }

  bool stop_requested() const noexcept {
    return source_ != nullptr && source_->stop_requested();
  }

  bool stop_possible() const noexcept {
    return source_ != nullptr;
  }

  void swap(inplace_stop_token& other) noexcept {
    std::swap(source_, other.source_);
  }

  friend bool operator==(const inplace_stop_token& a, const inplace_stop_token& b) noexcept {
    return a.source_ == b.source_;
  }

  friend bool operator!=(const inplace_stop_token& a, const inplace_stop_token& b) noexcept {
    return !(a == b);
  }

 private:
  friend inplace_stop_source;
  template <typename F>
  friend class inplace_stop_callback;

  explicit inplace_stop_token(inplace_stop_source* source) noexcept
      : source_(source) {}

  inplace_stop_source* source_;
};

inline inplace_stop_token inplace_stop_source::get_token() noexcept {
  return inplace_stop_token{this};
}

template <typename F>
class inplace_stop_callback final : private inplace_stop_callback_base {
 public:
  template(typename T)
    (requires convertible_to<T, F>)
  explicit inplace_stop_callback(inplace_stop_token token, T&& func) noexcept(
      is_nothrow_constructible_v<F, T>)
#ifndef NDEBUG
      : inplace_stop_callback_base(token.source_, &inplace_stop_callback::execute_impl, unifex::type_id<F>().name())
#else
      : inplace_stop_callback_base(token.source_, &inplace_stop_callback::execute_impl)
#endif
      , func_((T&&) func) {
    this->register_callback();
  }

  ~inplace_stop_callback() {
    if (source_ != nullptr) {
      source_->remove_callback(this);
    }
  }

 private:
  static void execute_impl(inplace_stop_callback_base* cb) noexcept {
    auto& self = *static_cast<inplace_stop_callback*>(cb);
    self.func_();
  }

  UNIFEX_NO_UNIQUE_ADDRESS F func_;
};

inline void inplace_stop_callback_base::register_callback() noexcept {
    if (source_ != nullptr) {
      if (!source_->try_add_callback(this)) {
        source_ = nullptr;
        // Callback not registered because stop_requested() was true.
        // Execute inline here.
        execute();
      }
    }
}

namespace detail {
  struct forward_stop_request_to_inplace_stop_source {
    inplace_stop_source& source;

    forward_stop_request_to_inplace_stop_source(inplace_stop_source& s) noexcept
      : source(s) {}

    void operator()() const noexcept {
      source.request_stop();
    }
  };
} // namespace detail

// Helper class for adapting an incoming StopToken type to an
// inplace_stop_token.
template<typename StopToken, typename = void>
class inplace_stop_token_adapter {
public:
  inplace_stop_token subscribe(StopToken stoken) noexcept {
    const bool stopPossible = stoken.stop_possible();
    callback_.construct(std::move(stoken), source_);
    return stopPossible ? source_.get_token() : inplace_stop_token{};
  }

  void unsubscribe() noexcept {
    callback_.destruct();
  }

private:
  using stop_callback = typename StopToken::template callback_type<detail::forward_stop_request_to_inplace_stop_source>;
  inplace_stop_source source_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<stop_callback> callback_;
};

template<>
class inplace_stop_token_adapter<inplace_stop_token, void> {
public:
  inplace_stop_token subscribe(inplace_stop_token stoken) noexcept {
    return stoken;
  }

  void unsubscribe() noexcept {}
};

template<typename StopToken>
class inplace_stop_token_adapter<StopToken, std::enable_if_t<is_stop_never_possible_v<StopToken>>> {
public:
  inplace_stop_token subscribe(StopToken) noexcept {
    return inplace_stop_token{};
  }

  void unsubscribe() noexcept {}
};

/// \cond
namespace detail {
template <typename StopToken>
struct inplace_stop_token_adapter_subscription {
  inplace_stop_token subscribe(StopToken stoken) noexcept {
    isSubscribed_ = true;
    return stopTokenAdapter_.subscribe(std::move(stoken));
  }
  void unsubscribe() noexcept {
    if (isSubscribed_) {
      isSubscribed_ = false;
      stopTokenAdapter_.unsubscribe();
    }
  }
  ~inplace_stop_token_adapter_subscription() {
    unsubscribe();
  }
private:
  bool isSubscribed_ = false;
  UNIFEX_NO_UNIQUE_ADDRESS
  inplace_stop_token_adapter<StopToken> stopTokenAdapter_{};
};
} // namespace detail
/// \endcond

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
