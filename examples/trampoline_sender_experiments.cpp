#include <unifex/tag_invoke.hpp>
#include <unifex/continuations.hpp>
#include <unifex/type_traits.hpp>

#include <cstdio>

//////////////////////////////////////////////////////////
// Concepts
//
// Sender   - A sender is just a special-case of a receiver that
//            can be invoked using set_value() with no additional arguments.
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
//    ::set_value(sender, resultReceiver) -> operation-state
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
//
// Algorithms as Receivers
// =======================
// An algorithm CPO that would normally have returned a sender can now
// be implemented as a receiver of values.
//
// This allows it to be invoked directly with the arguments instead of
// having to first curry the arguments into a sender.
//
// In this way we can 'async-invoke' an algorithm by calling the
// 'set_value(algorithm, receiver, args...)'
//
// And then we can define 'algorithm(args..)' simply as a mechanism
// for currying 'args' into another algorithm that can either be
// invoked with 'operator()' again to curry more args, or that
// can be 'async-invoked'.
//
// Thus a sender is just an algorithm/receiver that has had enough
// arguments curried so that it is async-invocable with no additional
// arguments.
//
// Note that algorithm receivers are transparent to set_done/set_error
// and immediately reflect those results back to the cleanup-receiver
// passed.

//////////////////////////////////////////////////////////
// CPOs

#include <unifex/detail/prologue.hpp>

template<typename Storage, typename T>
struct _value_factory {
    struct type {
        Storage value;

        T operator()() noexcept(std::is_nothrow_constructible_v<T, Storage>) {
            return static_cast<Storage&&>(value);
        }
    };
};

template<typename Storage, typename T = unifex::remove_cvref_t<Storage>>
using value_factory = typename _value_factory<Storage, T>::type;

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

