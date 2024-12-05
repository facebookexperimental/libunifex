/*
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 
 */
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
