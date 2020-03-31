# Index

* Receiver Queries
  * `get_stop_token()`
  * `get_scheduler()`
  * `get_allocator()`
* Sender Algorithms
  * `transform()`
  * `finally()`
  * `via()`
  * `typed_via()`
  * `on()`
  * `let()`
  * `sequence()`
  * `sync_wait()`
  * `when_all()`
  * `materialize()`
  * `dematerialize()`
  * `repeat_effect_until()`
  * `repeat_effect()`
  * `retry_when()`
  * `allocate()`
  * `with_query_value()`
  * `with_allocator()`
* Sender Types
  * `async_trace_sender`
* Sender Queries
  * `blocking()`
* Stream Algorithms
  * `adapt_stream()`
  * `next_adapt_stream()`
  * `reduce_stream()`
  * `for_each()`
  * `transform_stream()`
  * `via_stream()`
  * `typed_via_stream()`
  * `on_stream()`
  * `type_erase<Ts...>()`
  * `take_until()`
  * `single()`
  * `stop_immediately()`
  * `delay()`
* Stream Types
  * `range_stream`
  * `type_erased_stream<Ts...>`
  * `never_stream`
* Scheduler Types
  * `inline_scheduler`
  * `single_thread_context`
  * `trampoline_scheduler`
  * `timed_single_thread_context`
  * `thread_unsafe_event_loop`
  * `new_thread_context`
  * `linux::io_uring_context`
* StopToken Types
  * `unstoppable_token`
  * `inplace_stop_token` / `inplace_stop_source`
* Synchronisation Primitives
  * `async_mutex`

# Receiver Queries

### `get_scheduler(receiver)`

A query that can be used to obtain the associated scheduler from the receiver.

This can be used by senders to obtain a scheduler that can be used to schedule
work if required.

Receivers can customise this CPO to return the current scheduler.

See the `schedule()` algorithm, which schedules onto the current scheduler.

### `get_allocator(receiver)`

Obtain the current allocator that should be used for heap-allocating storage
needed by the implementation of a sender if required.

This may be customised by a receiver to return a specific allocator but if
it has not been customised then defaults to return `std::allocator<char>`.

### `get_stop_token(receiver)`

Obtain the current stop-token from the receiver.

If a sender's operation is able to be cancelled/interrupted then the sender should
call this function to query the stop-token provided by the receiver and use
this stop-token to either poll or subscribe for notification of a request to stop.

If a receiver has not customised this it will default to return `unstoppable_token`.

See the [Cancellation](cancellation.md) section for more details on cancellation.

# Sender Algorithms

### `transform(Sender predecessor, Func func) -> Sender`

Returns a sender that transforms the value of the `predecessor` by calling
`func(value)`.

### `finally(Sender source, Sender completion) -> Sender`

Returns a sender that will first launch `source` and upon completion of
`source` will launch the `completion` sender.

If `completion` completes with `set_value()` (which must complete with an
empty value pack) then the composed operation completes with the result of
`source`.
Otherwise, if `completion` sender completes with `set_done` or `set_error`
then the composed operation completes with the result of `completion`.

The composed finally-operation will complete inline on the execution context
that the `completion` sender completes on, except in the case that the call
to `connect()` on the completion-sender exits with an exception, in which case
the operation will complete with `set_error()` inline on whatever execution
context the `source` sender completed on.

Note that `completion` sender must complete with an empty value pack
if it completes with `set_value`.
ie. it must be a `void`-value sender.

### `via(Sender successor, Sender predecessor) -> Sender`

Returns a sender that produces the result from `predecessor` on the
execution context that `successor` completes on.

Any value produced by `successor` is discarded.
QUESTION: Should we require that `successor` is a `void`-sender?

If `successor` completes with `set_done()` then `set_done()` is sent.
If `successor` completes with `set_error()` then its error is sent.
Otherwise sends the result of `predecessor`.

### `typed_via(Sender source, Scheduler scheduler) -> Sender`

Returns a sender that produces the result from `source`, which must
declare the nested `value_types`/`error_types` type aliases which describe which
overloads of `set_value()`/`set_error()` they will call, on the execution context
associated with `scheduler`.



### `on(Sender sender, Scheduler scheduler) -> Sender`

Returns a sender that ensures that `sender` is started on the
execution context associated with the specified `scheduler`.

The `sender` is executed with a receiver that customises the
`get_scheduler` query to return the specified `scheduler`.

The default implementation schedules the call to `connect()`
and subsequent `start()` onto an execution context associated
with `scheduler` using the `schedule(scheduler)` operation.

