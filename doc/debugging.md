# Async Stack Traces

Unifex contains a prototype implementation of async stack-traces that
allows you to traverse a chain/graph of async continuations.

A stack-trace consists of a stack of `continuation_info` objects that
describes the address of the "frame" and the type of the continuation
as well as a mechanism to query what the next continuations in the chain are.

This allows you to traverse from a leaf receiver back to the original task
that launched it. If you are using structured concurrency and have represented
your application as a structured set of tasks then this chain should progress
all the way back to the root task of your application.

Each receiver must customise the `visit_continuations()` CPO to be able to
participate in the async stack-walk. Otherwise, the stack-walk will terminate
when it reaches that receiver.

Example:
```c++
template<typename Receiver>
struct my_receiver {
  Receiver wrappedReceiver_;

  void set_value() && noexcept;
  void set_error(std::exception_ptr) && noexcept;
  void set_done() && noexcept;

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>, const my_receiver& r, Func&& func) {
    std::invoke(func, wrappedReceiver_);
  }
};
```

## Capturing the current stack-trace

There is a helper sender called `async_trace_sender` that you can use to
get a dump of the async stack-trace at any point in a sender expression.
It will produce a `std::vector<async_trace_entry>` that contains a description
of the async stack at this point.

For example: Some helpers to dump an async trace.
```c++
auto dump_async_trace(std::string tag = {}) {
  return then(
      async_trace_sender{},
      [tag = std::move(tag)](const std::vector<async_trace_entry>& entries) {
        std::cout << "Async Trace (" << tag << "):\n";
        for (auto& entry : entries) {
          std::cout << " " << entry.depth << " [-> " << entry.parentIndex
                    << "]: " << entry.continuation.type().name() << " @ 0x";
          std::cout.setf(std::ios::hex, std::ios::basefield);
          std::cout << entry.continuation.address();
          std::cout.unsetf(std::ios::hex);
          std::cout << "\n";
        }
      });
}

template <typename Sender>
auto dump_async_trace_on_start(Sender&& sender, std::string tag = {}) {
  return unifex::sequence(dump_async_trace(std::move(tag)), (Sender &&) sender);
}

template <typename Sender>
auto dump_async_trace_on_completion(Sender&& sender, std::string tag = {}) {
  return unifex::finally(
      (Sender &&) sender, dump_async_trace(std::move(tag)));
}
```
