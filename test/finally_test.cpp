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
#include <unifex/finally.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/transform.hpp>

#include <cstdio>
#include <thread>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Finally, Pipeable) {
  timed_single_thread_context context;

  schedule(context.get_scheduler())
    | finally(schedule(context.get_scheduler()) 
        | transform([](){ std::printf("finally\n"); }))
    | transform([](){ std::printf("transform\n"); })
    | sync_wait();
}