If `schedule(scheduler)` completes with `set_done()` or
`set_error()` then the `on()` operation completes with
that signal and never starts executing `sender`.

The `on()` algorithm may be customised by particular schedulers
and/or scheduler+sender combinations to provide an alternative
impllementation.

### `let(Sender pred, Invocable func) -> Sender`

The `let()` algorithm accepts a predecessor task that produces a value that
you want remain alive for the duration of a successor operation.

When the predecessor operation completes with a value, the function `func`
is invoked with lvalue references to copies of the values produced by
the predecessor. This invocation must return a Sender.

The references passed to `func` remain valid until the returned sender
completes, at which point the variables go out of scope.

For example:
```c++
let(some_operation(),
    [](auto& x) {
      return other_operation(x);
    });
```
is roughly equivalent to the following coroutine code:
```c++
{
  auto x = co_await some_operation();
  co_await other_operation(x);
}
```

If the predecessor completes with value then the `let()` operation as a
whole will complete with the result of the successor.

If the predecessor completes with done/error then `func` is not invoked
and the operation as a whole completes with that done/error signal.

### `sequence(Sender... predecessors, Sender last) -> Sender`

The `sequence()` algorithm takes a variadic pack of senders and executes
them sequentially, only starting the next sender if/when the previous sender
completed successfully (ie. with `set_value`).

All but the `last` sender must produce a `void` value result
i.e. call `set_value(receiver)` with no additional value args.

If any of the input senders complete with `set_done` or `set_error`
then the operation as a whole completes with that signal and
any subsequent operations in the sequence are not started.

This algorithm may be customised by defining a custom `tag_invoke(tag_t<sequence>, ...)`
overload for your particular sender types. You can either provide a customisation
for a variadic pack of senders or for a pair of senders.

If you provide a customisation for a pair of senders then this customisation
will be applied to the first two arguments and then reinvoke `sequence()`
with the first two arguments replaced with the result of `sequence(first, second)`.

### `sync_wait(Sender sender, StopToken st = {}) -> std::optional<Result>`

Blocks the current thread waiting for the specified sender to complete.

Returns a non-empty optional if it completed with `set_value()`.
Or `std::nullopt` if it completed with `set_done()`
Or throws an exception if it completed with `set_error()`

### `when_all(Senders...) -> Sender`

Takes a variadic number of senders and returns a sender that launches each of
the input senders in-turn without waiting for the prior senders to complete.
This allows each of the input senders to potentially execute concurrently.

The result of the Sender has a value_type of:
`std::tuple<std::variant<std::tuple<Ts...>, ...>, ...>`

There is an element in the outer-tuple for each input sender.
Each element in the outer tuple is a variant that indicates which overload
of `value()` was called on the receiver by the corresponding sender.
The variant's value is a tuple that contains copies of the arguments passed
to `value()`.

If any of the input senders complete with done or error then it will request
any senders that have not yet completed to stop and the operation as a whole
will complete with done or error.

### `materialize(Sender sender) -> Sender`

Materializes the completion signal of `sender` into the value-channel by
invoking prepending the completion arguments with the corresponding
`set_value`, `set_error` or `set_done` CPO as an additional argument.

ie. Transforms the following LHS completion signals to the RHS completion signals
* `set_value(r, values...)` -> `set_value(r, set_value, values...)`
* `set_error(r, e)` -> `set_value(r, set_error, e)`
* `set_done(r)` -> `set_value(r, set_done)`

This allows you to treat any result as a success and process the result as
a value.

### `dematerialize(Sender sender) -> Sender`

Converts a sender of materialized signals into a sender of those signals.
This reverses the transformation of signals performed by `materialize()`.

If `sender` completes with `set_value(r, set_value, values...)` then the
dematerialized sender will complete with `set_value(r, values...)`.

Similarly if `sender` completes with `set_value(r, set_error, e)` then the
dematerialized sender will complete with `set_error(r, e)`.

And if `sender` completes with `set_value(r, set_done)` then the dematerialized
sender will complete with `set_done(r)`.

Any `set_error()` or `set_done()` signals are passed through unchanged.

### `repeat_effect_until(Sender source, Invocable predicate) -> Sender`

The `repeat_effect_until()` algorithm repeats the source sender for as long as the 
predicate returns false.

The `source` sender must be lvalue connectable (ie. can be connected and started 
multiple times).

The `source` sender must be an effect. It must prooduce void.

