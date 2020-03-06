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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/get_allocator.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>

namespace unifex {

namespace detail {

template <typename CPO, typename Value, typename Sender, typename Receiver>
class with_query_value_operation {
  class receiver_wrapper {
  public:
    template <typename Receiver2>
    explicit receiver_wrapper(Receiver2 &&receiver,
                              with_query_value_operation *op)
        : receiver_((Receiver2 &&) receiver), op_(op) {}

  private:
    Value& get_value() const { return op_->value_; }

    friend const Value &tag_invoke(CPO, const receiver_wrapper &r) noexcept {
      return r.get_value();
    }

    template <typename OtherCPO, typename... Args>
    friend auto tag_invoke(
        OtherCPO cpo, const receiver_wrapper &r,
        Args &&... args) noexcept(std::is_nothrow_invocable_v<OtherCPO,
                                                              const Receiver &,
                                                              Args...>)
        -> std::invoke_result_t<OtherCPO, const Receiver &, Args...> {
      return std::invoke(std::move(cpo), std::as_const(r.receiver_),
                         (Args &&) args...);
    }

    template <typename OtherCPO, typename... Args>
    friend auto
    tag_invoke(OtherCPO cpo, receiver_wrapper &r, Args &&... args) noexcept(
        std::is_nothrow_invocable_v<OtherCPO, Receiver &, Args...>)
        -> std::invoke_result_t<OtherCPO, Receiver &, Args...> {
      return std::invoke(std::move(cpo), r.receiver_, (Args &&) args...);
    }

    template <typename OtherCPO, typename... Args>
    friend auto
    tag_invoke(OtherCPO cpo, receiver_wrapper &&r, Args &&... args) noexcept(
        std::is_nothrow_invocable_v<OtherCPO, Receiver, Args...>)
        -> std::invoke_result_t<OtherCPO, Receiver, Args...> {
      return std::invoke(std::move(cpo), (Receiver &&) r.receiver_,
                         (Args &&) args...);
    }

    Receiver receiver_;
    with_query_value_operation *op_;
  };

public:
  template <typename Receiver2, typename Value2>
  explicit with_query_value_operation(Sender &&sender, Receiver2 &&receiver,
                                      Value2 &&value)
      : value_((Value2 &&) value),
        innerOp_(
            connect((Sender &&) sender,
                         receiver_wrapper{(Receiver2 &&) receiver, this})) {}

  void start() & noexcept { unifex::start(innerOp_); }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Value value_;
  /*UNIFEX_NO_UNIQUE_ADDRESS*/ operation_t<Sender, receiver_wrapper> innerOp_;
};

} // namespace detail

template <typename CPO, typename Value, typename Sender>
class with_query_value_sender {
public:
  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using value_types = typename Sender::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename Sender::template error_types<Variant>;

  template <typename Sender2, typename Value2>
  explicit with_query_value_sender(Sender2 &&sender, Value2 &&value)
      : sender_((Sender2 &&) sender), value_((Value &&) value) {}

  template <typename Receiver>
  detail::with_query_value_operation<CPO, Value, Sender, std::decay_t<Receiver>>
  connect(Receiver &&receiver) && {
    return detail::with_query_value_operation<CPO, Value, Sender,
                                              std::decay_t<Receiver>>{
        (Sender &&) sender_, (Receiver &&) receiver, (Value &&) value_};
  }

  template <typename Receiver>
  detail::with_query_value_operation<CPO, Value, Sender &,
                                     std::decay_t<Receiver>>
  connect(Receiver &&receiver) & {
    return detail::with_query_value_operation<CPO, Value, Sender &,
                                              std::decay_t<Receiver>>{
        sender_, (Receiver &&) receiver, value_};
  }

  template <typename Receiver>
  detail::with_query_value_operation<CPO, Value, const Sender &,
                                     std::decay_t<Receiver>>
  connect(Receiver &&receiver) const & {
    return detail::with_query_value_operation<CPO, Value, const Sender &,
                                              std::decay_t<Receiver>>{
        sender_, (Receiver &&) receiver, value_};
  }

private:
  Sender sender_;
  Value value_;
};

template <typename Sender, typename CPO, typename Value>
with_query_value_sender<CPO, std::decay_t<Value>, std::decay_t<Sender>>
with_query_value(Sender &&sender, CPO, Value &&value) {
  static_assert(std::is_empty_v<CPO>, "with_query_value() does not support stateful CPOs");
  return with_query_value_sender<CPO, std::decay_t<Value>,
                                 std::decay_t<Sender>>{(Sender &&) sender,
                                                       (Value &&) value};
}

} // namespace unifex
