/*
 * Copyright (c) Rishabh Dwivedi <rishabhdwivedi17@gmail.com>
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

#include <unifex/get_completion_scheduler.hpp>
#include <unifex/new_thread_context.hpp>
#include <unifex/type_traits.hpp>
#include "unifex/receiver_concepts.hpp"

#include <gtest/gtest.h>

TEST(GetCompletionScheduler, NewThreadScheduler) {
  unifex::new_thread_context ctx;
  auto sch = ctx.get_scheduler();
  auto sender = unifex::schedule(sch);
  static_assert(unifex::same_as<decltype(sch), decltype(unifex::get_completion_scheduler<decltype(unifex::set_value)>(sender))>);
  static_assert(unifex::same_as<decltype(sch), decltype(unifex::get_completion_scheduler<decltype(unifex::set_error)>(sender))>);
  static_assert(unifex::same_as<decltype(sch), decltype(unifex::get_completion_scheduler<decltype(unifex::set_done)>(sender))>);
}
