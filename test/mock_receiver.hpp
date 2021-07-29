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

template <typename Sig>
struct mock_receiver_body_base;

template <>
struct mock_receiver_body_base<void()> {
  MOCK_METHOD(void, set_value, (), ());
};
template <>
struct mock_receiver_body_base<void() noexcept> {
  MOCK_METHOD(void, set_value, (), (noexcept));
};

template <typename T>
struct mock_receiver_body_base<void(T)> {
  MOCK_METHOD(void, set_value, (T), ());
};
template <typename T>
struct mock_receiver_body_base<void(T) noexcept> {
  MOCK_METHOD(void, set_value, (T), (noexcept));
};

template <typename T, typename U>
struct mock_receiver_body_base<void(T, U)> {
  MOCK_METHOD(void, set_value, (T, U), ());
};
template <typename T, typename U>
struct mock_receiver_body_base<void(T, U) noexcept> {
  MOCK_METHOD(void, set_value, (T, U), (noexcept));
};

template <typename T, typename U, typename V>
struct mock_receiver_body_base<void(T, U, V)> {
  MOCK_METHOD(void, set_value, (T, U, V), ());
};
template <typename T, typename U, typename V>
struct mock_receiver_body_base<void(T, U, V) noexcept> {
  MOCK_METHOD(void, set_value, (T, U, V), (noexcept));
};

template <typename... Sigs>
struct mock_receiver_body : mock_receiver_body_base<Sigs>... {
  using mock_receiver_body_base<Sigs>::gmock_set_value...;
  using mock_receiver_body_base<Sigs>::set_value...;

  MOCK_METHOD(void, set_error, (std::exception_ptr), (noexcept));
  MOCK_METHOD(void, set_done, (), (noexcept));
};

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

  auto& operator*() noexcept {
    return *body_;
  }

  const auto& operator*() const noexcept {
    return *body_;
  }
};

} // namespace unifex_test