///////////////////////////////////////////////////
// Receiver interface

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
                unifex::tag_invocable<_set_value_from::_fn, Receiver, CleanupReceiver, value_factory<Values&&>...>)
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Values&&... values) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_set_value_from::_fn, Receiver, CleanupReceiver, value_factory<Values&&>...>)
            -> unifex::tag_invoke_result_t<_set_value_from::_fn, Receiver, CleanupReceiver, value_factory<Values&&>...> {
            return unifex::tag_invoke(
                _set_value_from::_fn{},
                (Receiver&&)r,
                (CleanupReceiver&&)cr,
                value_factory<Values&&>{(Values&&)values}...);
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
                (unifex::tag_invocable<_set_error_from::_fn, CleanupReceiver, value_factory<Error&&>>))
        auto operator()(Receiver&& r, CleanupReceiver&& cr, Error&& error) const noexcept
            -> unifex::tag_invoke_result_t<_set_error_from::_fn, Receiver, CleanupReceiver, value_factory<Error&&>> {
            static_assert(std::is_nothrow_constructible_v<unifex::remove_cvref_t<Error>, Error>);
            static_assert(unifex::is_nothrow_tag_invocable_v<_set_error_from::_fn, Receiver, value_factory<Error&&>>);
            return unifex::tag_invoke(_set_error_from::_fn{}, (Receiver&&)r, (CleanupReceiver&&)cr, value_factory<Error&&>{(Error&&)error});
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

template<typename CPO>
inline constexpr bool is_set_value_cpo_v = unifex::is_one_of_v<
    CPO,
    unifex::tag_t<::set_value>,
    unifex::tag_t<::set_value_from>>;

template<typename CPO>
inline constexpr bool is_set_error_cpo_v = unifex::is_one_of_v<
    CPO,
    unifex::tag_t<::set_error>,
    unifex::tag_t<::set_error_from>>;

template<typename CPO>
inline constexpr bool is_set_done_cpo_v = std::is_same_v<CPO, unifex::tag_t<::set_done>>;

template<typename CPO>
inline constexpr bool is_set_result_cpo_v = unifex::is_one_of_v<
    CPO,
    unifex::tag_t<::set_value>,
    unifex::tag_t<::set_value_from>,
    unifex::tag_t<::set_error>,
    unifex::tag_t<::set_error_from>,
    unifex::tag_t<::set_done>>;

//////////////////////////////
// operation_state interface

namespace _start
{
    struct _fn {
        template(typename OpState)
            (requires unifex::tag_invocable<_fn, OpState>)
        auto operator()(OpState&& op) const noexcept
            -> unifex::tag_invoke_result_t<_fn, OpState> {
            static_assert(unifex::is_nothrow_tag_invocable_v<_fn, OpState>);
            return unifex::tag_invoke(_fn{}, (OpState&&)op);
        }
    };
}

inline constexpr _start::_fn start{};

template<typename Op>
using start_result_t = unifex::callable_result_t<decltype(start), Op&>;

template<typename T>
inline constexpr bool is_operation_state_v =
    std::is_nothrow_destructible_v<T> &&
    unifex::is_callable_v<decltype(start), T&>;

// Check whether we can call start() on an rvalue-operation_state.
// If so then this means we don't need to keep the operation_state
// alive until the receiver completes and so we can place the
// operation-state on the stack.
template<typename T>
inline constexpr bool is_stateless_operation_state_v =
    is_operation_state_v<T> &&
    unifex::is_callable_v<decltype(start), T&&>;

/////////////////////////////
// sender/algorithm interface
//
// TODO: Should connect() be named async_invoke()?

namespace _connect
{
    struct _fn;
}

namespace _connect_from
{
    struct _fn {
        template(
            typename Algorithm,
            typename Receiver,
            typename... ValueFactories)
            (requires
                (unifex::invocable<ValueFactories&> && ...) AND
                unifex::tag_invocable<_fn, Algorithm, Receiver, ValueFactories...>)
        auto operator()(Algorithm alg, Receiver&& r, ValueFactories&&... valueFactories) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Algorithm, Receiver, ValueFactories...>)
            -> unifex::tag_invoke_result_t<_fn, Algorithm, Receiver, ValueFactories...> {
            return unifex::tag_invoke(_fn{}, alg, (Receiver&&)r, (ValueFactories&&)valueFactories...);
        }

        template(
            typename Algorithm,
            typename Receiver,
            typename... ValueFactories,
            typename Connect = _connect::_fn)
            (requires
                (unifex::invocable<ValueFactories&> && ...) AND
                (!unifex::tag_invocable<_fn, Algorithm, Receiver, ValueFactories...>) AND
                unifex::tag_invocable<Connect, Algorithm, Receiver, unifex::callable_result_t<ValueFactories&>...>)
        auto operator()(Algorithm alg, Receiver&& r, ValueFactories&&... valueFactories) const
            noexcept(
                (unifex::is_nothrow_callable_v<ValueFactories&> && ...) &&
                unifex::is_nothrow_tag_invocable_v<Connect, Algorithm, Receiver, unifex::callable_result_t<ValueFactories&>...>)
            -> unifex::tag_invoke_result_t<Connect, Algorithm, Receiver, unifex::callable_result_t<ValueFactories&>...> {
            return unifex::tag_invoke(Connect{}, alg, (Receiver&&)r, valueFactories()...);    
        }
    };
}

inline constexpr _connect_from::_fn connect_from{};

namespace _connect
{
    struct _fn {
        template(
            typename Algorithm,
            typename Receiver,
            typename... Args)
            (requires
                unifex::tag_invocable<_fn, Algorithm, Receiver, Args...>)
        auto operator()(Algorithm&& alg, Receiver&& r, Args&&... args) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Algorithm, Receiver, Args...>)
            -> unifex::tag_invoke_result_t<_fn, Algorithm, Receiver, Args...> {
            return unifex::tag_invoke(_fn{}, (Algorithm&&)alg, (Receiver&&)r, (Args&&)args...);
        }

