#include <unifex/tag_invoke.hpp>
#include <unifex/continuations.hpp>
#include <unifex/type_traits.hpp>

#include <iostream>

namespace _connect
{
    struct _fn {
        template<typename Sender, typename Receiver>
        auto operator()(Sender&& s, Receiver&& r) const
            -> unifex::tag_invoke_result_t<_fn, Sender, Receiver> {
            return unifex::tag_invoke(*this, (Sender&&)s, (Receiver&&)r);
        }
    };
}

inline constexpr _connect::_fn connect{};

namespace _set_value
{
    struct _fn {
        template<typename Receiver, typename... Args>
        auto operator()(Receiver&& r, Args&&... args) const
            noexcept(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, Args...>)
            -> unifex::tag_invoke_result_t<_fn, Receiver, Args...> {
            return unifex::tag_invoke(*this, (Receiver&&)r, (Args&&)args...);
        }
    };
}

inline constexpr _set_value::_fn set_value{};

namespace _set_error
{
    struct _fn {
        template<typename Receiver, typename... Args>
        auto operator()(Receiver&& r, Args&&... args) const noexcept
            -> unifex::tag_invoke_result_t<_fn, Receiver, Args...> {
            static_assert(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, Args...>);
            return unifex::tag_invoke(*this, (Receiver&&)r, (Args&&)args...);
        }
    };
}

inline constexpr _set_error::_fn set_error{};

