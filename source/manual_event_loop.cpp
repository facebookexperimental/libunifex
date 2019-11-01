#include <unifex/manual_event_loop.hpp>

namespace unifex {

void manual_event_loop::run() {
  std::unique_lock lock{mutex_};
  while (!stop_) {
    while (head_ == nullptr) {
      cv_.wait(lock);
      if (stop_)
        return;
    }
    auto* task = head_;
    head_ = task->next_;
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    lock.unlock();
    task->execute();
    lock.lock();
  }
}

void manual_event_loop::stop() {
  std::unique_lock lock{mutex_};
  stop_ = true;
  cv_.notify_all();
}

void manual_event_loop::enqueue(task_base* task) {
  std::unique_lock lock{mutex_};
  if (head_ == nullptr) {
    head_ = task;
  } else {
    tail_->next_ = task;
  }
  tail_ = task;
  task->next_ = nullptr;
  cv_.notify_one();
}

} // unifex