        template(
            typename Algorithm,
            typename Receiver,
            typename... Args)
            (requires
                (!unifex::tag_invocable<_fn, Algorithm, Receiver, Args...>) AND
                unifex::tag_invocable<_connect_from::_fn, Algorithm, Receiver, value_factory<Args&&>...>)
        auto operator()(Algorithm&& alg, Receiver&& r, Args&&... args) const
            noexcept(
                unifex::is_nothrow_tag_invocable_v<_connect_from::_fn, Algorithm, Receiver, value_factory<Args&&>...>)
            -> unifex::tag_invoke_result_t<_connect_from::_fn, Algorithm, Receiver, value_factory<Args&&>...> {
            return unifex::tag_invoke(_connect_from::_fn{}, (Algorithm&&)alg, (Receiver&&)r, value_factory<Args&&>{args}...);    
        }
    };
}

inline constexpr _connect::_fn connect{};

template<typename... Args>
using connect_result_t = unifex::callable_result_t<decltype(connect), Args&&...>;

template<typename... Args>
using connect_from_result_t = unifex::callable_result_t<decltype(connect_from), Args&&...>;

/////////////////////////////////////////////////
// typed operations
//
// operation_state types must have a OS::result_types type alias that describes
// the set of possible completion-signals that the operation may complete with.
//
// Example:
//   struct my_operation_state {
//     using result_types = unifex::type_list<
//       async_result_t<::set_value, my_cleanup_receiver, int, bool>,
//       async_result_t<::set_error, my_cleanup_receiver, std::exception_ptr>>;
//   };
//
// This type_list indicates the set of result-signals that the operation
// may complete with and thus indicates the set of completion methods that
// the receiver needs to be prepared to have instantiated.
//
// The types that are valid for the 'Signal' parameter are the three types:
// - unifex::tag_t<::set_value>
// - unifex::tag_t<::set_done>
// - unifex::tag_t<::set_error>
//
// If your operation would complete with either ::set_value_from() or
// ::set_error_from() then use the corresponding ::set_value/::set_error
// signal and the types that would be produced by the value-factory.

template<typename Signal, typename CleanupReceiver, typename... Values>
struct async_result {
    using signal_type = Signal;
    using receiver_type = CleanupReceiver;
    using values = unifex::type_list<Values...>;
};

template<auto& CPO, typename CleanupReceiver, typename... Args>
using async_result_t = async_result<unifex::tag_t<CPO>, Args...>;

template<typename... Overloads>
using overload_set = unifex::concat_type_lists_unique_t<
    unifex::type_list<Overloads>...>;

// template<typename CPO, typename CleanupReceiver, typename... Args>
// struct set_result_overload;

// template<typename CleanupReceiver>
// struct set_result_overload<unifex::tag_t<::set_done>, CleanupReceiver> {
//     using type = overload<unifex::tag_t<::set_done>, CleanupReceiver>;
// };

// template<typename CleanupReceiver, typename... ValueFactories>
// struct set_result_overload<unifex::tag_t<::set_value_from>, CleanupReceiver, ValueFactories...> {
//     using type = overload<unifex::tag_t<::set_value>, CleanupReceiver, unifex::tag_invoke_result_t<ValueFactories&>...>;
// };

// template<typename CleanupReceiver, typename... Values>
// struct set_result_overload<unifex::tag_t<::set_value>, CleanupReceiver, Values...> {
//     using type = overload<unifex::tag_t<::set_value>, CleanupReceiver, Values...>;
// };

// template<typename CleanupReceiver, typename ErrorFactory>
// struct set_result_overload<unifex::tag_t<::set_error_from>, CleanupReceiver, ErrorFactory> {
//     using type = overload<unifex::tag_t<::set_error>, CleanupReceiver, unifex::tag_invoke_result_t<ErrorFactory&>>;
// };

// template<typename CleanupReceiver, typename Error>
// struct set_result_overload<unifex::tag_t<::set_value>, CleanupReceiver, Error> {
//     using type = overload<unifex::tag_t<::set_value>, CleanupReceiver, Error>;
// };

// template<typename CPO, typename CleanupReceiver, typename... Args>
// using set_result_overload_t = typename set_result_overload<CPO, CleanupReceiver, Args...>::type;



// template<typename CPO, typename Receiver>
// struct _overload_invoke_result {
//     template<typename... Args>
//     using apply = unifex::callable_result_t<CPO, Receiver, Args...>;
// };

