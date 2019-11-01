#pragma once

#include <unifex/reduce_stream.hpp>
#include <unifex/transform.hpp>
#include <unifex/type_traits.hpp>

namespace unifex {
namespace cpo {

inline constexpr struct for_each_cpo {
  template <typename Stream, typename Func>
  friend auto tag_invoke(for_each_cpo, Stream&& stream, Func&& func) {
    return transform(
        reduce_stream(
            (Stream &&) stream,
            unit{},
            [func = (Func &&) func](unit s, auto&&... values) mutable {
              std::invoke(func, (decltype(values))values...);
              return s;
            }),
        [](unit) noexcept {});
  }

  template <typename Stream, typename Func>
  auto operator()(Stream&& stream, Func&& func) const
      noexcept(noexcept(tag_invoke(*this, (Stream &&) stream, (Func &&) func)))
          -> decltype(tag_invoke(*this, (Stream &&) stream, (Func &&) func)) {
    return tag_invoke(*this, (Stream &&) stream, (Func &&) func);
  }
} for_each;

} // namespace cpo
} // namespace unifex
