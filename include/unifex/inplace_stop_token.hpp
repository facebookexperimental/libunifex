#pragma once

#include <unifex/config.hpp>
#include <unifex/spin_wait.hpp>

#include <atomic>
#include <cassert>
#include <thread>
#include <cstdint>
#include <type_traits>

namespace unifex {

class inplace_stop_source;
class inplace_stop_token;
template <typename F>
class inplace_stop_callback;

class inplace_stop_callback_base {
 public:
  virtual void execute() noexcept = 0;

 protected:
  explicit inplace_stop_callback_base(inplace_stop_source* source)
      : source_(source) {}

  friend inplace_stop_source;

  inplace_stop_source* source_;
  inplace_stop_callback_base* next_ = nullptr;
  inplace_stop_callback_base** prevPtr_ = nullptr;
  bool* removedDuringCallback_ = nullptr;
  std::atomic<bool> callbackCompleted_{false};
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

  inplace_stop_token(inplace_stop_token&& other) noexcept
      : source_(std::exchange(other.source_, {})) {}

  inplace_stop_token(const inplace_stop_token& other) noexcept
      : source_(other.source_) {}

  bool stop_requested() const noexcept {
    return source_ != nullptr && source_->stop_requested();
  }

  bool stop_possible() const noexcept {
    return source_ != nullptr;
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
  explicit inplace_stop_callback(inplace_stop_token token, F&& func) noexcept(
      std::is_nothrow_move_constructible_v<F>)
      : inplace_stop_callback_base(token.source_), func_((F &&) func) {
    if (source_ != nullptr) {
      if (!source_->try_add_callback(this)) {
        source_ = nullptr;
        // Callback not registered because stop_requested() was true.
        // Execute inline here.
        execute();
      }
    }
  }

  ~inplace_stop_callback() {
    if (source_ != nullptr) {
      source_->remove_callback(this);
    }
  }

  void execute() noexcept final {
    func_();
  }

 private:
  UNIFEX_NO_UNIQUE_ADDRESS F func_;
};

} // namespace unifex