// template<typename Overload, typename Receiver>
// using overload_invoke_result_t =
//     typename Overload::argument_list::template apply<
//         _overload_invoke_result<typename Overload::cpo_type, Receiver>::template apply>;

// template<typename... CPOs>
// struct _overloads_with_cpo {
//     template<typename... Overloads>
//     using apply = unifex::concat_type_lists_t<
//         std::conditional_t<
//             unifex::is_one_of_v<typename Overloads::cpo_type, CPOs...>,
//             unifex::type_list<Overloads>,
//             unifex::type_list<>>...>;
// };

// template<typename OverloadSet, auto&... CPOs>
// using overloads_with_cpo_t =
//     typename OverloadSet::template apply<
//         _overloads_with_cpo<unifex::tag_t<CPOs>...>::template apply>;

// template<typename OverloadSet>
// using set_result_overloads_t =
//     overloads_with_cpo_t<OverloadSet, ::set_done, ::set_error, ::set_error_from, ::set_value, ::set_value_from>;

struct noop_cleanup_receiver {
    // template<typename CleanupReceiver>
    // using set_done_result_types = overload_set<
    //     overload_t<::set_done, noop_cleanup_receiver>>;

    // template<typename CleanupReceiver, typename Error>
    // using set_error_result_types = overload_set<
    //     overload_t<::set_error, noop_cleanup_receiver, Error>>;

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

template<typename CPO, typename... Args>
struct _sender_for {
    class type;
};

template<typename CPO, typename... Args>
using sender_for = typename _sender_for<CPO, Args...>::type;


template<typename CPO, typename... Args>
class _sender_for<CPO, Args...>::type {
    using sender_for = type;
public:
    template<typename... Args2>
    type(std::in_place_t, Args2&&... args)
    : curriedArgs_(static_cast<Args2&&>(args)...)
    {}

    template(typename... ExtraArgs)
        (requires
            unifex::invocable<CPO, Args..., ExtraArgs...>)
    auto operator()(ExtraArgs&&... extraArgs) &&
            noexcept(std::is_nothrow_invocable_v<CPO, Args..., ExtraArgs...>)
            -> std::invoke_result_t<CPO, Args..., ExtraArgs...> {
        return std::apply([&](Args&&... args) {
            return CPO{}((Args&&)args..., (ExtraArgs&&)extraArgs...);
        }, std::move(curriedArgs_));
    }

    template(typename... ExtraArgs)
        (requires
            unifex::invocable<CPO, unifex::member_t<const sender_for&, Args>..., ExtraArgs...>)
    auto operator()(ExtraArgs&&... extraArgs) const &
            noexcept(std::is_nothrow_invocable_v<CPO, unifex::member_t<const type&, Args>..., ExtraArgs...>)
            -> std::invoke_result_t<CPO, unifex::member_t<const sender_for&, Args>..., ExtraArgs...> {
        return std::apply([&](Args&&... curriedArgs) {
            return CPO{}(
                static_cast<Args&&>(curriedArgs)...,
                static_cast<ExtraArgs&&>(extraArgs)...);
        }, std::move(curriedArgs_));
    }

    template(typename Self, typename Receiver, typename... ExtraArgs)
        (requires
            unifex::same_as<unifex::remove_cvref_t<Self>, sender_for> AND
            unifex::invocable<decltype(::connect), CPO, Receiver, unifex::member_t<Self, Args>..., ExtraArgs...>)
    friend auto tag_invoke(unifex::tag_t<::connect>, Self&& self, Receiver&& r, ExtraArgs&&... extraArgs)
        noexcept(unifex::is_nothrow_callable_v<
            decltype(::connect),
            CPO,
            Receiver,
            unifex::member_t<Self, Args>...,
            ExtraArgs...>)
        -> unifex::callable_result_if_t<
                unifex::same_as<unifex::remove_cvref_t<Self>, sender_for>,
                decltype(::connect),
                CPO,
                Receiver,
                unifex::member_t<Self, Args>...,
                ExtraArgs...> {
        return std::apply([&](unifex::member_t<Self, Args>... curriedArgs) -> decltype(auto) {
            return ::connect(
                CPO{},
                static_cast<Receiver&&>(r), 
                static_cast<unifex::member_t<Self, Args>>(curriedArgs)...,
                static_cast<ExtraArgs&&>(extraArgs)...);
        }, static_cast<Self&&>(self).curriedArgs_);
    }

