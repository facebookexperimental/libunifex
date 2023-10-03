/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/type_erased_stream.hpp>

#include <unifex/async_auto_reset_event.hpp>
#include <unifex/for_each.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/never.hpp>
#include <unifex/on_stream.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>
#include <unifex/when_all.hpp>

#include <cstdio>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
single_thread_context context1;
single_thread_context context2;
}  // namespace

TEST(type_erase, UseType) {
  auto functor = []() -> type_erased_stream<int> {
    return type_erase<int>(via_stream(
        context1.get_scheduler(),
        on_stream(
            context2.get_scheduler(),
            transform_stream(range_stream{0, 10}, [](int value) {
              return value * value;
            }))));
  };
  sync_wait(then(
      for_each(functor(), [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));
}

TEST(type_erase, Smoke) {
  sync_wait(then(
      for_each(
          type_erase<int>(via_stream(
              context1.get_scheduler(),
              on_stream(
                  context2.get_scheduler(),
                  transform_stream(
                      range_stream{0, 10},
                      [](int value) { return value * value; })))),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));
}

TEST(type_erase, Pipeable) {
  range_stream{0, 10}                                           //
      | transform_stream(                                       //
            [](int value) { return value * value; })            //
      | on_stream(context2.get_scheduler())                     //
      | via_stream(context1.get_scheduler())                    //
      | type_erase<int>()                                       //
      | for_each(                                               //
            [](int value) { std::printf("got %i\n", value); })  //
      | then([]() { std::printf("done\n"); })                   //
      | sync_wait();
}

TEST(type_erase, InlineCancel) {
  sync_wait(let_value_with_stop_source([](auto& stopSource) {
    return when_all(
        for_each(type_erase<>(never_stream()), []() { std::printf("next\n"); }),
        just_from([&] { stopSource.request_stop(); }));
  }));
}
