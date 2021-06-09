/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/config.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/type_index.hpp>
#include <unifex/continuations.hpp>

#include <functional>
#include <vector>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _async_trace {
  struct entry {
    entry(
        std::size_t depth,
        std::size_t parentIndex,
        const continuation_info& continuation) noexcept
      : depth(depth)
      , parentIndex(parentIndex)
      , continuation(continuation) {}

    std::size_t depth;
    std::size_t parentIndex;
    continuation_info continuation;
  };
} // namespace _async_trace
using async_trace_entry = _async_trace::entry;

namespace _async_trace_cpo {
  inline const struct _fn {
    template <typename Continuation>
    std::vector<async_trace_entry> operator()(const Continuation& c) const {
      std::vector<async_trace_entry> results;
      results.emplace_back(0, 0, continuation_info::from_continuation(c));

      // Breadth-first search of async call-stack graph.
      for (std::size_t i = 0; i < results.size(); ++i) {
        auto [depth, parentIndex, info] = results[i];
        visit_continuations(
            info, [depth = depth, i, &results](const continuation_info& x) {
              results.emplace_back(depth + 1, i, x);
            });
      }
      return results;
    }
  } async_trace {};
} // namespace _async_trace_cpo
using _async_trace_cpo::async_trace;

namespace _async_trace {
  template <typename Receiver>
  struct _op {
    struct type {
      Receiver receiver_;

      void start() noexcept {
        UNIFEX_TRY {
          auto trace = async_trace(receiver_);
          unifex::set_value(std::move(receiver_), std::move(trace));
        } UNIFEX_CATCH (...) {
          unifex::set_error(std::move(receiver_), std::current_exception());
        }
      }
    };
  };
  template <typename Receiver>
  using operation = typename _op<remove_cvref_t<Receiver>>::type;

  struct sender {
    template <
        template <typename...> class Variant,
        template <typename...> class Tuple>
    using value_types = Variant<Tuple<std::vector<entry>>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = false;

    template <typename Receiver>
    operation<Receiver> connect(Receiver&& r) const& {
      return operation<Receiver>{(Receiver &&) r};
    }

    friend auto tag_invoke(tag_t<blocking>, const sender&) noexcept {
      return blocking_kind::always_inline;
    }
  };
} // namespace _async_trace
using async_trace_sender = _async_trace::sender;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