    template(typename Self, typename Receiver, typename... ExtraArgFactories)
        (requires
            unifex::same_as<std::remove_cvref_t<Self>, sender_for> AND
            unifex::invocable<
                decltype(::connect_from), CPO, Receiver,
                value_factory<unifex::member_t<Self, Args>>...,
                ExtraArgFactories...>)
    friend auto tag_invoke(unifex::tag_t<::connect_from>, Self&& self, Receiver&& r, ExtraArgFactories&&... extraArgs)
        noexcept(unifex::is_nothrow_callable_v<
            decltype(::connect_from),
            CPO,
            Receiver,
            value_factory<unifex::member_t<Self, Args>>...,
            ExtraArgFactories...>)
        -> unifex::callable_result_if_t<
                unifex::same_as<unifex::remove_cvref_t<Self>, sender_for>,
                decltype(::connect_from),
                CPO,
                Receiver,
                value_factory<unifex::member_t<Self, Args>>...,
                ExtraArgFactories...> {
        return std::apply([&](unifex::member_t<Self, Args>... curriedArgs) -> decltype(auto) {
            return ::connect_from(
                CPO{},
                static_cast<Receiver&&>(r), 
                value_factory<unifex::member_t<Self, Args>>(
                    static_cast<unifex::member_t<Self, Args>>(curriedArgs))...,
                static_cast<ExtraArgFactories&&>(extraArgs)...);
        }, static_cast<Self&&>(self).curriedArgs_);
    }

private:
    std::tuple<Args...> curriedArgs_;
};

template<typename Derived>
struct _sender_cpo_base {
    template(typename... Args)
        (requires
            unifex::tag_invocable<Derived, Args...>)
    auto operator()(Args&&... args) const
        noexcept(unifex::is_nothrow_tag_invocable_v<Derived, Args...>)
        -> unifex::tag_invoke_result_t<Derived, Args...> {
        return unifex::tag_invoke(Derived{}, (Args&&)args...);
    }

    template(typename... Args)
        (requires
            (!unifex::tag_invocable<Derived, Args...>) AND
            (unifex::constructible_from<std::decay_t<Args>, Args> && ...))
    auto operator()(Args&&... args) const
        noexcept(
            (std::is_nothrow_constructible_v<std::decay_t<Args>, Args> && ...))
        -> sender_for<Derived, std::decay_t<Args>...> {
        return sender_for<Derived, std::decay_t<Args>...>{
            std::in_place, static_cast<Args&&>(args)...
        };
    }
};

///////////////////////////////////////////////////////////////////////
// just(values...)
//
// Equivalent of the following coroutines-v2 function:
//
// auto just(auto... values) [->] task< {
//   co_return...{ std::move(values), ... };
// }

template<typename Continuation>
struct _just_op {
    class type;
};

template<typename Continuation>
using just_op = typename _just_op<Continuation>::type;


template<typename Continuation>
class _just_op<Continuation>::type {
    using just_op = type;
public:
    explicit type(Continuation continuation) noexcept
    : continuation_(continuation)
    , started_(false)
    {}

    type(const type&) = delete;
    type(type&&) = delete;
    type& operator=(const type&) = delete;
    type& operator=(type&&) = delete;

    ~type() {
        if (!started_) {
            continuation_.destroy();
        }
    }

private:

    friend Continuation tag_invoke(
        unifex::tag_t<::start>,
        just_op& op) noexcept {
        assert(!op.started_);
        op.started_ = true;
        return op.continuation_;
    }

    // Implement stateless_operation_state interface (rvalue start())
    friend Continuation tag_invoke(
        unifex::tag_t<::start>,
        just_op&& op) noexcept {
        assert(!op.started_);
        op.started_ = true;
        return op.continuation_;
    }

