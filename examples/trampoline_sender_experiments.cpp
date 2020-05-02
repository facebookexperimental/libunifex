#include <unifex/tag_invoke.hpp>
#include <unifex/continuations.hpp>
#include <unifex/type_traits.hpp>

#include <cstdio>

//////////////////////////////////////////////////////////
// Concepts
//
// Sender   - Represents an invocable that produces its result
//            via a callback.
//
// Receiver - Represents a callback that will receive the result
//            of some potentially asynchronous operation.
//            This represents an overload set of multiple possible
//            paths that the consumer of an operation could take
//            depending on the result of the operation.
//            This is the equivalent of the frame-pointer, return-address
//            and exception-table entries in normal function calls.
//
// Continuation - Represents a single chosen continuation of some
//            asynchronous operation. The result of this operation has
//            been constructed already. Continuations are single-shot
//            and can either be executed (by calling run_continuation())
//            or aborted (by calling .destroy()).
//            
// OperationState - Stores state for an asynchronous operation
//            for the lifetime of that operation. Equivalent of
//            a stack-frame for ordinary functions.
//
// Continuation-Passing-Style Calling Convention
// ---------------------------------------------
//
// The general form for the receiver-completion methods is as follows:
//    set_xxx(Receiver, CleanupReceiver, Args...) -> continuation_or_operation_state
//
// This represents a tail-call-compatible, async calling convention, similar
// to that of the generalised coroutines design mentioned in P1745R0.
// 
// The 'Receiver' represents the callback being invoked.
//
// The 'CleanupReceiver' represents the callback to invoke with the result of the
// operation when it completes (ie. the set of possible continuations of the invocation).
// It is roughly equivalent to the implicit return-address/frame-pointer and exception
// unwind tables used when making ordinary function-calls on the stack.
//
// If the invocation of Receiver is stateless then it may return a continuation handle.
//
// If the invocation of Receiver is stateful then it will return an operation-state
// object that the caller must call '.start()' on to obtain the continuation-handle.
// If this is the case then the caller must keep the returned operation-state object
// alive until one of the receiver methods is invoked on the CleanupReceiver.
//
// Receiver Channels
// -----------------
// The  basic receiver methods are as follows:
//  - set_value(Receiver, CleanupReceiver, Values...) -> continuation_or_operation_state
//  - set_error(Receiver, CleanupReceiver, Error) -> continuation_or_operation_state
//  - set_done(Receiver, CleanupReceiver)  -> continuation_or_operation_state
//
// These methods are generally used to construct the result in the correct place
// and defer the actual processing of that result until the returned continuation
// is executed. This is to allow for the ability to cancel a continuation by calling
// the .destroy() method. Doing this allows you to call another method on the
// receiver to create a new continuation in its place, which is important for
// handling errors during unwind after previously constructing a value result.
//
// TODO: Not sure if we should allow cancellation of set_error() and set_done()
// continuations?
// e.g. if we had a hypothetical 'co_error' keyword that completed the current
// coroutine with an error, what would the behaviour be during unwind if one
// of the async cleanup operations completed with an error? Should it terminate?
//
//   auto example() [->] task {
//     object_with_throwing_destructor x;
//     co_error some_error{}; // Constructs some_error() object in scope of
//                            // consumer() and then starts an unwind.
//                            // Will run consumer()'s continuation when
//                            // unwind completes. But what if unwind throws
//                            // an exception?
//   }
//
//   auto consumer() [->] task {
//     co_await example();
//   }
//
// Async RVO
// ---------
// There are also some equivalent CPOs that can be used to support async RVO
// by passing callable objects that will produce the values lazily instead of
// passing the values themselves.
//
// This allows the receiver to construct the values in-place in their final
// location (through use of guaranteed copy-elision of function return-values)
// rather than having to copy the parameters. It also allows receivers to more
// efficiently discard input values if they will be unused, by simply not
// calling the factory function that produces the value.
//
// The producer can choose to call either the set_xxx() or the set_xxx_from()
// method.
//
// It will generally be more efficient for a producer to call set_xxx() if it
// already has a value and will be more efficient to call set_xxx_from() if
// it is about to create a value to pass.
//
// The receiver can choose to implement either set_xxx() or set_xxx_from() or
// both, depending on how it will use the values. If a receiver only provides
// one of the set_xxx/set_xxx_from operations then it will get a default
// implementation of the other one that forwards through to the one that was
// implemented.
//
// Terminating the Recursion
// -------------------------
// When an operation has no more work to do after the completion of the operation
// then it can pass the 'noop_cleanup_receiver' as the CleanupReceiver parameter.
//
// This is typically used by senders when delivering the final result, indicating
// that any async-cleanup work is done and that the operation-state is free to
// be destroyed.
//
// Generally, the consumer of an operation will customise set_done() and set_error()
// specificaly to handle a CleanupReceiver of type 'noop_cleanup_receiver' and will
// return 
//
// Typical Sender-Operation
// ========================
//
// When starting an operation we call:
//
//    connect(sender, resultReceiver) -> operation-state
//    start(operation-state) -> continuation
//
// When the sender produces a result it calls:
//
//    set_value(resultReceiver, cleanupReceiver, values...)
//      -> operation-state-or-continuation
//
// The continuation returned from this will execute the processing
// of the result.
// This can be thought of "entering the async-scope of the result".
//
// When the consumer is finished processing the value and wants to
// exit the async scope it calls:
//
//    set_done(cleanupReceiver, cleanupDoneReceiver)
//       -> operation-state-or-continuation
//
// This returns a continuation that will start the async cleanup
// of the producer's operation.
//
// When the async cleanup completes, the producer will call:
//
//    set_done(cleanupDoneReceiver, noop_cleanup_receiver{})
//       -> continuation
//
// The implementation of the cleanupDoneReceiver's set_done() method
// is responsible for destroying the sender's operation-state object,
// typically before continuing. It should return a continuation (not
// an operation-state) as there will not be anywhere to store state
// in the producer's operation-state as it is about to be desroyed.
//
// TODO: Investigate possibility of colleapsing connect() on a sender
// into set_value(), allowing caller to provide extra args without
// having to copy them into a sender first.

