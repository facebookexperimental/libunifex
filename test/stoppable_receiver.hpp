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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or bodyied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>

namespace unifex_test {

using namespace unifex;

struct UnstoppableSimpleIntReceiver {
  void set_value(int) noexcept {}

  void set_error(std::exception_ptr) noexcept {}

  void set_done() noexcept {}
};

struct InplaceStoppableIntReceiver : public UnstoppableSimpleIntReceiver {
  InplaceStoppableIntReceiver(inplace_stop_source& source) noexcept
    : source_(source) {}

  friend inplace_stop_token tag_invoke(
      tag_t<get_stop_token>, const InplaceStoppableIntReceiver& r) noexcept {
    return r.source_.get_token();
  }

  inplace_stop_source& source_;
};

struct inplace_stop_token_redux : public inplace_stop_token {
  inplace_stop_token_redux(inplace_stop_token token)
    : inplace_stop_token(token) {}
};

struct NonInplaceStoppableIntReceiver : public UnstoppableSimpleIntReceiver {
  NonInplaceStoppableIntReceiver(inplace_stop_source& source) noexcept
    : source_(source) {}

  friend inplace_stop_token_redux tag_invoke(
      tag_t<get_stop_token>, const NonInplaceStoppableIntReceiver& r) noexcept {
    return inplace_stop_token_redux{r.source_.get_token()};
  }

  inplace_stop_source& source_;
};
}  // namespace unifex_test
