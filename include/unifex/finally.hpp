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

#include <unifex/async_trace.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/type_list.hpp>
#include <unifex/std_concepts.hpp>

#include <cassert>
#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex
{
  namespace _final
  {
    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver>
    struct _op {
      class type;
    };
    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver>
    using operation =
        typename _op<SourceSender, CompletionSender, remove_cvref_t<Receiver>>::type;

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename... Values>
    struct _value_receiver {
      class type;
    };
    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename... Values>
    using value_receiver =
      typename _value_receiver<
        SourceSender,
        CompletionSender,
        Receiver,
        std::decay_t<Values>...>::type;

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename... Values>
    class _value_receiver<SourceSender, CompletionSender, Receiver, Values...>::type final {
      using value_receiver = type;
      using operation_type = operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit type(operation_type* op) noexcept : op_(op) {}

      type(type&& other) noexcept
      : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionValueOp_.template get<
            connect_result_t<CompletionSender, value_receiver>>();
        completionOp.destruct();

        auto& valueStorage = op->value_.template get<std::tuple<Values...>>();
        try {
          // Move the stored values onto the stack so that we can
          // destroy the ones stored in the operation-state. This
          // prevents the need to add a big switch to the operation
          // state destructor to determine which value-tuple type
          // destructor needs to be run.
          auto values = [&]() -> std::tuple<Values...> {
            scope_guard g{[&]() noexcept {
              valueStorage.destruct();
            }};
            return static_cast<std::tuple<Values...>&&>(valueStorage.get());
          }
          ();

          std::apply(
              [&](Values&&... values) {
                unifex::set_value(
                    static_cast<Receiver&&>(op->receiver_),
                    static_cast<Values&&>(values)...);
              },
              static_cast<std::tuple<Values...>&&>(values));
        } catch (...) {
          unifex::set_error(
              static_cast<Receiver&&>(op->receiver_), std::current_exception());
        }
      }

      template <typename Error>
      void set_error(Error&& error) && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionValueOp_.template get<
            connect_result_t<CompletionSender, value_receiver>>();
        completionOp.destruct();

        // Discard the stored value.
        auto& valueStorage = op->value_.template get<std::tuple<Values...>>();
        valueStorage.destruct();

        unifex::set_error(
            static_cast<Receiver&&>(op->receiver_),
            static_cast<Error&&>(error));
      }

      void set_done() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionValueOp_.template get<
            connect_result_t<CompletionSender, value_receiver>>();
        completionOp.destruct();

        // Discard the stored value.
        auto& valueStorage = op->value_.template get<std::tuple<Values...>>();
        valueStorage.destruct();

        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, value_receiver> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const R& r) noexcept(is_nothrow_callable_v<
                                          CPO,
                                          const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_receiver());
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const value_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_receiver());
      }

    private:
      const Receiver& get_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename Error>
    struct _error_receiver {
      class type;
    };
    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename Error>
    using error_receiver =
      typename _error_receiver<
        SourceSender,
        CompletionSender,
        Receiver,
        std::decay_t<Error>>::type;

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename Error>
    class _error_receiver<SourceSender, CompletionSender, Receiver, Error>::type final {
      using error_receiver = type;
      using operation_type = operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit type(operation_type* op) noexcept : op_(op) {}

      type(type&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionErrorOp_.template get<
            connect_result_t<CompletionSender, error_receiver>>();
        completionOp.destruct();

        auto& errorStorage = op->error_.template get<Error>();
        Error errorCopy = static_cast<Error&&>(errorStorage.get());
        errorStorage.destruct();

        unifex::set_error(
            static_cast<Receiver&&>(op->receiver_),
            static_cast<Error&&>(errorCopy));
      }

      template(typename OtherError)
          (requires receiver<Receiver, OtherError>)
      void set_error(OtherError otherError) && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionErrorOp_.template get<
            connect_result_t<CompletionSender, error_receiver>>();
        completionOp.destruct();

        // Discard existing stored error from source-sender.
        auto& errorStorage = op->error_.template get<Error>();
        errorStorage.destruct();

        unifex::set_error(
            static_cast<Receiver&&>(op->receiver_),
            static_cast<OtherError&&>(otherError));
      }

      void set_done() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionErrorOp_.template get<
            connect_result_t<CompletionSender, error_receiver>>();
        completionOp.destruct();

        // Discard existing stored error from source-sender.
        auto& errorStorage = op->error_.template get<Error>();
        errorStorage.destruct();

        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, error_receiver> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const R& r) noexcept(is_nothrow_callable_v<
                                          CPO,
                                          const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_receiver());
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const error_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_receiver());
      }

    private:
      const Receiver& get_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename SourceSender, typename CompletionSender, typename Receiver>
    struct _done_receiver {
      class type;
    };
    template <typename SourceSender, typename CompletionSender, typename Receiver>
    using done_receiver =
        typename _done_receiver<SourceSender, CompletionSender, Receiver>::type;

    template <typename SourceSender, typename CompletionSender, typename Receiver>
    class _done_receiver<SourceSender, CompletionSender, Receiver>::type final {
      using done_receiver = type;
      using operation_type = operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit type(operation_type* op) noexcept : op_(op) {}

      type(type&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;
        op->completionDoneOp_.destruct();
        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template(typename Error)
          (requires receiver<Receiver, Error>)
      void set_error(Error&& error) && noexcept {
        auto* op = op_;
        op->completionDoneOp_.destruct();
        unifex::set_error(
            static_cast<Receiver&&>(op->receiver_),
            static_cast<Error&&>(error));
      }

      void set_done() && noexcept {
        auto* op = op_;
        op->completionDoneOp_.destruct();
        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, done_receiver> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto tag_invoke(
          CPO cpo,
          const R& r) noexcept(is_nothrow_callable_v<
                                          CPO,
                                          const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_receiver());
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const done_receiver& r,
          Func&& func) {
        std::invoke(func, r.get_receiver());
      }

    private:
      const Receiver& get_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <typename SourceSender, typename CompletionSender, typename Receiver>
    struct _receiver {
      class type;
    };
    template <typename SourceSender, typename CompletionSender, typename Receiver>
    using receiver_t = typename _receiver<SourceSender, CompletionSender, Receiver>::type;

    template <typename SourceSender, typename CompletionSender, typename Receiver>
    class _receiver<SourceSender, CompletionSender, Receiver>::type final {
      using operation_type = operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit type(operation_type* op) noexcept : op_(op) {}

      type(type&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      template(typename... Values)
          (requires receiver_of<Receiver, Values...>)
      void set_value(Values&&... values) && noexcept {
        auto* op = op_;
        auto& valueStorage =
            op->value_.template get<std::tuple<std::decay_t<Values>...>>();
        try {
          valueStorage.construct(static_cast<Values&&>(values)...);
        } catch (...) {
          std::move(*this).set_error(std::current_exception());
          return;
        }

        op->sourceOp_.destruct();

        try {
          using value_receiver = value_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              Values...>;
          auto& completionOp =
              op->completionValueOp_
                  .template get<connect_result_t<CompletionSender, value_receiver>>()
                  .construct_from([&] {
                    return unifex::connect(
                        static_cast<CompletionSender&&>(op->completionSender_),
                        value_receiver{op});
                  });
          unifex::start(completionOp);
        } catch (...) {
          valueStorage.destruct();
          unifex::set_error(
              static_cast<Receiver&&>(op->receiver_), std::current_exception());
        }
      }

      template <typename Error>
      void set_error(Error&& error) && noexcept {
        static_assert(
            std::is_nothrow_constructible_v<std::decay_t<Error>, Error>);

        auto* op = op_;
        auto& errorStorage = op->error_.template get<std::decay_t<Error>>();
        errorStorage.construct(static_cast<Error&&>(error));

        op->sourceOp_.destruct();

        try {
          using error_receiver_t = error_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              Error>;
          auto& completionOp =
              op->completionErrorOp_
                  .template get<connect_result_t<CompletionSender, error_receiver_t>>()
                  .construct_from([&] {
                    return unifex::connect(
                        static_cast<CompletionSender&&>(op->completionSender_),
                        error_receiver_t{op});
                  });
          unifex::start(completionOp);
        } catch (...) {
          errorStorage.destruct();
          unifex::set_error(
              static_cast<Receiver&&>(op->receiver_), std::current_exception());
        }
      }

      void set_done() && noexcept {
        auto* op = op_;

        op->sourceOp_.destruct();

        try {
          using done_receiver =
              done_receiver<SourceSender, CompletionSender, Receiver>;
          auto& completionOp = op->completionDoneOp_.construct_from([&] {
            return unifex::connect(
                static_cast<CompletionSender&&>(op->completionSender_),
                done_receiver{op});
          });
          unifex::start(completionOp);
        } catch (...) {
          unifex::set_error(
              static_cast<Receiver&&>(op->receiver_), std::current_exception());
        }
      }

      template(typename CPO, typename R)
          (requires is_receiver_query_cpo_v<CPO> AND
            same_as<R, type> AND
            is_callable_v<CPO, const Receiver&>)
      friend auto
      tag_invoke(CPO cpo, const R& r) noexcept(
          is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
        return static_cast<CPO&&>(cpo)(r.get_receiver());
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>, const type& r, Func&& func) {
        std::invoke(func, r.get_receiver());
      }

    private:
      const Receiver& get_receiver() const noexcept {
        return op_->receiver_;
      }

      operation_type* op_;
    };

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver>
    class _op<SourceSender, CompletionSender, Receiver>::type {
      friend receiver_t<SourceSender, CompletionSender, Receiver>;
      friend done_receiver<SourceSender, CompletionSender, Receiver>;

      template <
          typename SourceSender2,
          typename CompletionSender2,
          typename Receiver2,
          typename... Values>
      friend struct _value_receiver;

      template <
          typename SourceSender2,
          typename CompletionSender2,
          typename Receiver2,
          typename Error>
      friend struct _error_receiver;

      template <typename... Values>
      using value_operation = connect_result_t<
          CompletionSender,
          value_receiver<SourceSender, CompletionSender, Receiver, Values...>>;

      template <typename Error>
      using error_operation = connect_result_t<
          CompletionSender,
          error_receiver<SourceSender, CompletionSender, Receiver, Error>>;

      template <typename... Errors>
      using error_operation_union =
          manual_lifetime_union<
            error_operation<std::exception_ptr>,
            error_operation<Errors>...>;

      using done_operation = connect_result_t<
          CompletionSender,
          done_receiver<SourceSender, CompletionSender, Receiver>>;

      template <typename... Errors>
      using error_result_union =
        manual_lifetime_union<std::exception_ptr, Errors...>;

    public:
      template <typename CompletionSender2, typename Receiver2>
      explicit type(
          SourceSender&& sourceSender,
          CompletionSender2&& completionSender,
          Receiver2&& r)
        : completionSender_(static_cast<CompletionSender2&&>(completionSender))
        , receiver_(static_cast<Receiver2&&>(r)) {
        sourceOp_.construct_from([&] {
          return unifex::connect(
              static_cast<SourceSender&&>(sourceSender),
              receiver_t<SourceSender, CompletionSender, Receiver>{this});
        });
      }

      ~type() {
        if (!started_) {
          sourceOp_.destruct();
        }
      }

      void start() & noexcept {
        assert(!started_);
        started_ = true;
        unifex::start(sourceOp_.get());
      }

    private:
      UNIFEX_NO_UNIQUE_ADDRESS remove_cvref_t<CompletionSender> completionSender_;
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
      bool started_ = false;

      // Result storage.
      union {
        // Storage for error-types that might be produced by SourceSender.
        UNIFEX_NO_UNIQUE_ADDRESS
        typename remove_cvref_t<SourceSender>::template error_types<error_result_union>
            error_;

        // Storage for value-types that might be produced by SourceSender.
        UNIFEX_NO_UNIQUE_ADDRESS typename remove_cvref_t<SourceSender>::template value_types<
            manual_lifetime_union,
            decayed_tuple<std::tuple>::template apply>
            value_;
      };

      // Operation storage.
      union {
        // Storage for the source operation state.
        manual_lifetime<connect_result_t<
            SourceSender,
            receiver_t<SourceSender, CompletionSender, Receiver>>>
            sourceOp_;

        // Storage for the completion operation for the case where
        // the source operation completed with a value.
        typename remove_cvref_t<SourceSender>::
            template value_types<manual_lifetime_union, value_operation>
                completionValueOp_;

        // Storage for the completion operation for the case where the
        // source operation completed with an error.
        typename remove_cvref_t<SourceSender>::template error_types<error_operation_union>
            completionErrorOp_;

        // Storage for the completion operation for the case where the
        // source operation completed with 'done'.
        manual_lifetime<done_operation> completionDoneOp_;
      };
    };

    template <typename SourceSender, typename CompletionSender>
    struct _sender {
      class type;
    };
    template <typename SourceSender, typename CompletionSender>
    using sender = typename _sender<
        remove_cvref_t<SourceSender>,
        remove_cvref_t<CompletionSender>>::type;

    template <typename SourceSender, typename CompletionSender>
    class _sender<SourceSender, CompletionSender>::type {
      using sender = type;
    public:
      template <
          template <typename...> class Variant,
          template <typename...> class Tuple>
      using value_types = typename SourceSender::
          template value_types<Variant, decayed_tuple<Tuple>::template apply>;

      // This can produce any of the error_types of SourceSender, or of
      // CompletionSender or an exception_ptr corresponding to an exception thrown
      // by the copy/move of the value result.
      // TODO: In theory we could eliminate exception_ptr in the case that the
      // connect() operation and move/copy of values
      template <template <typename...> class Variant>
      using error_types = typename concat_type_lists_unique_t<
          typename SourceSender::template error_types<
              decayed_tuple<type_list>::template apply>,
          typename CompletionSender::template error_types<
              decayed_tuple<type_list>::template apply>,
          type_list<std::exception_ptr>>::template apply<Variant>;

      template <typename SourceSender2, typename CompletionSender2>
      explicit type(
          SourceSender2&& source, CompletionSender2&& completion)
          noexcept(std::is_nothrow_constructible_v<SourceSender, SourceSender2> &&
              std::is_nothrow_constructible_v<CompletionSender, CompletionSender2>)
        : source_(static_cast<SourceSender2&&>(source))
        , completion_(static_cast<CompletionSender2&&>(completion)) {}

    private:

      // TODO: Also constrain these methods to check that the CompletionSender
      // is connectable to any of the instantiations of done/value/error_receiver
      // that could be created for each of the results that SourceSender might
      // complete with. For now we just check done_receiver as an approximation.

      template(typename CPO, typename S, typename Receiver)
        (requires
          same_as<CPO, tag_t<connect>> AND
          same_as<remove_cvref_t<S>, sender> AND
          sender_to<
            member_t<S, SourceSender>,
            receiver_t<
              member_t<S, SourceSender>,
              CompletionSender,
              remove_cvref_t<Receiver>>> AND
          sender_to<
            CompletionSender,
            done_receiver<
              member_t<S, SourceSender>,
              CompletionSender,
              remove_cvref_t<Receiver>>>)
      friend auto tag_invoke(CPO, S&& s, Receiver&& r)
          -> operation<member_t<S, SourceSender>, CompletionSender, Receiver> {
        return operation<member_t<S, SourceSender>, CompletionSender, Receiver>{
                static_cast<S&&>(s).source_,
                static_cast<S&&>(s).completion_,
                static_cast<Receiver&&>(r)};
      }

      SourceSender source_;
      CompletionSender completion_;
    };
  }  // namespace _final

  namespace _final_cpo
  {
    inline const struct _fn {
      template <typename SourceSender, typename CompletionSender>
      auto operator()(SourceSender&& source, CompletionSender&& completion) const
          noexcept(std::is_nothrow_constructible_v<
                  _final::sender<SourceSender, CompletionSender>,
                  SourceSender,
                  CompletionSender>) -> _final::sender<SourceSender, CompletionSender> {
        return _final::sender<SourceSender, CompletionSender>{
            static_cast<SourceSender&&>(source),
            static_cast<CompletionSender&&>(completion)};
      }
    } finally{};
  } // namespace _final_cpo

using _final_cpo::finally;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
