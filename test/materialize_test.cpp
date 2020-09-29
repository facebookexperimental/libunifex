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

#include <unifex/when_all.hpp>
#include <unifex/just_error.hpp>
#include <unifex/materialize.hpp>
#include <unifex/dematerialize.hpp>
#include <unifex/transform.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <cassert>
#include <optional>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Materialize, Smoke) {
    single_thread_context ctx;

    std::optional<int> result = sync_wait(
        dematerialize(
            materialize(
                transform(
                    schedule(ctx.get_scheduler()),
                    []() { return 42; }))));

    EXPECT_TRUE(!!result);
    EXPECT_EQ(result.value(), 42);
}

TEST(Materialize, Pipeable) {
    single_thread_context ctx;

    std::optional<int> result = schedule(ctx.get_scheduler())
      | transform(
        []() { return 42; })
      | materialize()
      | dematerialize()
      | sync_wait();

    EXPECT_TRUE(!!result);
    EXPECT_EQ(result.value(), 42);
}

TEST(Materialize, Failure) {
    std::string what;
    try {
      std::optional<int> result = just_error(std::make_exception_ptr(std::runtime_error{"failure"}))
        | transform(
          []() { return 42; })
        | materialize()
        | dematerialize()
        | sync_wait();
    } catch(const std::runtime_error& ex) { what = ex.what(); }

    EXPECT_EQ(what, "failure");
}
