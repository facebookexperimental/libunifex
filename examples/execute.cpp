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
#include <unifex/single_thread_context.hpp>
#include <unifex/executor_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>

#include <cstdio>

using namespace unifex;

int main() {
    single_thread_context ctx;

    for (int i = 0; i < 5; ++i) {
        execute(schedule(ctx.get_scheduler()), [i]() {
            printf("hello execute() %i\n", i);
        });
    }

    return 0;
}
