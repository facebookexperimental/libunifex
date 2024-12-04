/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <unifex/coroutine.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// This header defines data-structures used to represent an async stack-trace.
//
// These data-structures are intended for use by coroutines (and possibly other
// representations of async operations) to allow the current program to record
// an async-stack as a linked-list of async-stack-frames in a similar way to
// how a normal thread represents the stack as a linked-list of stack frames.
//
// From a high-level, just looking at the AsyncStackRoot/AsyncStackFrame
// data-structures, each thread maintains a linked-list of active AsyncStack
// chains that looks a bit like this.
//
//  Stack Register
//      |
//      V
//  Stack Frame   currentStackRoot (TLS)
//      |               |
//      V               V
//  Stack Frame <- AsyncStackRoot  -> AsyncStackFrame -> AsyncStackFrame -> ...
//      |               |
//      V               |
//  Stack Frame         |
//      :               |
//      V               V
//  Stack Frame <- AsyncStackRoot  -> AsyncStackFrame -> AsyncStackFrame -> ...
//      |               |
//      V               X
//  Stack Frame
//      :
//      V
//
// Whenever a thread enters an event loop or is about to execute some
// asynchronus callback/continuation the current thread registers an
// AsyncStackRoot and records the stack-frame of the normal thread
// stack that corresponds to that call so that each AsyncStackRoot
// can be later interleaved with a normal thread stack-trace at
// the appropriate location.
//
// Each AsyncStackRoot contains a pointer to the currently active
// AsyncStackFrame (if any). This AsyncStackFrame forms the head
// of a linked-list of AsyncStackFrame objects that represent the
// async stack-trace. Each non-head AsyncStackFrame is a suspended
// asynchronous operation, which is typically suspended waiting for
// the previous operation to complete.
//
//
// The following diagram shows in more detail how each of the fields
// in these data-structures relate to each other and how the
// async-stack interleaves with the normal thread-stack.
//
//      Current Thread Stack
//      ====================
// +------------------------------------+ <--- current top of stack
// | Normal Stack Frame                 |
// | - stack-base-pointer  ---.         |
// | - return-address         |         |          Thread Local Storage
// |                          |         |          ====================
// +--------------------------V---------+
// |         ...                        |     +-------------------------+
// |                          :         |     | - currentStackRoot  -.  |
// |                          :         |     |                      |  |
// +--------------------------V---------+     +----------------------|--+
// | Normal Stack Frame                 |                            |
// | - stack-base-pointer  ---.         |                            |
// | - return-address         |      .-------------------------------`
// |                          |      |  |
// +--------------------------V------|--+
// | Active Async Operation          |  |
// | (Callback or Coroutine)         |  |            Heap Allocated
// | - stack-base-pointer  ---.      |  |            ==============
// | - return-address         |      |  |
// | - pointer to async state | --------------> +-------------------------+
// |   (e.g. coro frame or    |      |  |       | Coroutine Frame         |
// |    future core)          |      |  |       | +---------------------+ |
// |                          |      |  |       | | Promise             | |
// +--------------------------V------|--+       | | +-----------------+ | |
// |   Event  / Callback             |  |   .------>| AsyncStackFrame | | |
// |   Loop     Callsite             |  |   |   | | | - parentFrame  --------.
// | - stack-base-pointer  ---.      |  |   |   | | | - instructionPtr| | |  |
// | - return-address         |      |  |   |   | | | - stackRoot -.  | | |  |
// |                          |      |  |   |   | | +--------------|--+ | |  |
// |  +--------------------+  |      |  |   |   | | ...            |    | |  |
// |  | AsyncStackRoot     |<--------`  |   |   | +----------------|----+ |  |
// |  | - topFrame   -----------------------`   | ...              |      |  |
// |  | - stackFramePtr -. |<---------------,   +------------------|------+  |
// |  | - nextRoot --.   | |  |         |   |                      |         |
// |  +--------------|---|-+  |         |   '----------------------`         |
// +-----------------|---V----V---------+       +-------------------------+  |
// |         ...     |                  |       | Coroutine Frame         |  |
// |                 |        :         |       |                         |  |
// |                 |        :         |       |  +-------------------+  |  |
// +-----------------|--------V---------+       |  | AsyncStackFrame   |<----`
// | Async Operation |                  |       |  | - parentFrame   --------.
// | (Callback/Coro) |                  |       |  | - instructionPtr  |  |  |
// |                 |        :         |       |  | - stackRoot       |  |  |
// |                 |        :         |       |  +-------------------+  |  |
// +-----------------|--------V---------+       +-------------------------+  |
// |  Event Loop /   |                  |                                    :
// |  Callback Call  |                  |                                    :
// | - frame-pointer | -------.         |                                    V
// | - return-address|        |         |
// |                 |        |         |      Another chain of potentially
// |  +--------------V-----+  |         |      unrelated AsyncStackFrame
// |  | AsyncStackRoot     |  |         |       +---------------------+
// |  | - topFrame  ---------------- - - - - >  | AsyncStackFrame     |
// |  | - stackFramePtr -. |  |         |       | - parentFrame -.    |
// |  | - nextRoot -.    | |  |         |       +----------------|----+
// |  +-------------|----|-+  |         |                        :
// |                |    |    |         |                        V
// +----------------|----V----V---------+
// |         ...    :                   |
// |                V                   |
// |                                    |
// +------------------------------------+
//
//
// This data-structure can be inspected from within the current process
// if desired, but is also intended to allow tools such as debuggers or
// profilers that are inspecting the memory of this process remotely.