    Continuation continuation_;
    bool started_;
};

struct _just_fn : _sender_cpo_base<_just_fn> {
    template(typename Receiver, typename... Values)
        (requires
            unifex::invocable<decltype(::set_value), Receiver, noop_cleanup_receiver, Values...>)
    friend auto tag_invoke(unifex::tag_t<::connect>, _just_fn, Receiver&& r, Values&&... values)
        noexcept(std::is_nothrow_invocable_v<decltype(set_value), Receiver, noop_cleanup_receiver, Values...>)
        -> just_op<
            unifex::callable_result_t<decltype(::set_value), Receiver, noop_cleanup_receiver, Values...>> {
        return just_op<
                unifex::callable_result_t<decltype(::set_value), Receiver, noop_cleanup_receiver, Values...>>{
            ::set_value(
                static_cast<Receiver&&>(r),
                noop_cleanup_receiver{},
                static_cast<Values&&>(values)...)};
    }

    template(typename Receiver, typename... ValueFactories)
        (requires
            unifex::invocable<decltype(::set_value_from), Receiver, noop_cleanup_receiver, ValueFactories...>)
    friend auto tag_invoke(unifex::tag_t<::connect_from>, _just_fn, Receiver&& r, ValueFactories&&... valueFactories)
        noexcept(std::is_nothrow_invocable_v<decltype(set_value_from), Receiver, noop_cleanup_receiver, ValueFactories...>)
        -> just_op<unifex::callable_result_t<decltype(::set_value_from), Receiver, noop_cleanup_receiver, ValueFactories...>> {
        return just_op<unifex::callable_result_t<decltype(::set_value_from), Receiver, noop_cleanup_receiver, ValueFactories...>>{
            ::set_value_from(
                static_cast<Receiver&&>(r),
                noop_cleanup_receiver{},
                static_cast<ValueFactories&&>(valueFactories)...)};
    }
};

inline constexpr _just_fn just{};

/////////////////////////////////////////////////////////////////////
// just_done()

struct _just_done_fn : _sender_cpo_base<_just_done_fn> {
    // template<typename Receiver>
    // using invoke_results = overload_set<
    //     overload_t<::set_done, noop_cleanup_receiver>>;

    template(typename Receiver)
        (requires
            unifex::invocable<decltype(::set_done), Receiver, noop_cleanup_receiver>)
    friend auto tag_invoke(unifex::tag_t<::connect>, _just_done_fn, Receiver&& r)
        noexcept(std::is_nothrow_invocable_v<decltype(set_done), Receiver, noop_cleanup_receiver>)
        -> just_op<std::invoke_result_t<decltype(set_done), Receiver, noop_cleanup_receiver>> {
        return just_op<std::invoke_result_t<decltype(set_done), Receiver, noop_cleanup_receiver>>{
            ::set_done(
                static_cast<Receiver&&>(r),
                noop_cleanup_receiver{})};
    }
};

inline constexpr _just_done_fn just_done{};

///////////////////////////////////////////////////////////////////
// just_error(e)


struct _just_error_fn : _sender_cpo_base<_just_error_fn> {
    // template<typename Receiver, typename Error>
    // using invoke_results = overload_set<
    //     overload_t<::set_error, noop_cleanup_receiver, Error>>;

    template(typename Receiver, typename Error)
        (requires
            unifex::invocable<decltype(::set_error), Receiver, noop_cleanup_receiver, Error>)
    friend auto tag_invoke(unifex::tag_t<::connect>, _just_error_fn, Receiver&& r, Error&& error)
        noexcept(std::is_nothrow_invocable_v<decltype(set_error), Receiver, noop_cleanup_receiver, Error>)
        -> just_op<std::invoke_result_t<decltype(set_error), Receiver, noop_cleanup_receiver, Error>> {
        return just_op<std::invoke_result_t<decltype(set_error), Receiver, noop_cleanup_receiver, Error>>{
            ::set_error(
                static_cast<Receiver&&>(r),
                noop_cleanup_receiver{},
                static_cast<Error&&>(error))};
    }

