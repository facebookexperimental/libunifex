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

#include <cassert>
#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace unifex
{
  namespace detail
  {
    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver>
    class finally_operation;

    template <
        typename SourceSender,
        typename CompletionSender,
        typename Receiver,
        typename... Values>
    class finally_value_receiver {
      using operation_type =
          finally_operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit finally_value_receiver(operation_type* op) noexcept : op_(op) {}

      finally_value_receiver(finally_value_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionValueOp_.template get<
            operation_t<CompletionSender, finally_value_receiver>>();
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
            operation_t<CompletionSender, finally_value_receiver>>();
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
            operation_t<CompletionSender, finally_value_receiver>>();
        completionOp.destruct();

        // Discard the stored value.
        auto& valueStorage = op->value_.template get<std::tuple<Values...>>();
        valueStorage.destruct();

        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const finally_value_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver(),
            static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const finally_value_receiver& r,
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
    class finally_error_receiver {
      using operation_type =
          finally_operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit finally_error_receiver(operation_type* op) noexcept : op_(op) {}

      finally_error_receiver(finally_error_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionErrorOp_.template get<
            operation_t<CompletionSender, finally_error_receiver>>();
        completionOp.destruct();

        auto& errorStorage = op->error_.template get<Error>();
        Error errorCopy = static_cast<Error&&>(errorStorage.get());
        errorStorage.destruct();

        unifex::set_error(
            static_cast<Receiver&&>(op->receiver_),
            static_cast<Error&&>(errorCopy));
      }

      template <
          typename OtherError,
          std::enable_if_t<
              std::is_invocable_v<
                  decltype(unifex::set_error),
                  Receiver,
                  OtherError>,
              int> = 0>
      void set_error(OtherError otherError) && noexcept {
        auto* op = op_;

        auto& completionOp = op->completionErrorOp_.template get<
            operation_t<CompletionSender, finally_error_receiver>>();
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
            operation_t<CompletionSender, finally_error_receiver>>();
        completionOp.destruct();

        // Discard existing stored error from source-sender.
        auto& errorStorage = op->error_.template get<Error>();
        errorStorage.destruct();

        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const finally_error_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver(),
            static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const finally_error_receiver& r,
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
        typename Receiver>
    class finally_done_receiver {
      using operation_type =
          finally_operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit finally_done_receiver(operation_type* op) noexcept : op_(op) {}

      finally_done_receiver(finally_done_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      void set_value() && noexcept {
        auto* op = op_;
        op->completionDoneOp_.destruct();
        unifex::set_done(static_cast<Receiver&&>(op->receiver_));
      }

      template <
          typename Error,
          std::enable_if_t<
              std::is_invocable_v<decltype(unifex::set_error), Receiver, Error>,
              int> = 0>
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

      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto tag_invoke(
          CPO cpo,
          const finally_done_receiver& r,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           CPO,
                                           const Receiver&,
                                           Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver(),
            static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>,
          const finally_done_receiver& r,
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
        typename Receiver>
    class finally_receiver {
      using operation_type =
          finally_operation<SourceSender, CompletionSender, Receiver>;

    public:
      explicit finally_receiver(operation_type* op) noexcept : op_(op) {}

      finally_receiver(finally_receiver&& other) noexcept
        : op_(std::exchange(other.op_, nullptr)) {}

      template <
          typename... Values,
          std::enable_if_t<
              std::is_invocable_v<
                  decltype(unifex::set_value),
                  Receiver,
                  std::decay_t<Values>...>,
              int> = 0>
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
          using value_receiver = finally_value_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              std::decay_t<Values>...>;
          auto& completionOp =
              op->completionValueOp_
                  .template get<operation_t<CompletionSender, value_receiver>>()
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
          using error_receiver = finally_error_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              std::decay_t<Error>>;
          auto& completionOp =
              op->completionErrorOp_
                  .template get<operation_t<CompletionSender, error_receiver>>()
                  .construct_from([&] {
                    return unifex::connect(
                        static_cast<CompletionSender&&>(op->completionSender_),
                        error_receiver{op});
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
              finally_done_receiver<SourceSender, CompletionSender, Receiver>;
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

      template <
          typename CPO,
          typename... Args,
          std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
      friend auto
      tag_invoke(CPO cpo, const finally_receiver& r, Args&&... args) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&, Args...>)
          -> std::invoke_result_t<CPO, const Receiver&, Args...> {
        return static_cast<CPO&&>(cpo)(
            r.get_receiver(),
            static_cast<Args&&>(args)...);
      }

      template <typename Func>
      friend void tag_invoke(
          tag_t<visit_continuations>, const finally_receiver& r, Func&& func) {
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
    class finally_operation {
      friend class finally_receiver<SourceSender, CompletionSender, Receiver>;

      template <
          typename SourceSender2,
          typename CompletionSender2,
          typename Receiver2,
          typename... Values>
      friend class finally_value_receiver;

      template <
          typename SourceSender2,
          typename CompletionSender2,
          typename Receiver2,
          typename Error>
      friend class finally_error_receiver;

      template <
          typename SourceSender2,
          typename CompletionSender2,
          typename Receiver2>
      friend class finally_done_receiver;

      template <typename... Values>
      using value_operation = operation_t<
          CompletionSender,
          finally_value_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              std::decay_t<Values>...>>;

      template <typename Error>
      using error_operation = operation_t<
          CompletionSender,
          finally_error_receiver<
              SourceSender,
              CompletionSender,
              Receiver,
              std::decay_t<Error>>>;

      template <typename... Errors>
      using error_operation_union =
          manual_lifetime_union<
            error_operation<std::exception_ptr>,
            error_operation<Errors>...>;

      using done_operation = operation_t<
          CompletionSender,
          finally_done_receiver<SourceSender, CompletionSender, Receiver>>;

      template <typename... Errors>
      using error_result_union =
        manual_lifetime_union<std::exception_ptr, Errors...>;

    public:
      template <typename CompletionSender2, typename Receiver2>
      explicit finally_operation(
          SourceSender&& sourceSender,
          CompletionSender2&& completionSender,
          Receiver2&& receiver)
        : completionSender_(static_cast<CompletionSender2&&>(completionSender))
        , receiver_(static_cast<Receiver2&&>(receiver)) {
        sourceOp_.construct_from([&] {
          return unifex::connect(
              static_cast<SourceSender&&>(sourceSender),
              finally_receiver<SourceSender, CompletionSender, Receiver>{this});
        });
      }

      ~finally_operation() {
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
      UNIFEX_NO_UNIQUE_ADDRESS CompletionSender completionSender_;
      UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
      bool started_ = false;

      // Result storage.
      union {
        // Storage for error-types that might be produced by ValueSender.
        UNIFEX_NO_UNIQUE_ADDRESS
        typename SourceSender::template error_types<error_result_union>
            error_;

        // Storage for value-types that might be produced by ValueSender.
        UNIFEX_NO_UNIQUE_ADDRESS typename SourceSender::template value_types<
            manual_lifetime_union,
            decayed_tuple<std::tuple>::template apply>
            value_;
      };

      // Operation storage.
      union {
        // Storage for the source operation state.
        manual_lifetime<operation_t<
            SourceSender,
            finally_receiver<SourceSender, CompletionSender, Receiver>>>
            sourceOp_;

        // Storage for the completion operation for the case where
        // the source operation completed with a value.
        typename SourceSender::
            template value_types<manual_lifetime_union, value_operation>
                completionValueOp_;

        // Storage for the completion operation for the case where the
        // source operation completed with an error.
        typename SourceSender::template error_types<error_operation_union>
            completionErrorOp_;

        // Storage for the completion operation for the case where the
        // source operation completed with 'done'.
        manual_lifetime<done_operation> completionDoneOp_;
      };
    };
  }  // namespace detail

  template <typename SourceSender, typename CompletionSender>
  class finally_sender {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
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
    explicit finally_sender(
        SourceSender2&& source, CompletionSender2&& completion)
      : source_(static_cast<SourceSender2&&>(source))
      , completion_(static_cast<CompletionSender2&&>(completion)) {}

    // TODO: Also constrain this method to check that the CompletionSender
    // is connectable to any of the instantiations of finally_done/value/error_receiver
    // that could be created for each of the results that SourceSender might
    // complete with. For now we just check finally_done_receiver as an approximation.
    template <
        typename Receiver,
        std::enable_if_t<
            is_connectable_v<
                SourceSender,
                detail::finally_receiver<
                    SourceSender,
                    CompletionSender,
                    Receiver>> &&
            is_connectable_v<
                CompletionSender,
                detail::finally_done_receiver<
                    SourceSender,
                    CompletionSender,
                    Receiver>>,
            int> = 0>
    friend auto tag_invoke(tag_t<connect>, finally_sender&& s, Receiver&& r)
        -> detail::finally_operation<SourceSender, CompletionSender, Receiver> {
      return detail::
          finally_operation<SourceSender, CompletionSender, Receiver>{
              static_cast<SourceSender&&>(s.source_),
              static_cast<CompletionSender&&>(s.completion_),
              static_cast<Receiver&&>(r)};
    }

  private:
    SourceSender source_;
    CompletionSender completion_;
  };

  inline constexpr struct finally_cpo {
    template <
        typename SourceSender,
        typename CompletionSender,
        typename FinallySender = finally_sender<
            std::decay_t<SourceSender>,
            std::decay_t<CompletionSender>>>
    auto operator()(SourceSender&& source, CompletionSender&& completion) const
        noexcept(std::is_nothrow_constructible_v<
                 FinallySender,
                 SourceSender,
                 CompletionSender>) -> FinallySender {
      return FinallySender{
          static_cast<SourceSender&&>(source),
          static_cast<CompletionSender&&>(completion)};
    }
  } finally;
}  // namespace unifex
