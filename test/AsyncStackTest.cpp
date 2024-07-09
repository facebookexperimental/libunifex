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

#include <unifex/tracing/async_stack.hpp>

#if __has_include(<pthread.h>)
#  include <pthread.h>
#  define UNIFEX_NO_PTHREAD 0
#else
#  define UNIFEX_NO_PTHREAD 1
#endif

#include <gtest/gtest.h>

TEST(AsyncStack, ScopedAsyncStackRoot) {
  unifex::frame_ptr const stackFramePtr =
      unifex::frame_ptr::read_frame_pointer();
  unifex::instruction_ptr const returnAddress =
      unifex::instruction_ptr::read_return_address();

  ASSERT_TRUE(unifex::tryGetCurrentAsyncStackRoot() == nullptr);

  {
    unifex::detail::ScopedAsyncStackRoot scopedRoot{
        stackFramePtr, returnAddress};
    auto* root = unifex::tryGetCurrentAsyncStackRoot();
    ASSERT_FALSE(root == nullptr);

    unifex::AsyncStackFrame frame;
    scopedRoot.activateFrame(frame);

    ASSERT_EQ(root, frame.getStackRoot());
    ASSERT_EQ(stackFramePtr, root->getStackFramePointer());
    ASSERT_EQ(returnAddress, root->getReturnAddress());
    ASSERT_EQ(&frame, root->getTopFrame());

    unifex::deactivateAsyncStackFrame(frame);

    ASSERT_TRUE(frame.getStackRoot() == nullptr);
    ASSERT_TRUE(root->getTopFrame() == nullptr);
  }

  ASSERT_TRUE(unifex::tryGetCurrentAsyncStackRoot() == nullptr);
}

TEST(AsyncStack, PushPop) {
  unifex::detail::ScopedAsyncStackRoot scopedRoot{nullptr};

  auto& root = unifex::getCurrentAsyncStackRoot();

  unifex::AsyncStackFrame frame1;
  unifex::AsyncStackFrame frame2;
  unifex::AsyncStackFrame frame3;

  scopedRoot.activateFrame(frame1);

  ASSERT_EQ(&frame1, root.getTopFrame());
  ASSERT_EQ(&root, frame1.getStackRoot());

  unifex::pushAsyncStackFrameCallerCallee(frame1, frame2);

  ASSERT_EQ(&frame2, root.getTopFrame());
  ASSERT_EQ(&frame1, frame2.getParentFrame());
  ASSERT_EQ(&root, frame2.getStackRoot());
  ASSERT_TRUE(frame1.getStackRoot() == nullptr);

  unifex::pushAsyncStackFrameCallerCallee(frame2, frame3);

  ASSERT_EQ(&frame3, root.getTopFrame());
  ASSERT_EQ(&frame2, frame3.getParentFrame());
  ASSERT_EQ(&frame1, frame2.getParentFrame());
  ASSERT_TRUE(frame1.getParentFrame() == nullptr);
  ASSERT_TRUE(frame2.getStackRoot() == nullptr);

  unifex::deactivateAsyncStackFrame(frame3);

  ASSERT_TRUE(root.getTopFrame() == nullptr);
  ASSERT_TRUE(frame3.getStackRoot() == nullptr);

  unifex::activateAsyncStackFrame(root, frame3);

  ASSERT_EQ(&frame3, root.getTopFrame());
  ASSERT_EQ(&root, frame3.getStackRoot());

  unifex::popAsyncStackFrameCallee(frame3);

  ASSERT_EQ(&frame2, root.getTopFrame());
  ASSERT_EQ(&root, frame2.getStackRoot());
  ASSERT_TRUE(frame3.getStackRoot() == nullptr);

  unifex::popAsyncStackFrameCallee(frame2);

  ASSERT_EQ(&frame1, root.getTopFrame());
  ASSERT_EQ(&root, frame1.getStackRoot());
  ASSERT_TRUE(frame2.getStackRoot() == nullptr);

  unifex::deactivateAsyncStackFrame(frame1);

  ASSERT_TRUE(root.getTopFrame() == nullptr);
  ASSERT_TRUE(frame1.getStackRoot() == nullptr);
}