If the `source` sender completes with `set_error()` or `set_done()` then the
`repeat_effect_until()` operation completes with that same signal.

If the `source` sender completes with void then the `predicate` function is 
invoked. The `predicate` function must return `false` to repeat the source and 
`true` to complete with void.

If the invocation of the `predicate()` throws an exception then the 
`repeat_effect_until()` operation immediately completes with 
`set_error(std::current_exception())`.

Example usage: Repeat the operation forever - until the source is cancelled.
```c++
unifex::repeat_effect_until(
  some_operation(),
  [] {
    return false;
  });
```

This is the default implementation for `repeat_effect()`.

### `repeat_effect(Sender source) -> Sender`

The `repeat_effect()` algorithm repeats the source sender until the source is 
cancelled.

The `source` sender must be lvalue connectable (ie. can be connected and started 
multiple times).

The `source` sender must be an effect. It must prooduce void.

If the `source` sender completes with `set_error()` or `set_done()` then the
`repeat_effect()` operation completes with that same signal.

If the `source` sender completes with void then the source is started again.

Example usage: Repeat the operation forever - until the source is cancelled.
```c++
unifex::repeat_effect(some_operation());
```

The default implementation uses `repeat_effect_until()` with a predicate that 
always returns false.

### `retry_when(Sender source, Invocable<Error> handler) -> Sender`

The `retry_when()` algorithm repeatedly retries executing the input sender
if it fails with an error after some delay indicated by the handler function.

The `source` sender must be lvalue connectable (ie. can be connected and started
multiple times).

If the `source` sender completes with `set_value()` or `set_done()` then the
`retry_when()` operation completes with that same signal.

If the `source` sender completes with an error then the `handler` function is
invoked with that error value. The `handler` function must return a new sender
which is then immediately started.

If the invocation of the `handler()` throws an exception or attempting to launch
the returned sender throws an exception then the `retry_when()` operation immediately
completes with `set_error(std::current_exception())`.

If the sender returned by `handler()` completes with `set_value()` then the
`source` operation is relaunched.

Otherwise, if the sender returned by `handler()` completes with `set_error(e)` or
`set_done()` then this becomes the result of the `retry_when()` operation.


Example usage: Retry the operation up to 5 times with increasing delays between retries.
```c++
unifex::retry_when(
  some_operation(),
  [count = 0, scheduler](std::exception_ptr ex) mutable {
    if (++count >= 5) {
      std::rethrow_exception(ex);
    }
    return unifex::schedule_after(scheduler, count * 50ms);
  });
```

### `allocate(Sender sender) -> Sender`

Takes a Sender and produces a new Sender that will heap-allocate its operation
state rather than embedding its operation state into the parent operation-state.

This can be used to avoid bloating parent operation-state objects with a large
child operation-state that might only be used part of the time.

Uses the allocator returned by `get_allocator(receiver)`.

The allocator to be used can be customised by injecting an allocator using the
`with_allocator()` algorithm.

### `with_query_value(Sender sender, CPO cpo, T value) -> Sender`

Wraps `sender` in a new sender that will pass a receiver to `connect()`
on `sender` that customises CPO to return the specified value.

This can be used to inject contextual information into child operations.

For example:
```c++
inline constexpr unspecified get_some_property = {}; // Some CPO

sender auto some_async_operation() { ... }

sender auto inject_context() {
  // Inject the value '42' as the result of 'get_some_property()' when queried
  // by child operations of some_async_operation().
  return with_query_value(some_async_operation(), get_some_property, 42);
}
```

### `with_allocator(Sender sender, Allocator allocator) -> Allocator`

Wraps `sender` in a new sender that will injects `allocator` as the
result of `get_allocator()` query on receivers passed to child operations.

Child operations should use this allocator to perform heap allocations.

## Sender Types

### `async_trace_sender`

A sender that will produce the current async stack-trace containing the
chain of continuations for the current async operation.

The stack-trace is represented as a `std::vector<async_trace_entry>` where
the `async_trace_entry` is defined as follows:

```
struct async_trace_entry {
  size_t depth; // depth of this trace entry from the starting point.
  size_t parentIndex; // index into vector of the parent continuation
  continuation_info continuation; // description of this continuation
};
```

## Sender Queries

### `blocking(const Sender&) -> blocking_kind`

Returns `blocking_kind::never` if the receiver will never be called on the
current thread before `start()` returns.

Returns `blocking_kind::always` if the receiver is guaranteed to be called
on some thread strongly-happens-before `start()` returns.
ie. the caller of `start()` can rely on the receiver having been called
after the `start()` method returns.

