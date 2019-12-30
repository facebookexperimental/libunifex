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
 #pragma once

#include <unifex/scheduler_concepts.hpp>
#include <unifex/transform.hpp>
#include <unifex/typed_via.hpp>

namespace unifex {

template <typename Predecessor, typename Range, typename Policy, typename Func>
auto indexed_for(Predecessor&& p, Range&& r, Policy&& p, Func&& f) {
  return indexed_for_sender<std::remove_cvref_t<Sender>, std::decay_t<Range>, std::decay_t<Policy>, std::decay_t<Func>>{
      (Sender &&) predecessor, (Range&& ) r, (Policy&&) p, (Func &&) func};
}

} // namespace unifex
