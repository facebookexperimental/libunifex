/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <unifex/config.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _sf
  {
    template <const auto& C, typename Sender>
    struct sender_for {
      explicit sender_for(Sender snd)
          noexcept(std::is_nothrow_move_constructible_v<Sender>)
        : snd_((Sender&&) snd) {}

      template (typename CPO, typename Self, typename... Args)
        (requires same_as<sender_for, remove_cvref_t<Self>> AND
            callable<CPO, member_t<Self, Sender>, Args...>)
      UNIFEX_ALWAYS_INLINE
      friend decltype(auto) tag_invoke(CPO cpo, Self&& self, Args&&... args)
          noexcept(is_nothrow_callable_v<CPO, member_t<Self, Sender>, Args...>) {
        return ((CPO&&) cpo)(((Self&&) self).snd_, (Args&&) args...);
      }

      const Sender& base() const & noexcept {
        return snd_;
      }

      Sender&& base() && noexcept {
        return (Sender&&) snd_;
      }
     private:
      Sender snd_;
    };

    template <auto& CPO>
    struct _make_sender_for {
      template (typename Sender)
        (requires sender<Sender>)
      sender_for<CPO, remove_cvref_t<Sender>> operator()(Sender&& snd) const
          noexcept(std::is_nothrow_constructible_v<remove_cvref_t<Sender>, Sender>) {
        return sender_for<CPO, remove_cvref_t<Sender>>{(Sender&&) snd};
      }
    };
  } // namespace _sf

  using _sf::sender_for;

  template <const auto& CPO>
  inline constexpr _sf::_make_sender_for<CPO> make_sender_for {};

  template <const auto& CPO, typename Sender>
  struct sender_traits<sender_for<CPO, Sender>>
    : sender_traits<Sender>
  {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