Returns `blocking_kind::always_inline` if the receiver is guaranteed to be
called inline on the current thread before `start()` returns.

Otherwise returns `blocking_kind::maybe`.

Senders can customise this algorithm by providing an overload of
`tag_invoke(tag_t<blocking>, const your_sender_type&)`.

## Stream Algorithms

### `adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `next(stream)` and `cleanup(stream)` senders.

### `adapt_stream(Stream stream, Func nextAdaptor, Func cleanupAdaptor) -> Stream`

Applies `nextAdaptor()` to `next(stream)` and
applies `cleanupAdaptor()` to `cleanup(stream)`.

### `next_adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `next(stream)` only.
The `cleanup(stream)` Sender is passed through unchanged.

### `reduce_stream(Stream stream, T initialState, Func reducer) -> Sender<T>`

Applies `state = func(state, value)` for each value produced by `stream`.
Returns a Sender that returns the final value.

### `for_each(Stream stream, Func func) -> Sender<void>`

Executes `func(value)` for each value produced by stream.
Returned sender completes with `set_value()` once end of stream is reached.

Stream types can customise this algorithm via ADL by providing an overload
of `tag_invoke(tag_t<for_each>, your_stream_type, Func)`.

### `transform_stream(Stream stream, Func func) -> Stream`

Returns a stream that produces values that are the result of calling
`func(value)` on each value produced by the input stream.

### `via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified scheduler's
execution context.

Note that this works with streams that do not declare the types that they
send, but incurs a heap-allocation per value.

### `typed_via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified
scheduler's execution context.

This differs from `via_stream()` in that it requires that the stream
declares what overloads of `set_value()` and `set_error()` it will call by
providing the `value_types`/`error_types` type aliases.

### `on_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that ensures `next(stream)` is started on the specified
scheduler's execution context.

### `type_erase<Ts...>(Stream stream) -> type_erased_stream<Ts...>`

Type-erases the stream.
Stream must produce value packs of type `(Ts...,)`.

### `take_until(Stream source, Stream trigger) -> Stream`

Returns a stream that will produce values from 'source' until the 'trigger'
stream produces any of value/error/done.

### `single(Sender sender) -> Stream`

Returns a stream that will produce the result of `sender` as the result
of the first element of the stream. If this is a 'value' then it will
produce `done()` as the second element of the stream.

### `stop_immediately<Ts...>(Stream stream) -> Stream`

Returns a stream that will immediately send `set_done()` from a pending `next()`
when stop is requested on the provided stop-token.

The request to stop will be passed on to the upstream `next()` call but
it will not wait for that stream to respond to cancellation before sending
`set_done()`.

The abandoned `next(stream)` call will be waited-for by the `cleanup(stream)`.

Any `set_value()` produced by an abandoned `next()` call is discarded.
Any `set_error()` produced by an abandoned `next()` call is reported in
the `cleanup()` result.

### `delay(Stream stream, TimeScheduler scheduler, Duration d) -> Stream`

Adapts `stream` to produce a new stream that delays the delivery of each
value, done and error signal by the specified duration.

## Stream Types

### `range_stream`

Produces a sequence of `int` values within a given range.
Mainly used for testing purposes.

### `type_erased_stream<Ts...>`

A type-erased stream that produces a sequence of value packs of type `(Ts, ...)`.
ie. calls to `set_value()` will be passed arguments of type `Ts&&...`

### `never_stream`

A stream whose `next()` completes with `set_done()` once when stop is requested.

Note that using this stream with a stop-token where `stop_possible()` returns
`false` will result in a memory-leak. The `next()` operation will never
complete.

## Scheduler Algorithms

### `schedule(Scheduler schedule) -> SenderOf<void>`

This is the basis operation for a scheduler.

The `schedule` operation returns a sender that is a lazy async operation.

A schedule operation logically enqueues an item onto the scheduler's queue when `start()`
is called and the operation completes when some thread associated with the scheduler's
execution context dequeues that item.

The operation signals completion by invoking either the `set_value()`,
`set_done()` or `set_error()` methods on the receiver passed to `connect()`.

As the operation completes on the execution context, the `set_value()` method by definition
be called on that execution context. Applications can therefore use the `schedule()`
operation to execute logic on the associated execution context by placing that logic within
the body of `set_value()`.

### `schedule() -> SenderOf<void>`

This is like `schedule(scheduler)` above but uses the implicit scheduler
obtained from the receiver passed to `connect()` by a calling `get_scheduler(receiver)`.