//////////////////////////////////////////////////////////
// CPOs

#include <unifex/detail/prologue.hpp>

namespace _connect
{
    struct _fn {
        template(typename Sender, typename Receiver)
            (requires unifex::tag_invocable<_fn, Sender, Receiver>)
        auto operator()(Sender&& s, Receiver&& r) const
            -> unifex::tag_invoke_result_t<_fn, Sender, Receiver> {
            return unifex::tag_invoke(*this, (Sender&&)s, (Receiver&&)r);
        }
    };
}

inline constexpr _connect::_fn connect{};

namespace _set_value
{
    struct _fn;
}

namespace _set_value_from
{
    struct _fn {
        template(
            typename Receiver,
            typename CleanupReceiver,
            typename... ValueFactories)
            (requires
                unifex::tag_invocable<_fn, Receiver, CleanupReceiver, ValueFactories...> AND
                (unifex::invocable<ValueFactories> && ...))
        auto operator()(Receiver&& r, CleanupReceiver&& cr, ValueFactories&&... valueFactories) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, CleanupReceiver, ValueFactories...>)
            -> unifex::tag_invoke_result_t<_fn, Receiver, CleanupReceiver, ValueFactories...> {
            return unifex::tag_invoke(*this, (Receiver&&)r, (CleanupReceiver&&)cr, (ValueFactories&&)valueFactories...);
        }

        template(
            typename Receiver,
            typename CleanupReceiver,
            typename... ValueFactories,
            typename SetValue = _set_value::_fn)
            (requires
                (!unifex::tag_invocable<_fn, Receiver, CleanupReceiver, ValueFactories...>) AND
                (unifex::invocable<ValueFactories&> && ...) AND
                (unifex::tag_invocable<SetValue, Receiver, CleanupReceiver, unifex::callable_result_t<ValueFactories&>...>))
        auto operator()(Receiver&& r, CleanupReceiver&& cr, ValueFactories&&... valueFactories) const
            noexcept(
                (unifex::is_nothrow_callable_v<ValueFactories> && ...) &&
                unifex::is_nothrow_tag_invocable_v<_set_value::_fn, Receiver, CleanupReceiver, unifex::callable_result_t<ValueFactories&>...>)
            -> unifex::tag_invoke_result_t<_set_value::_fn, Receiver, CleanupReceiver, unifex::callable_result_t<ValueFactories&>...> {
            return unifex::tag_invoke(
                SetValue{},
                (Receiver&&)r,
                (CleanupReceiver&&)cr,
                valueFactories()...);
        }
    };
}

template<typename Storage, typename T = unifex::remove_cvref_t<Storage>>
struct _value_factory {
    Storage value;

    T operator()() noexcept(std::is_nothrow_constructible_v<T, Storage>) {
        return static_cast<Storage&&>(value);
    }
};

