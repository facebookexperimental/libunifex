#pragma once

#if __GNUG__ && !__clang__
#define UNIFEX_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define UNIFEX_NO_UNIQUE_ADDRESS
#endif

#if __cpp_coroutines >= 201703L
# define UNIFEX_HAVE_COROUTINES 1
#else
# define UNIFEX_HAVE_COROUTINES 0
#endif

#include <type_traits>

namespace std
{
  template<typename T>
  using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;
}