    template(typename Receiver, typename ErrorFactory)
        (requires
            unifex::invocable<decltype(::set_error_from), Receiver, noop_cleanup_receiver, ErrorFactory>)
    friend auto tag_invoke(unifex::tag_t<::connect_from>, _just_error_fn, Receiver&& r, ErrorFactory&& errorFactory)
        noexcept(std::is_nothrow_invocable_v<decltype(set_error_from), Receiver, noop_cleanup_receiver, ErrorFactory>)
        -> just_op<std::invoke_result_t<decltype(set_error_from), Receiver, noop_cleanup_receiver, ErrorFactory>> {
        return just_op<std::invoke_result_t<decltype(set_error_from), Receiver, noop_cleanup_receiver, ErrorFactory>>{
            ::set_error_from(
                static_cast<Receiver&&>(r),
                noop_cleanup_receiver{},
                static_cast<ErrorFactory&&>(errorFactory))};
    }
};

inline constexpr _just_error_fn just_error{};

////////////////////////////////////////////////////////////
// async_cleanup_scope

namespace _async_cleanup_scope {

template<typename Source, typename Receiver>
struct _op {
    class type;
};

template<typename Source, typename Receiver>
using async_cleanup_scope_op = typename _op<Source, Receiver>::type;

template<typename Source, typename Receiver>
struct _cleanup_done_receiver {
    struct type {
        template(typename SetError, typename Error)
            (requires
                is_set_error_cpo_v<SetError> AND
                unifex::invocable<SetError, Receiver, noop_cleanup_receiver, Error>)
        friend auto tag_invoke(
            SetError setError, type&& r, noop_cleanup_receiver, Error&& error) noexcept
            -> unifex::callable_result_t<SetError, Receiver, noop_cleanup_receiver, Error> {
            r.op_->continuation_.destroy();
            return setError(std::move(r.op_->receiver_), noop_cleanup_receiver{}, (Error&&)error);
        }

        friend unifex::any_continuation_handle tag_invoke(
            unifex::tag_t<::set_done>,
            type&& r,
            noop_cleanup_receiver) noexcept {
            return r.op_->continuation_;
        }

        async_cleanup_scope_op<Source, Receiver>* op_;
    };
};

template<typename Source, typename Receiver>
using cleanup_done_receiver = typename _cleanup_done_receiver<Source, Receiver>::type;


template<typename Source, typename Receiver>
struct _error_cleanup_done_receiver {
    struct type {
        template(typename SetError, typename Error)
            (requires
                is_set_error_cpo_v<SetError> AND
                unifex::invocable<SetError, Receiver, noop_cleanup_receiver, Error>)
        friend unifex::noop_continuation_handle tag_invoke(
            SetError setError, type&&, noop_cleanup_receiver, Error&& error) noexcept {
            // Error thrown during unwind while an existing error was in-flight.
            std::terminate();
        }

        friend unifex::any_continuation_handle tag_invoke(
            unifex::tag_t<::set_done>,
            type&& r,
            noop_cleanup_receiver) noexcept {
            return r.op_->continuation_;
        }

        async_cleanup_scope_op<Source, Receiver>* op_;
    };
};

template<typename Source, typename Receiver>
using error_cleanup_done_receiver = typename _error_cleanup_done_receiver<Source, Receiver>::type;

template<typename Source, typename Receiver>
struct _source_receiver {
    struct type {
        async_cleanup_scope_op<Source, Receiver>* op_;

        template(typename SetResult, typename CleanupReceiver, typename... Args)
            (requires true
                //(is_set_value_cpo_v<SetResult> || is_set_done_cpo_v<SetResult>)
                //AND
                //unifex::invocable<SetResult, Receiver, noop_cleanup_receiver, Args...>
                )
        friend auto tag_invoke(
            SetResult setResult,
            type&& r,
            CleanupReceiver&& cr,
            Args&&... args)
                noexcept(unifex::is_nothrow_callable_v<SetResult, Receiver, noop_cleanup_receiver, Args...>)
                -> unifex::callable_result_t<decltype(::set_done), CleanupReceiver, cleanup_done_receiver<Source, Receiver>> {
            // TODO: Avoid the need to type-erase the continuation as an any_continuation_handle
            // here by enumerating all of the possible completions and creating a manual_lifetime_union
            // of them all instead of the current contination_ member.
            r.op_->continuation_ = setResult(std::move(r.op_->receiver_), noop_cleanup_receiver{}, (Args&&)args...);
            return ::set_done((CleanupReceiver&&)cr, cleanup_done_receiver<Source, Receiver>{r.op_});
        }

