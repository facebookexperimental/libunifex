/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

template <bool Noexcept>
struct mock_receiver_body_base<void() noexcept(Noexcept)> {
  MOCK_QUALIFIED_METHOD0(set_value, noexcept(Noexcept), void());
};

template <typename T, bool Noexcept>
struct mock_receiver_body_base<void(T) noexcept(Noexcept)> {
  MOCK_QUALIFIED_METHOD1_T(set_value, noexcept(Noexcept), void(T));
};

template <typename T, typename U, bool Noexcept>
struct mock_receiver_body_base<void(T, U) noexcept(Noexcept)> {
  MOCK_QUALIFIED_METHOD2_T(set_value, noexcept(Noexcept), void(T, U));
};

template <typename T, typename U, typename V, bool Noexcept>
struct mock_receiver_body_base<void(T, U, V) noexcept(Noexcept)> {
  MOCK_QUALIFIED_METHOD3_T(set_value, noexcept(Noexcept), void(T, U, V));
};

template <typename... Sigs>
struct mock_receiver_body : mock_receiver_body_base<Sigs>... {
  using mock_receiver_body_base<Sigs>::gmock_set_value...;
  using mock_receiver_body_base<Sigs>::set_value...;

  MOCK_QUALIFIED_METHOD1(set_error, noexcept, void(std::exception_ptr));

  MOCK_QUALIFIED_METHOD0(set_done, noexcept, void());
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
