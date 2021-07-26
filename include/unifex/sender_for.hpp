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
    template <typename Query>
    struct _property {
      const typename Query::value_type& operator()(const typename Query::key_type &) const noexcept {
        return query_.value;
      }
      UNIFEX_NO_UNIQUE_ADDRESS Query query_;
    };
    template <typename... Queries>
    struct _ctx {
      struct type : _property<Queries>... {
        using _property<Queries>::operator()...;
      };
    };
    template <typename... Queries>
    using _context = typename _ctx<Queries...>::type;

    template <const auto& C, typename Sender, typename Context>
    struct sender_for {
      explicit sender_for(Sender snd, Context ctx)
          noexcept(
              std::is_nothrow_move_constructible_v<Sender> &&
              std::is_nothrow_move_constructible_v<Context>)
        : snd_((Sender&&) snd), ctx_{(Context&&) ctx} {}

      // Forward all tag_invokes:
      template (typename CPO, typename Self, typename... Args)
        (requires same_as<sender_for, remove_cvref_t<Self>> AND
            (!callable<const Context&, CPO>) AND
            callable<CPO, member_t<Self, Sender>, Args...>)
      UNIFEX_ALWAYS_INLINE
      friend decltype(auto) tag_invoke(CPO cpo, Self&& self, Args&&... args)
          noexcept(is_nothrow_callable_v<CPO, member_t<Self, Sender>, Args...>) {
        return ((CPO&&) cpo)(((Self&&) self).snd_, (Args&&) args...);
      }

      // Handle custom property queries by consulting the context:
      template (typename CPO)
        (requires callable<const Context&, CPO>)
      UNIFEX_ALWAYS_INLINE
      friend decltype(auto) tag_invoke(CPO cpo, const sender_for& self) noexcept {
        return self.ctx_(cpo);
      }

      const Sender& base() const & noexcept {
        return snd_;
      }

      Sender&& base() && noexcept {
        return (Sender&&) snd_;
      }
     private:
      Sender snd_;
      UNIFEX_NO_UNIQUE_ADDRESS Context ctx_;
    };

    template <auto& CPO>
    struct _make_sender_for {
      template (typename Sender, typename... Queries)
        (requires sender<Sender>)
      sender_for<CPO, remove_cvref_t<Sender>, _context<remove_cvref_t<Queries>...>>
      operator()(Sender&& snd, Queries&&... queries) const
          noexcept(
              std::is_nothrow_constructible_v<remove_cvref_t<Sender>, Sender> &&
              (std::is_nothrow_constructible_v<remove_cvref_t<Queries>, Queries> &&...)) {
        return sender_for<CPO, remove_cvref_t<Sender>, _context<remove_cvref_t<Queries>...>>{
            (Sender&&) snd, _context<remove_cvref_t<Queries>...>{{(Queries&&) queries}...}};
      }
    };
  } // namespace _sf

  using _sf::sender_for;

  template <const auto& CPO>
  inline constexpr _sf::_make_sender_for<CPO> make_sender_for {};

  template <typename T, const auto& CPO>
  inline constexpr bool is_sender_for_v = false;

  template <typename Sender, typename Context, const auto& CPO>
  inline constexpr bool is_sender_for_v<sender_for<CPO, Sender, Context>, CPO> = true;

  template <const auto& CPO, typename Sender, typename Context>
  struct sender_traits<sender_for<CPO, Sender, Context>>
    : sender_traits<Sender>
  {};
} // namespace unifex

#include <unifex/detail/epilogue.hpp>