inline constexpr _set_value_from::_fn set_value_from{};

namespace _set_value
{
    struct _fn {
        template(typename Receiver, typename CleanupReceiver, typename... Values)
            (requires
                unifex::tag_invocable<_fn, Receiver, CleanupReceiver, Values...>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Values&&... values) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, CleanupReceiver, Values...>)
            -> unifex::tag_invoke_result_t<_fn, Receiver, CleanupReceiver, Values...> {
            return unifex::tag_invoke(_fn{}, (Receiver&&)r, (CleanupReceiver&&)cr, (Values&&)values...);
        }

        template(typename Receiver, typename CleanupReceiver, typename... Values)
            (requires
                (!unifex::tag_invocable<_fn, Receiver, CleanupReceiver, Values...>) AND
                (unifex::constructible_from<unifex::remove_cvref_t<Values>, Values> && ...) AND
                unifex::tag_invocable<_set_value_from::_fn, Receiver, CleanupReceiver, _value_factory<Values&&>...>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Values&&... values) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_set_value_from::_fn, Receiver, CleanupReceiver, _value_factory<Values&&>...>)
            -> unifex::tag_invoke_result_t<_set_value_from::_fn, Receiver, CleanupReceiver, _value_factory<Values&&>...> {
            return unifex::tag_invoke(
                _set_value_from::_fn{},
                (Receiver&&)r,
                (CleanupReceiver&&)cr,
                _value_factory<Values&&>{(Values&&)values}...);
        }
    };
}

inline constexpr _set_value::_fn set_value{};

namespace _set_error
{
    struct _fn;
}

namespace _set_error_from
{
    struct _fn {
        template(typename Receiver, typename CleanupReceiver, typename ErrorFactory)
            (requires
                unifex::tag_invocable<_fn, Receiver, CleanupReceiver, ErrorFactory> AND
                unifex::invocable<ErrorFactory>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, ErrorFactory&& errorFactory) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, CleanupReceiver, ErrorFactory>)
            -> unifex::tag_invoke_result_t<_fn, Receiver, CleanupReceiver, ErrorFactory> {
            return unifex::tag_invoke(_fn{}, (Receiver&&)r, (CleanupReceiver&&)cr, (ErrorFactory&&)errorFactory);
        }

        template(typename Receiver, typename CleanupReceiver, typename ErrorFactory, typename SetError = _set_error::_fn)
            (requires
                (!unifex::tag_invocable<_fn, Receiver, CleanupReceiver, ErrorFactory>) AND
                unifex::invocable<ErrorFactory> AND
                unifex::tag_invocable<SetError, Receiver, CleanupReceiver, unifex::callable_result_t<ErrorFactory>>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, ErrorFactory&& errorFactory) const
            noexcept(unifex::is_nothrow_tag_invocable_v<SetError, Receiver, CleanupReceiver, unifex::callable_result_t<ErrorFactory&>>)
            -> unifex::tag_invoke_result_t<SetError, Receiver, CleanupReceiver, unifex::callable_result_t<ErrorFactory&>> {
            return unifex::tag_invoke(SetError{}, (Receiver&&)r, (CleanupReceiver&&)cr, errorFactory());
        }
    };
}

inline constexpr _set_error_from::_fn set_error_from{};

namespace _set_error
{
    struct _fn {
        template(typename Receiver, typename CleanupReceiver, typename Error)
            (requires
                unifex::tag_invocable<_fn, Receiver, CleanupReceiver, Error>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Error&& error) const noexcept
            -> unifex::tag_invoke_result_t<_fn, Receiver, CleanupReceiver, Error> {
            static_assert(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, Error>);
            return unifex::tag_invoke(*this, (Receiver&&)r, (Error&&)error);
        }

        template(typename Receiver, typename CleanupReceiver, typename Error)
            (requires
                (!unifex::tag_invocable<_fn, Receiver, CleanupReceiver, Error>) AND
                (unifex::tag_invocable<_set_error_from::_fn, CleanupReceiver, _value_factory<Error&&>>))
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Error&& error) const noexcept
            -> unifex::tag_invoke_result_t<_set_error_from::_fn, Receiver, CleanupReceiver, _value_factory<Error&&>> {
            static_assert(std::is_nothrow_constructible_v<unifex::remove_cvref_t<Error>, Error>);
            static_assert(unifex::is_nothrow_tag_invocable_v<_set_error_from::_fn, Receiver, _value_factory<Error&&>>);
            return unifex::tag_invoke(_set_error_from::_fn{}, (Receiver&&)r, (CleanupReceiver&&)cr, _value_factory<Error&&>{(Error&&)error});
        }
    };
}

