#pragma once

#include <unifex/scheduler_concepts.hpp>
#include <unifex/transform.hpp>
#include <unifex/typed_via.hpp>

namespace unifex {

template <typename Scheduler, typename Predecessor, typename Func>
auto then_execute(Scheduler&& s, Predecessor&& p, Func&& f) {
  return transform(
      typed_via(cpo::schedule((Scheduler &&) s), (Predecessor &&) p),
      (Func &&) f);
}

} // namespace unifex
