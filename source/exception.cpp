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

#include <unifex/config.hpp>
#include <unifex/exception.hpp>

namespace unifex {
namespace _except_ptr {
#if !UNIFEX_HAS_FAST_MAKE_EXCEPTION_PTR
inline void _ref::rethrow() const {
  throw_(p_);
  UNIFEX_ASSUME_UNREACHABLE;
}

std::exception_ptr _fn::operator()(_ref const e) const noexcept {
  UNIFEX_TRY {
    e.rethrow();
  } UNIFEX_CATCH(...) {
    return std::current_exception();
  }
}
#endif
} // namespace _except_ptr
} // namespace unifex