struct AsyncStackRoot;
struct AsyncStackFrame;
namespace detail {
class ScopedAsyncStackRoot;
}  // namespace detail

// Get access to the current thread's top-most AsyncStackRoot.
//
// Returns nullptr if there is no active AsyncStackRoot.
[[nodiscard]] AsyncStackRoot* tryGetCurrentAsyncStackRoot() noexcept;

// Get access to the current thread's top-most AsyncStackRoot.
//
// Assumes that there is a current AsyncStackRoot.
[[nodiscard]] AsyncStackRoot& getCurrentAsyncStackRoot() noexcept;

// Exchange the current thread's active AsyncStackRoot with the
// specified AsyncStackRoot pointer, returning the old AsyncStackRoot
// pointer.
//
// This is intended to be used to update the thread-local pointer
// when context-switching fiber stacks.
[[nodiscard]] AsyncStackRoot*
exchangeCurrentAsyncStackRoot(AsyncStackRoot* newRoot) noexcept;

// Perform some consistency checks on the specified AsyncStackFrame,
// assuming that it is the currently active AsyncStackFrame.
void checkAsyncStackFrameIsActive(
    const unifex::AsyncStackFrame& frame) noexcept;

// Activate the specified AsyncStackFrame on the specified AsyncStackRoot,
// setting it as the current 'topFrame'.
//
// The AsyncStackRoot must be the current thread's top-most AsyncStackRoot
// and it must not currently have an active 'topFrame'.
//
// This is typically called immediately prior to executing a callback that
// resumes the async operation represented by 'frame'.
void activateAsyncStackFrame(
    unifex::AsyncStackRoot& root, unifex::AsyncStackFrame& frame) noexcept;

// Deactivate the specified AsyncStackFrame, clearing the current 'topFrame'.
//
// Typically called when the current async operation completes or is suspended
// and execution is about to return from the callback to the executor's event
// loop.
void deactivateAsyncStackFrame(unifex::AsyncStackFrame& frame) noexcept;

// Push the 'callee' frame onto the current thread's async stack, deactivating
// the 'caller' frame and setting up the 'caller' to be the parent-frame of
// the 'callee'.
//
// The 'caller' frame must be the current thread's active frame.
//
// After this call, the 'callee' frame will be the current thread's active
// frame.
//
// This is typically used when one async operation is about to transfer
// execution to a child async operation. e.g. via a coroutine symmetric
// transfer.
void pushAsyncStackFrameCallerCallee(
    unifex::AsyncStackFrame& callerFrame,
    unifex::AsyncStackFrame& calleeFrame) noexcept;

