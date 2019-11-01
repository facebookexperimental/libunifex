#pragma once

#include <unifex/scheduler_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/transform.hpp>

namespace unifex {
namespace cpo {

inline constexpr struct schedule_with_subscheduler_cpo {
 private:
  template <typename T>
  struct return_value {
    T value;

    T operator()() && {
      return std::move(value);
    }

    T operator()() & {
      return value;
    }

    T operator()() const& {
      return value;
    }
  };

  template <typename Scheduler>
  friend auto tag_invoke(schedule_with_subscheduler_cpo, Scheduler&& scheduler)
      -> decltype(transform(
          std::declval<std::invoke_result_t<decltype(schedule), Scheduler&>>(),
          std::declval<return_value<std::decay_t<Scheduler>>>())) {
    auto&& scheduleOp = cpo::schedule(scheduler);
    return transform(
        static_cast<decltype(scheduleOp)>(scheduleOp),
        return_value<std::decay_t<Scheduler>>{(Scheduler &&) scheduler});
  }

 public:
  template <typename Scheduler>
  auto operator()(Scheduler&& s) const
      noexcept(noexcept(tag_invoke(*this, (Scheduler &&) s)))
          -> decltype(tag_invoke(*this, (Scheduler &&) s)) {
    return tag_invoke(*this, (Scheduler &&) s);
  }
} schedule_with_subscheduler;

} // namespace cpo
} // namespace unifex
