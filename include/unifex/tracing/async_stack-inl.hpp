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

namespace unifex {

inline void checkAsyncStackFrameIsActive(
    [[maybe_unused]] const unifex::AsyncStackFrame& frame) noexcept {
  (void)frame;
  assert(frame.stackRoot != nullptr);
  assert(tryGetCurrentAsyncStackRoot() == frame.stackRoot);
  assert(frame.stackRoot->topFrame.load(std::memory_order_relaxed) == &frame);
}

inline void activateAsyncStackFrame(
    unifex::AsyncStackRoot& root, unifex::AsyncStackFrame& frame) noexcept {
  assert(tryGetCurrentAsyncStackRoot() == &root);
  root.setTopFrame(frame);
}

inline void deactivateAsyncStackFrame(unifex::AsyncStackFrame& frame) noexcept {
  checkAsyncStackFrameIsActive(frame);
  frame.stackRoot->topFrame.store(nullptr, std::memory_order_relaxed);
  frame.stackRoot = nullptr;
}

inline void pushAsyncStackFrameCallerCallee(
    unifex::AsyncStackFrame& callerFrame,
    unifex::AsyncStackFrame& calleeFrame) noexcept {
  checkAsyncStackFrameIsActive(callerFrame);
  calleeFrame.stackRoot = callerFrame.stackRoot;
  calleeFrame.parentFrame = &callerFrame;
  calleeFrame.stackRoot->topFrame.store(
      &calleeFrame, std::memory_order_release);

  // Clearing out non-top-frame's stackRoot is not strictly necessary
  // but it may help with debugging.
  callerFrame.stackRoot = nullptr;
}

inline void
popAsyncStackFrameCallee(unifex::AsyncStackFrame& calleeFrame) noexcept {
  checkAsyncStackFrameIsActive(calleeFrame);
  auto* callerFrame = calleeFrame.parentFrame;
  auto* stackRoot = calleeFrame.stackRoot;
  if (callerFrame != nullptr) {
    callerFrame->stackRoot = stackRoot;
  }
  stackRoot->topFrame.store(callerFrame, std::memory_order_release);

  // Clearing out non-top-frame's stackRoot is not strictly necessary
  // but it may help with debugging.
  calleeFrame.stackRoot = nullptr;
}

inline void popAsyncStackFrameFromCaller(
    [[maybe_unused]] unifex::AsyncStackFrame& callerFrame) noexcept {
  auto root = tryGetCurrentAsyncStackRoot();
  assert(root != nullptr);
  auto topFrame = root->getTopFrame();
  assert(topFrame != nullptr);
  assert(topFrame->getParentFrame() == &callerFrame);
  popAsyncStackFrameCallee(*topFrame);
}

inline std::size_t getAsyncStackTraceFromInitialFrame(
    unifex::AsyncStackFrame* initialFrame,
    std::uintptr_t* addresses,
    std::size_t maxAddresses) {
  std::size_t numFrames = 0;
  for (auto* frame = initialFrame; frame != nullptr && numFrames < maxAddresses;
       frame = frame->getParentFrame()) {
    addresses[numFrames++] =
        static_cast<std::uintptr_t>(frame->getReturnAddress());
  }
  return numFrames;
}

#if !UNIFEX_NO_COROUTINES

template <typename Promise>
void resumeCoroutineWithNewAsyncStackRoot(
    coro::coroutine_handle<Promise> h) noexcept {
  resumeCoroutineWithNewAsyncStackRoot(h, h.promise().getAsyncFrame());
}

#endif

inline AsyncStackFrame* AsyncStackFrame::getParentFrame() noexcept {
  return parentFrame;
}

inline const AsyncStackFrame* AsyncStackFrame::getParentFrame() const noexcept {
  return parentFrame;
}

inline void AsyncStackFrame::setParentFrame(AsyncStackFrame& frame) noexcept {
  parentFrame = &frame;
}

inline AsyncStackRoot* AsyncStackFrame::getStackRoot() noexcept {
  return stackRoot;
}

inline void AsyncStackFrame::setReturnAddress(instruction_ptr p) noexcept {
  instructionPointer = p;
}

inline instruction_ptr AsyncStackFrame::getReturnAddress() const noexcept {
  return instructionPointer;
}

inline void AsyncStackRoot::setTopFrame(AsyncStackFrame& frame) noexcept {
  assert(this->topFrame.load(std::memory_order_relaxed) == nullptr);
  assert(frame.stackRoot == nullptr);
  frame.stackRoot = this;
  this->topFrame.store(&frame, std::memory_order_release);
}

inline AsyncStackFrame* AsyncStackRoot::getTopFrame() const noexcept {
  return topFrame.load(std::memory_order_relaxed);
}

inline void AsyncStackRoot::setStackFrameContext(
    frame_ptr framePtr, instruction_ptr ip, std::thread::id tId) noexcept {
  stackFramePtr = framePtr;
  returnAddress = ip;
  threadId = tId;
}

inline frame_ptr AsyncStackRoot::getStackFramePointer() const noexcept {
  return stackFramePtr;
}

inline instruction_ptr AsyncStackRoot::getReturnAddress() const noexcept {
  return returnAddress;
}

inline const AsyncStackRoot* AsyncStackRoot::getNextRoot() const noexcept {
  return nextRoot;
}

inline void AsyncStackRoot::setNextRoot(AsyncStackRoot* next) noexcept {
  nextRoot = next;
}

}  // namespace unifex
