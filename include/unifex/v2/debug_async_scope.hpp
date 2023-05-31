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

#include <unifex/v2/async_scope.hpp>
#include <unifex/detail/debug_async_scope.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex::v2 {

namespace _debug_async_scope {

struct debug_async_scope final {
  [[nodiscard]] auto join() noexcept { return scope_.join(); }

  bool joined() const noexcept { return scope_.joined(); }

  bool join_started() const noexcept { return scope_.join_started(); }

  std::size_t use_count() const noexcept { return scope_.use_count(); }

  template <typename Sender>
  using debug_scope_sender_t =
      unifex::detail::debug_scope_sender<remove_cvref_t<Sender>>;

  template <typename Sender>
  static constexpr bool sender_nothrow_constructible =
      std::is_nothrow_constructible_v<
          debug_scope_sender_t<Sender>,
          Sender,
          unifex::detail::debug_op_list*>;
  template <typename Sender>
  static constexpr bool nest_nothrow_invocable =
      noexcept(UNIFEX_DECLVAL(unifex::v2::async_scope)
                   .nest(UNIFEX_DECLVAL(debug_scope_sender_t<Sender>)));
  template(typename Sender)      //
      (requires sender<Sender>)  //
      [[nodiscard]] auto nest(Sender&& sender) noexcept(
          sender_nothrow_constructible<Sender>&&
              nest_nothrow_invocable<Sender>) {
    return scope_.nest(
        debug_scope_sender_t<Sender>{static_cast<Sender&&>(sender), &ops_});
  }

private:
  unifex::v2::async_scope scope_;
  unifex::detail::debug_op_list ops_;
};

}  // namespace _debug_async_scope
using _debug_async_scope::debug_async_scope;
}  // namespace unifex::v2

#include <unifex/detail/epilogue.hpp>