## Scheduler Types

### `inline_scheduler`

The `schedule()` operation immediately invokes the receiver inline
upon calling `start()`.

### `single_thread_context`

Spawns a single background thread that executes tasks scheduled to it.

Call the `.get_scheduler()` method to obtain a scheduler that can be
used to schedule work to this thread.

### `trampoline_scheduler`

An inline scheduler that only allows invoking a maximum number of
operations inline recursively after which time it schedules subsequent
work to run once the call-stack has unwound back to the first call.

### `timed_single_thread_context`

A single-threaded execution context that supports scheduling work at a
particular time via either `schedule_at()` with a time-point or
`schedule_after()` with a delay in addition to the regular `schedule()`
operation which is equivalent to calling `schedule_at()` with the current
time.

Obtain a TimeScheduler by calling the `.get_scheduler()` method.

### `thread_unsafe_event_loop`

An execution context that assumes all accesses to the scheduler are from the same
thread. It does not do any thread-synchronisation internally.

Supports `schedule_at()` and `schedule_after()` operations in addition to
the base `schedule()` operation.

Obtain a TimeScheduler to schedule work onto this context by calling the
`.get_scheduler()` method.

### `new_thread_context`

An execution context that implements the `schedule()` operation by spawning
a new thread to schedule the call to `set_value()`.

If thread creation fails then the `schedule()` operation can fail and `set_error()`
will be called on the receiver inline with the call to `start()`.

The `new_thread_context` keeps track of the threads that have been created
and the destructor will ensure that all of these threads are joined before
returning.

### `linux::io_uring_context`

An I/O event loop execution context that makes use of the Linux io_uring APIs
to perform asynchronous file I/O.

You must call `.run()` from some thread to process tasks and I/O completions
posted to the I/O thread. Only a single call to `.run()` is allowed to execute
at a time.

The `.get_scheduler()` method returns a TimeScheduler object that can be used
to schedule work onto the I/O thread, using the `schedule()` or `schedule_at()`
CPOs.

You can also call one of the following CPOs, passing the scheduler obtained from
a given `io_uring_context`, to open a file:
* `open_file_read_only(scheduler, path) -> AsyncReadFile`
* `open_file_write_only(scheduler, path) -> AsyncWriteFile`
* `open_file_read_write(scheduler, path) -> AsyncReadWriteFile`

You can then use the following CPOs to read from and/or write to that file.
* `async_read_some_at(AsyncReadFile& file, AsyncReadFile::offset_t offset, span<std::byte> buffer)`
* `async_write_some_at(AsyncWriteFile& file, AsyncWriteFile::offset_t offset, span<const std::byte> buffer)`

These CPOs both return a `SenderOf<ssize_t>` that produces the number of bytes written.

For files associated with the `io_uring_context`, these operations will always complete
on the associated on the thread that is calling `run()` on the associated context.

## StopToken Types

### `unstoppable_token`

A trivial stop-token that can never be stopped.

This is used as the default stop-token for the `get_stop_token()`
customisation point.

### `inplace_stop_token` and `inplace_stop_source`

A stop token that can have stop requested via the corresponding
stop-source. The stop-token holds a reference to the stop-source
rather than heap-allocating some shared state. The caller must make
sure that all callbacks are deregistered and that any stop-tokens are
destroyed before the stop-source is destructed.

This is a less-safe but more efficient version of `std::stop_token`
proposed in [P0660R10](https://wg21.link/P0660R10).


## Synchronisation Primitives

### `async_mutex`

A mutex that allows acquiring the mutex asynchronously.

```c++
namespace unifex
{
  class async_mutex {
  public:
    async_mutex() noexcept;
    async_mutex(async_mutex&&) = delete;
    async_mutex(const async_mutex&) = delete;
    ~async_mutex();

    // Attempt to acquire the mutex lock synchronously.
    // Returns true if successful, false otherwise.
    // If the lock is acquired then the caller is responsible for releasing
    // the lock by calling unlock().
    bool try_lock() noexcept;

    // Acquire the mutex lock asynchronously.
    // Returns a sender that will complete when the lock has been
    // acquired. The caller is then responsible for calling unlock()
    // to release the mutex.
    sender auto async_lock() noexcept;

    // Unlock the mutex.
    // Only valid to call if you currently own the mutex lock.
    //
    // This will cause the next 'async_lock' operation in the queue to complete
    // (if any).
    void unlock() noexcept;
  };
};
```
