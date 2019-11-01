#pragma once

#include <unifex/manual_event_loop.hpp>

#include <thread>

namespace unifex {

class single_thread_context {
  manual_event_loop loop_;
  std::thread thread_;

public:
  single_thread_context() : loop_(), thread_([this] { loop_.run(); }) {}

  ~single_thread_context() {
    loop_.stop();
    thread_.join();
  }

  auto get_scheduler() noexcept {
    return loop_.get_scheduler();
  }
};

} // namespace unifex