inline constexpr _set_error::_fn set_error{};

namespace _set_done
{
    struct _fn {
        template(typename Receiver, typename CleanupReceiver)
            (requires
                unifex::tag_invocable<_fn, Receiver, CleanupReceiver>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr) const noexcept
            -> unifex::tag_invoke_result_t<_fn, Receiver, CleanupReceiver> {
            static_assert(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, CleanupReceiver>);
            return unifex::tag_invoke(*this, (Receiver&&)r, (CleanupReceiver&&)cr);
        }
    };
}

inline constexpr _set_done::_fn set_done{};

namespace _start
{
    struct _fn {
        template<typename OpState>
        auto operator()(OpState& op) const
            -> unifex::tag_invoke_result_t<_fn, OpState&> {
            return unifex::tag_invoke(*this, op);
        }
    };
}

inline constexpr _start::_fn start{};

template<typename... Args>
using set_value_result_t = unifex::callable_result_t<decltype(set_value), Args&&...>;

template<typename... Args>
using set_value_from_result_t = unifex::callable_result_t<decltype(set_value_from),  Args&&...>;

template<typename... Args>
using set_error_result_t = unifex::callable_result_t<decltype(set_error), Args&&...>;

template<typename... Args>
using set_error_from_result_t = unifex::callable_result_t<decltype(set_error_from), Args&&...>;

template<typename... Args>
using set_done_result_t = unifex::callable_result_t<decltype(set_done), Args&&...>;

template<typename Op>
using start_result_t = unifex::callable_result_t<decltype(start), Op&>;

template<typename T>
inline constexpr bool is_operation_state_v =
    std::is_nothrow_destructible_v<T> &&
    unifex::is_callable_v<decltype(start), T&>;

struct noop_cleanup_receiver {
    template(typename CleanupReceiver)
        (requires (!std::is_same_v<CleanupReceiver, noop_cleanup_receiver>))
    friend auto tag_invoke(
        unifex::tag_t<::set_done>,
        noop_cleanup_receiver,
        CleanupReceiver&& cr) noexcept
        -> set_done_result_t<CleanupReceiver, noop_cleanup_receiver> {
        return ::set_done((CleanupReceiver&&)cr, noop_cleanup_receiver{});
    }

    template(typename CleanupReceiver, typename Error)
        (requires (!std::is_same_v<CleanupReceiver, noop_cleanup_receiver>))
    friend auto tag_invoke(
        unifex::tag_t<::set_error>,
        noop_cleanup_receiver,
        CleanupReceiver&& cr,
        Error&& e) noexcept
        -> set_error_result_t<CleanupReceiver, noop_cleanup_receiver, Error> {
        return ::set_error((CleanupReceiver&&)cr, noop_cleanup_receiver{}, (Error&&)e);
    }

    template(typename CleanupReceiver, typename ErrorFactory)
        (requires (!std::is_same_v<CleanupReceiver, noop_cleanup_receiver>))
    friend auto tag_invoke(
        unifex::tag_t<::set_error_from>,
        noop_cleanup_receiver,
        CleanupReceiver&& cr,
        ErrorFactory&& ef) noexcept
        -> set_error_from_result_t<CleanupReceiver, noop_cleanup_receiver, ErrorFactory> {
        return ::set_error_from((CleanupReceiver&&)cr, noop_cleanup_receiver{}, (ErrorFactory&&)ef);
    }
};

///////////////////////////////////////////////////////////////////////
// Helpers

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
class stateless_set_result_operation_storage {
public:
    using continuation_type = unifex::callable_result_t<decltype(CPO), ResultReceiver, CleanupReceiver, Args...>;

    void destroy() noexcept {}

    continuation_type start(ResultReceiver&& r, CleanupReceiver&& cr, Args&&... args)
        noexcept(unifex::is_nothrow_callable_v<decltype(CPO), ResultReceiver, CleanupReceiver, Args...>) {
        return CPO((ResultReceiver&&)r, (CleanupReceiver&&)cr, (Args&&)args...);
    }
};

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
class stateful_set_result_operation_storage {
    using op_t = unifex::callable_result_t<decltype(CPO), ResultReceiver, CleanupReceiver, Args...>;
    UNIFEX_NO_UNIQUE_ADDRESS unifex::manual_lifetime<op_t> opStorage_;

public:
    using continuation_type = start_result_t<op_t>;

