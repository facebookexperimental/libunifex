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
#include <unifex/single_thread_context.hpp>
#include <unifex/executor_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/fifo_context.hpp>
#include <unifex/fifo_support.hpp>
#include <unifex/sequence.hpp>
#include <unifex/bulk_schedule.hpp>
#include <unifex/bulk_transform.hpp>
#include <unifex/bulk_join.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/fifo_bulk_transform.hpp>
#include <unifex/fifo_bulk_join.hpp>
#include <unifex/fifo_sequence.hpp>


#include <cstdio>
#include <cassert>
#include <iostream>

using namespace unifex;

int main() {
    single_thread_context sctx;
    auto sched = sctx.get_scheduler();
    fifo_context fctx;
    auto fifo_sched = fctx.get_scheduler();

    std::cout << "On normal single thread context\n";
    std::cout << "Fifo context: " << unifex::get_fifo_context(sched) << "\n";
    sync_wait(
        sequence(
          bulk_join(
            bulk_transform(
              bulk_schedule(sched, 2),
              [](int idx){std::cout << "Transform 1 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(sched, 2),
              [](int idx){std::cout << "Transform 2 at " << idx << "\n";}))));

    std::cout << "On normal single thread context with eager fifo scheduling\n";
    std::cout << "Fifo context: " << unifex::get_fifo_context(fifo_sched) << "\n";
    // The renamed algorithms here are for simplicity
    // We should actually have each algorithm being called via tag_invoke against
    // the previous sender, with the current implementation as the default.
    // That way each customised algorithm is just a tag_invoke hook point
    // on each other fifo algorithm from the initial sender.
    // These will be replaced with tag_invokes and the public CPO be hidden once
    // this works.
    sync_wait(
        fifo_sequence(
          fifo_bulk_join(
            fifo_bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 3 at " << idx << "\n";})),
          fifo_bulk_join(
            fifo_bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 4 at " << idx << "\n";}))));

    return 0;
}
