#include <unifex/trampoline_scheduler.hpp>

namespace unifex {

thread_local trampoline_scheduler::trampoline_state*
    trampoline_scheduler::trampoline_state::current_ = nullptr;

void trampoline_scheduler::trampoline_state::drain() noexcept {
  while (head_ != nullptr) {
    operation_base* op = head_;
    head_ = op->next_;
    recursionDepth_ = 1;
    op->execute();
  }
}

} // namespace unifex