    void destroy() noexcept {
        opStorage_.destruct();
    }

    continuation_type start(ResultReceiver&& r, CleanupReceiver&& cr, Args&&... args)
            noexcept(unifex::is_nothrow_callable_v<decltype(CPO), ResultReceiver, CleanupReceiver, Args...>) {
        auto& op = opStorage_.construct_from([&]() -> op_t {
            return CPO((ResultReceiver&&)r, (CleanupReceiver&&)cr, (Args&&)args...);
        });

        // TODO: Do we need to handle this start() call potentialy throwing?
        return ::start(op);
    }
};

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
using set_result_operation_storage_t =
    std::conditional_t<
        is_operation_state_v<
            unifex::callable_result_t<decltype(CPO), ResultReceiver, CleanupReceiver, Args...>>,
        stateful_set_result_operation_storage<CPO, ResultReceiver, CleanupReceiver, Args...>,
        stateless_set_result_operation_storage<CPO, ResultReceiver, CleanupReceiver, Args...>>;

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
struct set_result_operation_storage {
    using type = set_result_operation_storage_t<CPO, ResultReceiver, CleanupReceiver, Args...>;
};

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
using set_result_continuation_t = decltype(
    UNIFEX_DECLVAL(set_result_operation_storage_t<CPO, ResultReceiver, CleanupReceiver, Args...>&).start(
        UNIFEX_DECLVAL(ResultReceiver),
        UNIFEX_DECLVAL(CleanupReceiver),
        UNIFEX_DECLVAL(Args)...));

template<auto& CPO, typename ResultReceiver, typename CleanupReceiver, typename... Args>
inline constexpr bool is_set_result_nothrow_v =
    noexcept(UNIFEX_DECLVAL(set_result_operation_storage_t<CPO, ResultReceiver, CleanupReceiver, Args...>&).start(
        UNIFEX_DECLVAL(ResultReceiver),
        UNIFEX_DECLVAL(CleanupReceiver),
        UNIFEX_DECLVAL(Args)...));

template<auto& CPO, typename ResultReceiver, typename... Args>
struct set_result_no_cleanup_operation_storage {
    struct cleanup_receiver {
        set_result_no_cleanup_operation_storage* op_;

        template<typename CleanupDoneReceiver>
        friend auto tag_invoke(
            unifex::tag_t<::set_done>,
            cleanup_receiver&& r,
            CleanupDoneReceiver&& cr) noexcept
            -> set_done_result_t<CleanupDoneReceiver, noop_cleanup_receiver> {
            r.op_->resultOp_.destroy();
            return ::set_done(std::move(cr), noop_cleanup_receiver{});
        }

        template<typename CleanupDoneReceiver, typename Error>
        friend auto tag_invoke(
            unifex::tag_t<::set_error>,
            cleanup_receiver&& r,
            CleanupDoneReceiver&& cr,
            Error&& e) noexcept
            -> set_error_result_t<CleanupDoneReceiver, noop_cleanup_receiver, Error> {
            r.op_->resultOp_.destroy();
            return ::set_error((CleanupDoneReceiver&&)cr, noop_cleanup_receiver{}, (Error&&)e);
        }

        template<typename CleanupDoneReceiver, typename ErrorFactory>
        friend auto tag_invoke(
            unifex::tag_t<::set_error_from>,
            cleanup_receiver&& r,
            CleanupDoneReceiver&& cr,
            ErrorFactory&& ef) noexcept
            -> set_error_from_result_t<CleanupDoneReceiver, noop_cleanup_receiver, ErrorFactory> {
            r.op_->resultOp_.destroy();
            return ::set_error_from((CleanupDoneReceiver&&)cr, noop_cleanup_receiver{}, (ErrorFactory&&)ef);
        }
    };

    using storage_t = set_result_operation_storage_t<CPO, ResultReceiver, cleanup_receiver, Args...>;
    storage_t resultOp_;

public:
    using continuation_type = typename storage_t::continuation_type;

    auto start(ResultReceiver&& r, Args&&... args)
        noexcept(is_set_result_nothrow_v<CPO, ResultReceiver, cleanup_receiver, Args...>)
        -> continuation_type {
        return resultOp_.start((ResultReceiver&&)r, cleanup_receiver{this}, (Args&&)args...);
    }
};

///////////////////////////////////////////////////////////////////////
// just()
//
// Equivalent of the following coroutines-v2 function:
//
// auto just(auto... values) [->] task< {
//   co_return...{ std::move(values), ... };
// }
//