namespace _set_done
{
    struct _fn {
        template<typename Receiver, typename... Args>
        auto operator()(Receiver&& r, Args&&... args) const noexcept
            -> unifex::tag_invoke_result_t<_fn, Receiver, Args...> {
            static_assert(unifex::is_nothrow_tag_invocable_v<_fn, Receiver, Args...>);
            return unifex::tag_invoke(*this, (Receiver&&)r, (Args&&)args...);
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

template<typename Receiver, typename... Args>
using set_value_result_t = unifex::callable_result_t<decltype(set_value), Receiver, Args...>;

template<typename Receiver, typename... Args>
using set_error_result_t = unifex::callable_result_t<decltype(set_error), Receiver, Args...>;

template<typename Receiver, typename... Args>
using set_done_result_t = unifex::callable_result_t<decltype(set_done), Receiver, Args...>;

template<typename Op>
using start_result_t = unifex::callable_result_t<decltype(start), Op&>;

template<typename T>
inline constexpr bool is_operation_state_v =
    std::is_nothrow_destructible_v<T> &&
    unifex::is_callable_v<decltype(start), T&>;

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
            unifex::tag_t<set_done>,
            cleanup_receiver&& r,
            CleanupDoneReceiver cr) noexcept
            -> set_done_result_t<CleanupDoneReceiver> {
            r.op_->resultOp_.destroy();
            return set_done(std::move(cr));
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

// Version when receiver is nothrow
template<bool ReceiverIsNothrow, typename ResultReceiver, typename... Values>
class _just_operation {
    using value_op_t = set_result_no_cleanup_operation_storage<set_value, ResultReceiver, Values...>;

    std::tuple<Values...> values_;
    ResultReceiver receiver_;
    value_op_t valueOp_;

public:
    template<
        template<typename...> class Variant,
        template<typename...> class Tuple>
    using value_types = Variant<Tuple<Values...>>;

    template<template<typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = false;

    template<typename ValueTuple, typename ResultReceiver2>
    explicit _just_operation(ValueTuple&& values, ResultReceiver2&& r)
    : values_((ValueTuple&&)values)
    , receiver_((ResultReceiver2&&)r)
    {}

    friend auto tag_invoke(unifex::tag_t<start>, _just_operation& self) noexcept {
        return std::apply([&](Values&&... values) noexcept {
            return self.valueOp_.start(std::move(self.receiver_), (Values&&)values...);
        }, std::move(self.values_));
    }
};

// Version when receiver's set_value() is potentially throwing.
template<typename ResultReceiver, typename... Values>
class _just_operation<false, ResultReceiver, Values...> {
    using value_op_t = set_result_no_cleanup_operation_storage<set_value, ResultReceiver, Values...>;
    using error_op_t = set_result_no_cleanup_operation_storage<set_error, ResultReceiver, std::exception_ptr>;

    std::tuple<Values...> values_;
    ResultReceiver receiver_;
    union {
        value_op_t valueOp_;
        error_op_t errorOp_;
    };

public:
    template<
        template<typename...> class Variant,
        template<typename...> class Tuple>
    using value_types = Variant<Tuple<Values...>>;

    template<template<typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = false;

    template<typename ValueTuple, typename ResultReceiver2>
    explicit _just_operation(ValueTuple&& values, ResultReceiver2&& r)
    : values_((ValueTuple&&)values)
    , receiver_((ResultReceiver2&&)r)
    {}

    ~_just_operation() {}

    friend unifex::variant_continuation_handle<
        typename value_op_t::continuation_type,
        typename error_op_t::continuation_type> tag_invoke(unifex::tag_t<start>, _just_operation& self) noexcept {
        try {
            return std::apply([&](Values&&... values) {
                return self.valueOp_.start(std::move(self.receiver_), (Values&&)values...);
            }, std::move(self.values_));
        } catch (...) {
            return self.errorOp_.start(std::move(self.receiver_), std::current_exception());
        }
    }
};

template<typename ResultReceiver, typename... Values>
using just_operation = _just_operation<
    noexcept(UNIFEX_DECLVAL(set_result_no_cleanup_operation_storage<set_value, ResultReceiver, Values...>&).start(
        UNIFEX_DECLVAL(ResultReceiver),
        UNIFEX_DECLVAL(Values)...)),
    ResultReceiver,
    Values...>;

template<typename... Values>
class just_sender {
    std::tuple<Values...> values_;

public:
    template<typename... Values2>
    explicit just_sender(std::in_place_t, Values2&&... values)
    : values_((Values2&&)values...)
    {}

    template<
        typename Self,
        typename ResultReceiver,
        std::enable_if_t<std::is_same_v<std::remove_cvref_t<Self>, just_sender>, int> = 0>
    friend auto tag_invoke(unifex::tag_t<connect>, Self&& self, ResultReceiver&& r)
        -> just_operation<ResultReceiver, Values...> {
        return just_operation<ResultReceiver, Values...>{
            static_cast<Self&&>(self).values_,
            static_cast<ResultReceiver&&>(r)
        };
    }
};

template<typename... Values>
auto just(Values&&... values) -> just_sender<std::decay_t<Values>...> {
    return just_sender<std::decay_t<Values>...>{std::in_place, (Values&&)values...};
}

struct suspend_on_cleanup_done {
    friend auto tag_invoke(
        unifex::tag_t<::set_done>,
        suspend_on_cleanup_done) noexcept {
        return unifex::noop_continuation;
    }
};

template<typename... Values>
static void output(Values...) {}

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
                output(self.values);
            } catch (const std::exception& e) {
                std::printf("error: processing values failed: %s\n", e.what());    
            } catch (...) {
                std::printf("error: unknown error processing values.\n");
            }

            // Finished processing the values, start cleanup
            return ::set_done(std::move(self.cr), suspend_on_cleanup_done{});
        }
    };

    template<typename CleanupReceiver, typename... Values>
    friend auto tag_invoke(
        unifex::tag_t<::set_value>,
        simple_receiver&& r,
        CleanupReceiver&& cr,
        Values&&... values) {
        return process_value_op<CleanupReceiver, Values...>{
            {(Values&&)values...},
            (CleanupReceiver&&)cr
        };
    }

    template<typename CleanupReceiver, typename... Values>
    friend auto tag_invoke(
        unifex::tag_t<::set_error>,
        simple_receiver&& r,
        CleanupReceiver&& cr,
        std::exception_ptr eptr) noexcept {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            std::printf("error: %s\n", e.what());  
        } catch (...) {
            std::printf("error: unknown\n");
        }

        return ::set_done(std::move(cr), suspend_on_cleanup_done{});
    }
};

static void test() {
    auto a = just(7, 13, 2.0f);
    auto op = ::connect(a, simple_receiver{});
    unifex::run_continuation(::start(op));
}

int main() {
    test();
}