// Pop the 'callee' frame off the stack, restoring the parent frame as the
// current frame.
//
// This is typically used when the current async operation completes and
// you are about to call/resume the caller. e.g. performing a symmetric
// transfer to the calling coroutine in final_suspend().
//
// If calleeFrame.getParentFrame() is null then this method is equivalent
// to deactivateAsyncStackFrame(), leaving no active AsyncStackFrame on
// the current AsyncStackRoot.
void popAsyncStackFrameCallee(unifex::AsyncStackFrame& calleeFrame) noexcept;

void popAsyncStackFrameFromCaller(
    unifex::AsyncStackFrame& callerFrame) noexcept;

// Get a pointer to a special frame that can be used as the root-frame
// for a chain of AsyncStackFrame that does not chain onto a normal
// call-stack.
//
// The caller should never modify this frame as it will be shared across
// many frames and threads. The implication of this restriction is that
// you should also never activate this frame.
AsyncStackFrame& getDetachedRootAsyncStackFrame() noexcept;

// Given an initial AsyncStackFrame, this will write `addresses` with
// the return addresses of the frames in this async stack trace, up to
// `maxAddresses` written.
// This assumes `addresses` has `maxAddresses` allocated space available.
// Returns the number of frames written.
std::size_t getAsyncStackTraceFromInitialFrame(
    unifex::AsyncStackFrame* initialFrame,
    std::uintptr_t* addresses,
    std::size_t maxAddresses);

#if !UNIFEX_NO_COROUTINES

// Resume the specified coroutine after installing a new AsyncStackRoot
// on the current thread and setting the specified AsyncStackFrame as
// the current async frame.
UNIFEX_NO_INLINE void resumeCoroutineWithNewAsyncStackRoot(
    coro::coroutine_handle<> h, AsyncStackFrame& frame) noexcept;

// Resume the specified coroutine after installing a new AsyncStackRoot
// on the current thread and setting the coroutine's associated
// AsyncStackFrame, obtained by calling promise.getAsyncFrame(), as the
// current async frame.
template <typename Promise>
void resumeCoroutineWithNewAsyncStackRoot(
    coro::coroutine_handle<Promise> h) noexcept;

#endif  // FOLLY_HAS_COROUTINES

// The following lldb command will make instruction_ptrs more legible
//  type summary add unifex::instruction_ptr --summary-string "${var.p_%A}"
struct instruction_ptr final {
  constexpr instruction_ptr(std::nullptr_t = nullptr) noexcept : p_(nullptr) {}

  explicit constexpr instruction_ptr(void* p) noexcept : p_(p) {}