template<typename... Values>
class just_sender {
    std::tuple<Values...> values_;

public:
    template<typename... Values2>
    explicit just_sender(std::in_place_t, Values2&&... values)
    : values_((Values2&&)values...)
    {}

    template(typename Self, typename ResultReceiver)
        (requires
            unifex::same_as<std::remove_cvref_t<Self>, just_sender> AND
            unifex::invocable<decltype(::set_value), ResultReceiver, noop_cleanup_receiver, unifex::member_t<Self, Values>...>)
    friend auto tag_invoke(
            unifex::tag_t<connect>,
            Self&& self,
            ResultReceiver&& r)
            noexcept(unifex::is_nothrow_callable_v<decltype(::set_value), ResultReceiver, noop_cleanup_receiver, unifex::member_t<Self, Values>...>)
            -> set_value_result_t<ResultReceiver, noop_cleanup_receiver, unifex::member_t<Self, Values>...> {
        return std::apply([&](unifex::member_t<Self, Values>&&... values) {
            return ::set_value((ResultReceiver&&)r, noop_cleanup_receiver{}, (unifex::member_t<Self, Values>&&)values...);
        }, static_cast<Self&&>(self).values_);
    }
};

template<typename... Values>
auto just(Values&&... values) -> just_sender<std::decay_t<Values>...> {
    return just_sender<std::decay_t<Values>...>{std::in_place, (Values&&)values...};
}

#include <unifex/detail/epilogue.hpp>

////////////////////////////////////////////////////////////
// async_cleanup_scope

struct suspend_on_cleanup_done {
    friend auto tag_invoke(
        unifex::tag_t<::set_done>,
        suspend_on_cleanup_done,
        noop_cleanup_receiver) noexcept {
        return unifex::noop_continuation;
    }
};

template<typename... Values>
void output(const Values&... values) {}

template<typename F>
struct _invoke_on_conversion {
    F f_;

    operator unifex::callable_result_t<const F&>() const & {
        return f_();
    }
    operator unifex::callable_result_t<F&>() & {
        return f_();
    }
    operator unifex::callable_result_t<const F&&>() const && {
        return static_cast<const F&&>(f_)();
    }
    operator unifex::callable_result_t<F&&>() && {
        return static_cast<F&&>(f_)();
    }
};

template<typename F>
_invoke_on_conversion(F&&) -> _invoke_on_conversion<F&&>;

struct simple_receiver {
    template<typename CleanupReceiver, typename... Values>
    struct process_value_op {
        std::tuple<Values...> values;
        CleanupReceiver cr;

        friend auto tag_invoke(
                unifex::tag_t<::start>,
                process_value_op& self) noexcept {
            try {
                std::printf("got values:\n");
                std::apply([](auto&&... values) {
                    (void)((std::printf("- %i\n", values), false) || ...);
                }, self.values);
            } catch (...) {
                std::printf("error: error processing values.\n");
            }

            // Finished processing the values, start cleanup
            return ::set_done(std::move(self.cr), suspend_on_cleanup_done{});
        }
    };

    template<typename CleanupReceiver, typename... ValueFactories>
    friend process_value_op<std::remove_cvref_t<CleanupReceiver>, unifex::callable_result_t<ValueFactories&>...> tag_invoke(
        unifex::tag_t<::set_value_from>,
        simple_receiver&& r,
        CleanupReceiver&& cr,
        ValueFactories&&... values) {
        return process_value_op<std::remove_cvref_t<CleanupReceiver>, unifex::callable_result_t<ValueFactories>...>{
            { _invoke_on_conversion{values}... },
            (CleanupReceiver&&)cr
        };
    }

    template<typename CleanupReceiver>
    struct process_error_op {
        std::exception_ptr ex;
        CleanupReceiver r;

        friend auto tag_invoke(
            unifex::tag_t<::start>,
            process_error_op& self) noexcept {
            std::printf("error: unknown\n");
            return ::set_done(std::move(self.r), suspend_on_cleanup_done{});
        }
    };

    template<typename CleanupReceiver, typename... Values>
    friend auto tag_invoke(
        unifex::tag_t<::set_error>,
        simple_receiver&& r,
        CleanupReceiver&& cr,
        std::exception_ptr eptr) noexcept {
        return process_error_op<CleanupReceiver>{std::move(eptr), std::move(cr)};
    }
};

static void test() {
    auto a = just(42, 13, 99);
    auto op = ::connect(a, simple_receiver{});
    unifex::run_continuation(::start(op));
}

int main() {
    test();
}