        template(typename SetError, typename CleanupReceiver, typename... Args)
            (requires true
                //is_set_error_cpo_v<SetError>
                //AND
                //unifex::invocable<SetError, Receiver, noop_cleanup_receiver, Args...>
                )
        friend auto tag_invoke(
            SetError setError,
            type&& r,
            CleanupReceiver&& cr,
            Args&&... args)
                noexcept(unifex::is_nothrow_callable_v<SetError, Receiver, noop_cleanup_receiver, Args...>) 
                -> unifex::callable_result_t<decltype(::set_done), CleanupReceiver, error_cleanup_done_receiver<Source, Receiver>> {
            r.op_->continuation_ = setError(std::move(r.op_->receiver_), noop_cleanup_receiver{}, (Args&&)args...);
            return ::set_done((CleanupReceiver&&)cr, error_cleanup_done_receiver<Source, Receiver>{r.op_});
        }
    };
};

template<typename Source, typename Receiver>
using source_receiver = typename _source_receiver<Source, Receiver>::type;

template<typename Source, typename Receiver>
class _op<Source, Receiver>::type {
    using async_cleanup_scope_op = type;
public:
    template<typename Receiver2>
    explicit type(Source&& source, Receiver2&& receiver)
    : receiver_((Receiver2&&)receiver)
    , sourceOp_(::connect((Source&&)source, source_receiver<Source, Receiver>{this}))
    {}

    friend auto tag_invoke(
        unifex::tag_t<::start>,
        async_cleanup_scope_op& op) noexcept {
        return ::start(op.sourceOp_);
    }
    
    Receiver receiver_;
    unifex::any_continuation_handle continuation_;
    connect_result_t<Source, source_receiver<Source, Receiver>> sourceOp_;
};

struct _fn : _sender_cpo_base<_fn> {
    template(typename Receiver, typename Source)
        (requires
            unifex::invocable<decltype(::connect), Source, source_receiver<Source, unifex::remove_cvref_t<Receiver>>>)
    friend auto tag_invoke(
        unifex::tag_t<::connect>,
        _fn,
        Receiver&& receiver,
        Source&& source) noexcept {
        return async_cleanup_scope_op<Source, unifex::remove_cvref_t<Receiver>>{
            (Source&&)source,
            (Receiver&&)receiver};
    }
};

} // namespace _async_cleanup_scope

inline constexpr _async_cleanup_scope::_fn async_cleanup_scope{};

#include <unifex/detail/epilogue.hpp>

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

struct simple_receiver {
    template<typename CleanupReceiver, typename... Values>
    friend auto tag_invoke(
        unifex::tag_t<::set_value>,
        simple_receiver&& r,
        CleanupReceiver&& cr,
        Values&&... values) {
        std::printf("set_value(");
        (void)((std::printf("%i, ", values), 0) || ...);
        std::printf(")\n");
        return unifex::noop_continuation;
    }
};

static void algorithm_as_a_sender() {
    auto op = ::connect(just, simple_receiver{}, 42, 13, 99);
    unifex::run_continuation(::start(op));
}

static void algorithm_as_a_sender_factory() {
    auto s = just(5, 6, 7);
    auto op = ::connect(s, simple_receiver{});
    unifex::run_continuation(::start(op));
}

static void senders_as_argument_curriers() {
    auto s = just(1, 2, 3);
    auto op = ::connect(s, simple_receiver{}, 8, 9, 10);
    unifex::run_continuation(::start(op));
}

static void async_cleanup_scope_test() {
    auto s = async_cleanup_scope(just(1, 2, 3));
    auto op = ::connect(s, simple_receiver{});
}

int main() {
    algorithm_as_a_sender();
    algorithm_as_a_sender_factory();
    senders_as_argument_curriers();
    return 0;
}