  // Gets the instruction pointer of the return-address of the current function.
  //
  // Generally a function that uses this macro should be declared FOLLY_NOINLINE
  // to prevent this returning surprising results in cases where the function
  // is inlined.
#if UNIFEX_HAS_BUILTIN(__builtin_return_address) && !UNIFEX_NO_ASYNC_STACKS
  static constexpr instruction_ptr
  read_return_address(void* p = __builtin_return_address(0)) noexcept {
#else
  static constexpr instruction_ptr
  read_return_address(void* p = nullptr) noexcept {
#endif
    return instruction_ptr{p};
  }

  explicit operator std::uintptr_t() const noexcept {
    return reinterpret_cast<std::uintptr_t>(p_);
  }

private:
  void* p_;

  friend constexpr bool
  operator==(instruction_ptr lhs, instruction_ptr rhs) noexcept {
    return lhs.p_ == rhs.p_;
  }

  friend constexpr bool
  operator!=(instruction_ptr lhs, instruction_ptr rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct frame_ptr {
  constexpr frame_ptr(std::nullptr_t = nullptr) noexcept : p_(nullptr) {}

  explicit constexpr frame_ptr(void* p) noexcept : p_(p) {}

  // Gets pointer to the current function invocation's stack-frame.
  //
  // Generally a function that uses this macro should be declared FOLLY_NOINLINE
  // to prevent this returning surprising results in cases where the function
  // is inlined.
#if UNIFEX_HAS_BUILTIN(__builtin_frame_address) && !UNIFEX_NO_ASYNC_STACKS
  static constexpr frame_ptr
  read_frame_pointer(void* p = __builtin_frame_address(0)) noexcept {
#else
  static constexpr frame_ptr read_frame_pointer(void* p = nullptr) noexcept {
#endif
    return frame_ptr{p};
  }

  explicit operator void*() const noexcept { return p_; }

private:
  void* p_;

  friend constexpr bool operator==(frame_ptr lhs, frame_ptr rhs) noexcept {
    return lhs.p_ == rhs.p_;
  }

  friend constexpr bool operator!=(frame_ptr lhs, frame_ptr rhs) noexcept {
    return !(lhs == rhs);
  }
};

// An async stack frame contains information about a particular
// invocation of an asynchronous operation.
//
// For example, asynchronous operations implemented using coroutines
// would have each coroutine-frame contain an instance of AsyncStackFrame
// to record async-stack trace information for that coroutine invocation.
struct AsyncStackFrame {
public:
  AsyncStackFrame() = default;

  // The parent frame is the frame of the async operation that is logically
  // the caller of this frame.
  AsyncStackFrame* getParentFrame() noexcept;
  const AsyncStackFrame* getParentFrame() const noexcept;
  void setParentFrame(AsyncStackFrame& frame) noexcept;

  // Get access to the current stack-root.
  //
  // This is only valid for either the root or leaf AsyncStackFrame
  // in a chain of frames.
  //
  // In the case of an active leaf-frame it is used as a cache to
  // avoid accessing the thread-local when pushing/popping frames.
  // In the case of the root frame (which has a null parent frame)
  // it points to an AsyncStackRoot that contains information about
  // the normal-stack caller.
  AsyncStackRoot* getStackRoot() noexcept;

  // The return address is generallty the address of the code in the
  // caller that will be executed when the operation owning the current
  // frame completes.
  void setReturnAddress(
      instruction_ptr p = instruction_ptr::read_return_address()) noexcept;
  instruction_ptr getReturnAddress() const noexcept;

private:
  friend AsyncStackRoot;

  friend AsyncStackFrame& getDetachedRootAsyncStackFrame() noexcept;
  friend void activateAsyncStackFrame(
      unifex::AsyncStackRoot&, unifex::AsyncStackFrame&) noexcept;
  friend void deactivateAsyncStackFrame(unifex::AsyncStackFrame&) noexcept;
  friend void pushAsyncStackFrameCallerCallee(
      unifex::AsyncStackFrame&, unifex::AsyncStackFrame&) noexcept;
  friend void
  checkAsyncStackFrameIsActive(const unifex::AsyncStackFrame&) noexcept;
  friend void popAsyncStackFrameCallee(unifex::AsyncStackFrame&) noexcept;

  // Pointer to the async caller's stack-frame info.
  //
  // This forms a linked-list of frames that make up a stack.
  // The list is terminated by a null pointer which indicates
  // the top of the async stack - either because the operation
  // is detached or because the next frame is a thread that is
  // blocked waiting for the async stack to complete.
  AsyncStackFrame* parentFrame = nullptr;

  // Instruction pointer of the caller of this frame.
  // This will typically be either the address of the continuation
  // of this asynchronous operation, or the address of the code
  // that launched this asynchronous operation. May be null
  // if the address is not known.
  //
  // Typically initialised with the result of a call to
  // instruction_ptr::read_return_address().
  instruction_ptr instructionPointer;

  // Pointer to the stack-root for the current thread.
  // Cache this in each async-stack frame so we don't have to
  // read from a thread-local to get the pointer.
  //
  // This pointer is only valid for the top-most stack frame.
  // When a frame is pushed or popped it should be copied to
  // the next frame, etc.
  //
  // The exception is for the bottom-most frame (ie. where
  // parentFrame == null). In this case, if stackRoot is non-null
  // then it points to a root that is currently blocked on some
  // thread waiting for the async work to complete. In this case
  // you can find the information about the stack-frame for that
  // thread in the AsyncStackRoot and can use it to continue
  // walking the stack-frames.
  AsyncStackRoot* stackRoot = nullptr;
};

// A stack-root represents the context of an event loop
// that is running some asynchronous work. The current async
// operation that is being executed by the event loop (if any)
// is pointed to by the 'topFrame'.
//
// If the current event loop is running nested inside some other
// event loop context then the 'nextRoot' points to the AsyncStackRoot
// context for the next event loop up the stack on the current thread.
//
// The 'stackFramePtr' holds a pointer to the normal stack-frame
// that is currently executing this event loop. This allows
// reconciliation of the parts between a normal stack-trace and
// the start of the async-stack trace.
//
// The current thread's top-most context (the head of the linked
// list of contexts) is obtained by calling getCurrentAsyncStackRoot().
struct AsyncStackRoot {
public:
  // Sets the top-frame to be 'frame' and also updates the cached
  // 'frame.stackRoot' to be 'this'.
  //
  // The current stack root must not currently have any active
  // frame.
  void setTopFrame(AsyncStackFrame& frame) noexcept;
  AsyncStackFrame* getTopFrame() const noexcept;

  // Initialises this stack root with information about the context
  // in which the stack-root was declared. This records information
  // about where the async-stack-trace should be spliced into the
  // normal stack-trace.
  void setStackFrameContext(
      frame_ptr fp = frame_ptr::read_frame_pointer(),
      instruction_ptr ip = instruction_ptr::read_return_address()) noexcept;
  frame_ptr getStackFramePointer() const noexcept;
  instruction_ptr getReturnAddress() const noexcept;

  const AsyncStackRoot* getNextRoot() const noexcept;
  void setNextRoot(AsyncStackRoot* next) noexcept;

private:
  friend class detail::ScopedAsyncStackRoot;
  friend void activateAsyncStackFrame(
      unifex::AsyncStackRoot&, unifex::AsyncStackFrame&) noexcept;
  friend void deactivateAsyncStackFrame(unifex::AsyncStackFrame&) noexcept;
  friend void pushAsyncStackFrameCallerCallee(
      unifex::AsyncStackFrame&, unifex::AsyncStackFrame&) noexcept;
  friend void
  checkAsyncStackFrameIsActive(const unifex::AsyncStackFrame&) noexcept;
  friend void popAsyncStackFrameCallee(unifex::AsyncStackFrame&) noexcept;

  // Pointer to the currently-active AsyncStackFrame for this event
  // loop or callback invocation. May be null if this event loop is
  // not currently executing any async operations.
  //
  // This is atomic primarily to enforce visibility of writes to the
  // AsyncStackFrame that occur before the topFrame in other processes,
  // such as profilers/debuggers that may be running concurrently
  // with the current thread.
  std::atomic<AsyncStackFrame*> topFrame{nullptr};

  // Pointer to the next event loop context lower on the current
  // thread's stack.
  // This is nullptr if this is not a nested call to an event loop.
  AsyncStackRoot* nextRoot = nullptr;

  // Pointer to the stack-frame and return-address of the function
  // call that registered this AsyncStackRoot on the current thread.
  // This is generally the stack-frame responsible for executing async
  // callbacks (typically an event-loop).
  // Anything prior to this frame on the stack in the current thread
  // is potentially unrelated to the call-chain of the current async-stack.
  //
  // Typically initialised with frame_ptr::read_frame_pointer() or
  // setStackFrameContext().
  frame_ptr stackFramePtr;

  // Typically initialise with instruction_ptr::read_return_address() or
  // setStackFrameContext().
  instruction_ptr returnAddress;
};

namespace detail {

class ScopedAsyncStackRoot {
public:
  explicit ScopedAsyncStackRoot(
      frame_ptr framePointer = frame_ptr::read_frame_pointer(),
      instruction_ptr returnAddress =
          instruction_ptr::read_return_address()) noexcept;
  ~ScopedAsyncStackRoot();

  void activateFrame(AsyncStackFrame& frame) noexcept {
    unifex::activateAsyncStackFrame(root_, frame);
  }

  void ensureFrameDeactivated(
      [[maybe_unused]] AsyncStackFrame* possiblyDeadFrame) noexcept {
    // this is like deactivateAsyncStackFrame(*possibleDeadFrame) but it
    // doesn't make assertions about the state of the possibly-dead frame
    assert(tryGetCurrentAsyncStackRoot() == &root_);
    [[maybe_unused]] auto topFrame =
        root_.topFrame.exchange(nullptr, std::memory_order_relaxed);
    assert(topFrame == nullptr || topFrame == possiblyDeadFrame);
  }

private:
  AsyncStackRoot root_;
};

}  // namespace detail
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>

#include <unifex/tracing/async_stack-inl.hpp>
