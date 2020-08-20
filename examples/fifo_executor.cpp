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
              [](int idx){std::cout << "Transform 2 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(sched, 2),
              [](int idx){std::cout << "Transform 3 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(sched, 2),
              [](int idx){std::cout << "Transform 4 at " << idx << "\n";}))));

    std::cout << "On normal single thread context with eager fifo scheduling\n";
    std::cout << "Fifo context: " << unifex::get_fifo_context(fifo_sched) << "\n";
    // This enqueues eagerly by making an internal decision based on matching fifo
    // contexts, and then takes into account the fifo assumption to not call set_next.
    // bulk_schedule is customised for fifo_sched.
    // fifo_sequence is a customisation of sequence that I've called out explicitly here
    // to avoid noisily adding tag_invoke calls throughout.
    // I think what we would actually want to do here is implement tag_invoke for each
    // bulk algorithm to trivially wrap the returned sender in a wrapper that can
    // be used to trigger the next customisation.
    // That way we can call "sequence" here instead of fifo sequence and use the
    // customisation underneath.
    // That's the customisation machinery though, independent of the point about
    // creating a fifo operation.
    sync_wait(
        fifo_sequence(
          bulk_join(
            bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 5 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 6 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 7 at " << idx << "\n";})),
          bulk_join(
            bulk_transform(
              bulk_schedule(fifo_sched, 2),
              [](int idx){std::cout << "Transform 8 at " << idx << "\n";}))));

    {
      auto compound_sender =
        bulk_join(
          bulk_transform(
            bulk_schedule(fifo_sched, 2),
            [](int idx){std::cout << "Transform 3 at " << idx << "\n";}));
      std::cout <<
        "Fifo context of a compound fifo sender: " <<
        unifex::get_fifo_context(compound_sender) <<
        "\n";
    }

    return 0;
}
