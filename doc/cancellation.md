# The need for cancellation

## Ordinary functions

When we write sequential, single-threaded synchronous code in C++ we call
a function when we want it to produce some result for us.

The caller presumably needs the result of that function to achieve its goal,
otherwise it wouldn't have called the function. You can think of "goal" here
as roughly equivalent to "satisfying the post-conditions".

If the function call completes with an error (ie. an exception) then this
indicates that the callee was unable to achieve its goal. Thus the caller's
current strategy for achieving the its goal has failed and so the caller
can either handle the error and try a different strategy for achieving its
goal, or if the caller does not have an alternative strategy for achieving
its goal then it can let the error propagate to its caller as a way of
saying that it was not able to achieve its goal.

Either way, a function call will either complete with a value, indicating
success, and the caller will continue executing along the value-path, or
it will complete with an error, indicating failure, and the caller will
execute along the error-path (typically by unwinding until it hits a handler).

As the caller is suspended while the callee is executing and the program
is single-threaded, in many cases there is nothing that can change the fact
that the caller needs that result in order to achieve its goal.

The program can only pursue one strategy for achieving its goal at a time
and so needs to wait until the result of the current strategy is known at
which point it can then make a choice about what to do next.

## Concurrency introduces a need for cancellation

Once we allow a program to execute multiple operations concurrently,
it is possible that we might have multiple strategies for achieving the
goal of a program that are executing at the same time.

For example, the goal of (a part of) the program might be
"try to download this file or stop trying after 30s".

This program might concurrently try download the file and also start
waiting for 30s. If one of these strategies is successful (eg. we finished
waiting for 30s) then this means that the other strategy is no longer needed
as we have already satisfied the goal of this part of the program.

This now means that we have an operation that is currently executing whose
result is no longer required. It would be a waste of resources to let this
operation continue trying to fulfil its goal - it may still be consuming
CPU-cycles, memory and network bandwidth.

Ideally we want this operation to stop executing as soon as possible so
that it can free the resources it is using. This means we need a way to
send a request to this operation to stop executing.

However, we still need to wait until the operation completes before we
know that it has finished releasing its resources.

Note that the concurrent nature of the operations means that there will
typically be a race between the operation completing naturally and completing
early because it received a request to stop.

## Completing with a cancellation result

If an operation receives a request to stop before it completes
then it still needs to signal to the consumer when it has completed.
This means it needs to decide what result it will complete with.

Returning a 'value' may not be appropriate here as this typically indicates
that the "goal" of the operation was achieved. ie. that the post-conditions
were satisfied. However, this may not be the case if the operation stopped
execution early because it was asked to do so.

## Two flavours of cancellation

Note that there are actually two different flavours of cancellation.

There is the concurrent cancellation case, where an operation is currently
executing and some other concurrent operation requests that the operation
stops because its result is no longer required.

However, there is also the lazy cancellation case, where an operation has
not yet started and so can request cancellation of the operation without
need for synchronisation.

For example, a consumer might be processing elements from a stream one
at a time until it sees an element that matches a certain predicate.
When it receives the next element it matches it against the predicate
and then based on the result can either ask for the next element,
by calling the `next()` operation on the stream. Or it can cancel the
stream by calling the `cleanup()` operation on the stream.

As the stream has not yet been asked to produce the next element there
is no need to cancel an already-running operation and so we can cancel
the operation

Another example is a coroutine `generator<T>` type, where we can either
ask for the next element by executing `operator++()` on the iterator or
we can cancel the execution of the generator by calling the generator
destructor.

Another example is a sender that has not yet started. This async operation
can often be trivially cancelled by destroying the sender without starting it.

## References

With thanks to Kirk Shoop and Lisa Lippincott for the framing and terminology
used here. See the paper "Cancellation is serendipitous-success"
([P1677R2](https://wg21.link/P1677R2)) for further reading.

# Cancellation Design

Unifex uses stop-tokens to allow a higher-level operation to communicate a
request to stop to an operation that has already started.

See the section on the "StopToken" concept in the [Concepts documentation](concepts.md).

This requires the consumer of the operation to provide the stop-token when
it launches the operation so that it can later communicate a request to stop
via that token.

In the case of asynchronous operations implemented using the sender/receiver
pattern, the receiver is the consumer of the operation.

Note that this same pattern also works for cancellation of a **ManySender**
operation as well as **Sender** operations.

## Passing a stop-token

There is a `get_stop_token()` customisation point that receivers can customise
to return the stop-token that should be used by senders to check for a request
to stop.

Note that it is entirely optional for a receiver to customise the `get_stop_token()`
CPO, the default is to return the `unstoppable_token` which indicates to the
sender that there will never be a request to stop this operation.

It is also entirely optional for a sender to call this CPO to obtain a stop-token
or to check for a request to stop. A sender that doesn't check for a request to
stop will just run to completion naturally rather than complete early.

Senders and receivers that do not want to participate in cancellation do not have
to use it and should not incur any cognitive or runtime overhead if cancellation
is not used.

## Stop Token Validity

A stop-token obtained from a call to `get_stop_token(r)` on a receiver, `r`, must
be assumed to be invalidated when by a call to a completion-signalling function.
ie. `set_value(std::move(r), values...)`, `set_error(std::move(r), error)` or
`set_done(std::move(r))`.

The sender must ensure that any stop-callback that has been constructed using
the token obtained by calling `get_stop_token()` on a given receiver is
destroyed prior to signalling completion on the receiver.

It is invalid to use a stop token obtained from a call to `get_stop_token()`
after one of the completion-signalling functions has been called on the receiver.
ie. It is invalid to call `.stop_requested()`, `.stop_possible()` or construct
a stop-callback using that token.

This restriction allows more efficient implementations of stop-tokens that
avoid heap-allocations and reference-counting. Stop tokens may be lightweight
handles to a shared resource that is owned by the receiver. The receiver may
destroy this shared resource upon completion, thus invalidating any stop-tokens
that referred to it.

## Completing with cancellation

If an operation completes early because its result is no longer needed
or because the higher-level goal has already been met then it is idiomatic
for a sender to complete by calling the receiver's `set_done()` method.

The `set_done()` method indicates that the operation did not run to
completion and so may not have satisfied its post-conditions. However,
it did not satisfy the post-conditions because it was unable to do so
but rather because it was asked to stop early, typically because its
result is no longer required.

Consumer code that was dependent on the operation completing successfully
should be cancelled (by not executing it) and should generally propagate
the cancellation result as its result (after performing any necessary cleanup).

The exception to this rule is for the operation that actually requested
the child operation to stop. In this case, the operation will typically
handle the 'done' signal and then produce its result (eg. an 'error' or
'value').
