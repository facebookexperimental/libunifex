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
#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <unifex/task.hpp>
#include <unifex/at_coroutine_exit.hpp>

namespace unifex::_task {
void _promise_base::transform_schedule_sender_impl_(any_scheduler_ref newSched) {
  // If we haven't already inserted a cleanup action to take us back to the correct
  // scheduler, do so now:
  if (!std::exchange(this->rescheduled_, true)) {
    // Create a cleanup action that transitions back onto the current scheduler:
    auto cleanupTask = at_coroutine_exit(schedule, this->sched_);
    // Insert the cleanup action into the head of the continuation chain by making
    // direct calls to the cleanup task's awaiter member functions. See type
    // _cleanup_task in at_coroutine_exit.hpp:
    cleanupTask.await_suspend_impl_(*this);
    (void) cleanupTask.await_resume();
  }

  // Update the current scheduler. (Don't do this before we have inserted the
  // cleanup action because the insertion of the cleanup action reads this task's
  // current scheduler.)
  this->sched_ = newSched;
}
} // unifex::_task

#endif
