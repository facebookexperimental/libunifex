/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/when_all.hpp>
#include <unifex/just_error.hpp>
#include <unifex/materialize.hpp>
#include <unifex/dematerialize.hpp>
#include <unifex/then.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <optional>

#include <gtest/gtest.h>

using namespace unifex;

TEST(Materialize, Smoke) {
    single_thread_context ctx;

    std::optional<int> result = sync_wait(
        dematerialize(
            materialize(
                then(
                    schedule(ctx.get_scheduler()),
                    []() { return 42; }))));

    EXPECT_TRUE(!!result);
    EXPECT_EQ(result.value(), 42);
}

TEST(Materialize, Pipeable) {
    single_thread_context ctx;

    std::optional<int> result = schedule(ctx.get_scheduler())
      | then(
        []() { return 42; })
      | materialize()
      | dematerialize()
      | sync_wait();

    EXPECT_TRUE(!!result);
    EXPECT_EQ(result.value(), 42);
}

#if !UNIFEX_NO_EXCEPTIONS
TEST(Materialize, Failure) {
    EXPECT_THROW({
      try {
        std::optional<unit> result = just_error(std::make_exception_ptr(std::runtime_error{"failure"}))
          | materialize()
          | dematerialize()
          | sync_wait();
        EXPECT_FALSE(!!result); // should be unreachable - silences unused warning
      } catch(const std::runtime_error& ex) { 
        EXPECT_STREQ(ex.what(), "failure"); 
        throw;
      }
    }, std::runtime_error);
}
#endif // !UNIFEX_NO_EXCEPTIONS
