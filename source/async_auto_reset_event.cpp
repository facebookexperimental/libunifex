// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <unifex/async_auto_reset_event.hpp>

namespace unifex::_aare {

void async_auto_reset_event::set() noexcept {
  std::lock_guard lock{mutex_};

  if (state_ != state::DONE) {
    state_ = state::SET;
    event_.set();
  }
}

void async_auto_reset_event::set_done() noexcept {
  std::lock_guard lock{mutex_};

  state_ = state::DONE;
  event_.set();
}

bool async_auto_reset_event::try_reset() noexcept {
  std::lock_guard lock{mutex_};

  UNIFEX_ASSERT(event_.ready());

  if (state_ == state::SET) {
    state_ = state::UNSET;
    event_.reset();
    return true;
  } else {
    UNIFEX_ASSERT(state_ == state::DONE);
    return false;
  }
}

}  // namespace unifex::_aare
