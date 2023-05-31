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

#include <unifex/just_from.hpp>
#include <unifex/on.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/spawn_future.hpp>
#include <unifex/v1/async_scope.hpp>
#include <unifex/detail/debug_async_scope.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

inline namespace v1 {

namespace _debug_async_scope {

struct debug_async_scope final {
  template <typename Sender>
  auto spawn(Sender&& sender) {
    return spawn_future(static_cast<Sender&&>(sender), *this);
  }

  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler>)            //
      auto spawn_on(Scheduler&& scheduler, Sender&& sender) {
    return spawn(
        on(static_cast<Scheduler&&>(scheduler), static_cast<Sender&&>(sender)));
  }

  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      auto spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    return spawn_on(
        static_cast<Scheduler&&>(scheduler),
        just_from(static_cast<Fun&&>(fun)));
  }

  template <typename Sender>
  auto detached_spawn(Sender&& sender) {
    spawn_detached(static_cast<Sender&&>(sender), *this);
  }

  template(typename Sender, typename Scheduler)  //
      (requires scheduler<Scheduler>)            //
      auto detached_spawn_on(Scheduler&& scheduler, Sender&& sender) {
    detached_spawn(
        on(static_cast<Scheduler&&>(scheduler), static_cast<Sender&&>(sender)));
  }

  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      void detached_spawn_call_on(Scheduler&& scheduler, Fun&& fun) {
    static_assert(
        is_nothrow_callable_v<Fun>,
        "Please annotate your callable with noexcept.");

    detached_spawn_on(
        static_cast<Scheduler&&>(scheduler),
        just_from(static_cast<Fun&&>(fun)));
  }

  template <typename Sender>
  using debug_scope_sender_t =
      unifex::detail::debug_scope_sender<remove_cvref_t<Sender>>;

  template <typename Sender>
  [[nodiscard]] auto
  attach(Sender&& sender) noexcept(std::is_nothrow_constructible_v<
                                   debug_scope_sender_t<Sender>,
                                   Sender,
                                   unifex::detail::debug_op_list*>) {
    return scope_.attach(
        debug_scope_sender_t<Sender>{static_cast<Sender&&>(sender), &ops_});
  }

  template(typename Fun)        //
      (requires callable<Fun>)  //
      [[nodiscard]] auto attach_call(Fun&& fun) noexcept(
          noexcept(attach(just_from(static_cast<Fun&&>(fun))))) {
    return attach(just_from(static_cast<Fun&&>(fun)));
  }

  template(typename Sender, typename Scheduler)           //
      (requires scheduler<Scheduler> AND sender<Sender>)  //
      [[nodiscard]] auto attach_on(Scheduler&& scheduler, Sender&& sender) noexcept(
          noexcept(attach(
              on(static_cast<Scheduler&&>(scheduler),
                 static_cast<Sender&&>(sender))))) {
    return attach(
        on(static_cast<Scheduler&&>(scheduler), static_cast<Sender&&>(sender)));
  }

  template(typename Scheduler, typename Fun)             //
      (requires scheduler<Scheduler> AND callable<Fun>)  //
      [[nodiscard]] auto attach_call_on(Scheduler&& scheduler, Fun&& fun) noexcept(
          noexcept(attach_on(
              static_cast<Scheduler&&>(scheduler),
              just_from(static_cast<Fun&&>(fun))))) {
    return attach_on(
        static_cast<Scheduler&&>(scheduler),
        just_from(static_cast<Fun&&>(fun)));
  }

  [[nodiscard]] auto complete() noexcept { return scope_.complete(); }

  [[nodiscard]] auto cleanup() noexcept { return scope_.cleanup(); }

  inplace_stop_token get_stop_token() noexcept {
    return scope_.get_stop_token();
  }

  void request_stop() noexcept { scope_.request_stop(); }

private:
  unifex::v1::async_scope scope_;
  unifex::detail::debug_op_list ops_;

  template(typename Sender, typename Scope)           //
      (requires same_as<debug_async_scope&, Scope&>)  //
      friend auto tag_invoke(
          tag_t<nest>,
          Sender&& sender,
          Scope& scope) noexcept(noexcept(scope
                                              .attach(static_cast<Sender&&>(
                                                  sender))))
          -> decltype(scope.attach(static_cast<Sender&&>(sender))) {
    return scope.attach(static_cast<Sender&&>(sender));
  }
};

}  // namespace _debug_async_scope
using _debug_async_scope::debug_async_scope;
}  // namespace v1
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
