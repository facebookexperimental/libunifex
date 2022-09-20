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

#include <gmock/gmock.h>

#include <exception>
#include <memory>

namespace unifex_test {

template <typename Sig, bool NoExcept>
struct mock_receiver_body_base_impl;

template <bool NoExcept>
struct mock_receiver_body_base_impl<void(), NoExcept> {
  MOCK_METHOD(void, set_value, (), (noexcept(NoExcept)));
};

template <typename T, bool NoExcept>
struct mock_receiver_body_base_impl<void(T), NoExcept> {
  MOCK_METHOD(void, set_value, (T), (noexcept(NoExcept)));
};

template <typename T, typename U, bool NoExcept>
struct mock_receiver_body_base_impl<void(T, U), NoExcept> {
  MOCK_METHOD(void, set_value, (T, U), (noexcept(NoExcept)));
};

template <typename T, typename U, typename V, bool NoExcept>
struct mock_receiver_body_base_impl<void(T, U, V), NoExcept> {
  MOCK_METHOD(void, set_value, (T, U, V), (noexcept(NoExcept)));
};

template <typename Sig>
struct mock_receiver_body_base;

template <typename R, typename... As>
struct mock_receiver_body_base<R(As...)> {
    using type = mock_receiver_body_base_impl<R(As...), false>;
};

template <typename R, typename... As>
struct mock_receiver_body_base<R(As...) noexcept> {
    using type = mock_receiver_body_base_impl<R(As...), true>;
};

template <typename Sig>
using mock_receiver_body_base_t =
    typename mock_receiver_body_base<Sig>::type;

template <typename... Bases>
struct mock_receiver_body_impl : Bases... {
  using Bases::gmock_set_value...;
  using Bases::set_value...;

  MOCK_METHOD(void, set_error, (std::exception_ptr), (noexcept));
  MOCK_METHOD(void, set_done, (), (noexcept));
};

template <typename... Sigs>
using mock_receiver_body =
    mock_receiver_body_impl<mock_receiver_body_base_t<Sigs>...>;

template <typename... Sigs>
struct mock_receiver {
 private:
  std::shared_ptr<mock_receiver_body<Sigs...>> body_
      = std::make_shared<mock_receiver_body<Sigs...>>();

 public:
  template <typename... T>
  auto set_value(T&&... ts) noexcept(noexcept(body_->set_value((T&&)ts...)))
      -> decltype(body_->set_value((T&&)ts...)) {
    body_->set_value((T&&)ts...);
  }

  void set_error(std::exception_ptr eptr) noexcept {
    body_->set_error(std::move(eptr));
  }

  void set_done() noexcept {
    body_->set_done();
  }

  mock_receiver_body<Sigs...>& operator*() noexcept {
    return *body_;
  }

  const mock_receiver_body<Sigs...>& operator*() const noexcept {
    return *body_;
  }
};

} // namespace unifex_test
