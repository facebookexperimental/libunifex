#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/when_all.hpp>
#include <unifex/sequence.hpp>

#include <chrono>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(TimedSingleThreadContext, ConstructDestruct) {
    unifex::timed_single_thread_context ctx;
}

TEST(TimedSingleThreadContext, ScheduleAfter) {
    unifex::timed_single_thread_context ctx;
    unifex::sync_wait(unifex::schedule_after(ctx.get_scheduler(), 10ms));
}

TEST(TimedSingleThreadContext, ScheduleAfterConcurrent) {
    unifex::timed_single_thread_context ctx;
    std::thread t1{[&] { unifex::sync_wait(unifex::schedule_after(ctx.get_scheduler(), 10ms)); }};
    std::thread t2{[&] { unifex::sync_wait(unifex::schedule_after(ctx.get_scheduler(), 10ms)); }};
    t1.join();
    t2.join();
}

TEST(TimedSingleThreadContext, ScheduleAfterParallel) {
    unifex::timed_single_thread_context ctx;
    unifex::sync_wait(
        unifex::when_all(
            unifex::schedule_after(ctx.get_scheduler(), 10ms),
            unifex::schedule_after(ctx.get_scheduler(), 15ms)));
}

TEST(TimedSingleThreadContext, ScheduleAfterParallelWithSchedule) {
    unifex::timed_single_thread_context ctx;
    unifex::sync_wait(
        unifex::when_all(
            unifex::schedule_after(ctx.get_scheduler(), 10ms),
            unifex::schedule_after(ctx.get_scheduler(), 15ms),
            unifex::schedule(ctx.get_scheduler())));
}

TEST(TimedSingleThreadContext, ScheduleAfterSequential) {
    unifex::timed_single_thread_context ctx;
    unifex::sync_wait(
        unifex::sequence(
            unifex::schedule_after(ctx.get_scheduler(), 15ms),
            unifex::schedule_after(ctx.get_scheduler(), 15ms)));
}
