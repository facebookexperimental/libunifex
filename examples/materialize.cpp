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

#include <unifex/when_all.hpp>
#include <unifex/materialize.hpp>
#include <unifex/dematerialize.hpp>
#include <unifex/transform.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <cassert>

using namespace unifex;

int main() {
    single_thread_context ctx;

    [[maybe_unused]] optional<int> result = sync_wait(
        dematerialize(
            materialize(
                transform(
                    schedule(ctx.get_scheduler()),
                    []() { return 42; }))));
    UNIFEX_ASSERT(result.value() == 42);

    return 0;
}
