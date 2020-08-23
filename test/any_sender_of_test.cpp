/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unifex/any_sender_of.hpp>

#include <unifex/finally.hpp>
#include <unifex/just.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/transform.hpp>

#include "mock_receiver.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <string>
#include <tuple>
#include <variant>

using namespace unifex;
using namespace unifex_test;
using namespace testing;

// We need to validate the following contract:
//  - any_sender_of<T...> is a typed_sender
//  - sender_traits<any_sender_of<T...>>::value_types<std::variant, std::tuple>
//    is std::variant<std::tuple<T...>>
//  - sender_traits<any_sender_of<T...><::error_types<std::variant> is
//    std::variant<std::exception_ptr>
//  - any_sender_of<T...> is constructible from just(declval<T>()...)
//  - connect(any_sender_of<T...>{}, mock_receiver<T...>{}):
//     - invokes nothing on the receiver immediately, but
//     - returns an operation state that, when started, acts on the receiver
//       as if the wrapped sender and receiver were directly connected
//  - there is no confusion when an any_sender_of<T...> is connect with a
//    receiver that can receive more than one kind of tuple
//
// Stretch:
//  - all operations that would be noexcept with directly connected sender and
//    receiver pairs are also noexcept when the same pair is connected through
//    an any_sender_of/any_receiver_of pair

template <typename Sig>
struct AnySenderOfTest;

template <typename... T, bool Noexcept>
struct AnySenderOfTest<void(T...) noexcept(Noexcept)> : Test {
  using any_sender = any_sender_of<T...>;

  static constexpr size_t value_count = sizeof...(T);

  static_assert(typed_sender<any_sender>);

  static_assert(sender_to<any_sender, mock_receiver<void(T...)>>);

  using any_sender_traits = sender_traits<any_sender>;

  static_assert(std::is_same_v<std::variant<std::tuple<T...>>,
                               typename any_sender_traits::template value_types<std::variant, std::tuple>>);

  static_assert(std::is_same_v<std::variant<std::exception_ptr>,
                               typename any_sender_traits::template error_types<std::variant>>);

  static auto default_just() {
    if constexpr (value_count == 0) {
      return just();
    }
    else if constexpr (value_count == 1) {
      return just(42);
    }
    else if constexpr (value_count == 2) {
      return just(42, std::string{"hello"});
    }
  }

  template <typename U>
  static auto& expect_set_value_call(U& receiver) noexcept {
    if constexpr (value_count == 0) {
      return EXPECT_CALL(receiver, set_value());
    }
    else if constexpr (value_count == 1) {
      return EXPECT_CALL(receiver, set_value(_));
    }
    else if constexpr (value_count == 2) {
      return EXPECT_CALL(receiver, set_value(_, _));
    }
  }
};

using AnySenderOfTestTypes = Types<
    void(),
    void() noexcept,
    void(int),
    void(int) noexcept,
    void(int, std::string),
    void(int, std::string) noexcept>;

TYPED_TEST_CASE(AnySenderOfTest, AnySenderOfTestTypes);

namespace {

template <typename SenderSig, typename ReceiverSig = SenderSig, typename... ExtraReceiverSigs>
void testWrappingAJust() noexcept {
  using test_t = AnySenderOfTest<SenderSig>;
  using any_sender = typename test_t::any_sender;

  any_sender sender = test_t::default_just();

  mock_receiver<ReceiverSig, ExtraReceiverSigs...> receiver;

  auto op = connect(std::move(sender), receiver);

  test_t::expect_set_value_call(*receiver)
      .WillOnce(Invoke([](auto... values) noexcept {
        if constexpr (test_t::value_count == 0) {
          EXPECT_EQ(std::tuple{}, std::make_tuple(std::move(values)...));
        } else if constexpr (test_t::value_count == 1) {
          EXPECT_EQ(std::tuple{42}, std::make_tuple(std::move(values)...));
        } else {
          static_assert(test_t::value_count == 2, "Unimplemented");

          EXPECT_EQ((std::tuple{42, std::string{"hello"}}), std::make_tuple(std::move(values)...));
        }
      }));

  start(op);
}

}

TYPED_TEST(AnySenderOfTest, AnySenderOfCanWrapAJust) {
  testWrappingAJust<TypeParam>();
}

TYPED_TEST(AnySenderOfTest, AnySenderOfCanConnectToAMultiReceiver) {
  testWrappingAJust<TypeParam, void(), void(int), void(int, std::string), void(int, int, int)>();
}

TYPED_TEST(AnySenderOfTest, AnySenderOfCanBeCancelled) {
  using test_t = AnySenderOfTest<TypeParam>;
  using any_sender = typename test_t::any_sender;

  any_sender sender = finally(test_t::default_just(), ready_done_sender{});

  mock_receiver<TypeParam> receiver;

  auto op = connect(std::move(sender), receiver);

  EXPECT_CALL(*receiver, set_done()).Times(1);

  start(op);
}

#if !UNIFEX_NO_EXCEPTIONS
TYPED_TEST(AnySenderOfTest, AnySenderOfCanError) {
  using test_t = AnySenderOfTest<TypeParam>;
  using any_sender = typename test_t::any_sender;

  any_sender sender = finally(test_t::default_just(),
      transform(just(), [] {
        throw std::runtime_error("uh oh");
      }));

  mock_receiver<TypeParam> receiver;

  auto op = connect(std::move(sender), receiver);

  EXPECT_CALL(*receiver, set_error(_))
      .WillOnce(Invoke([](std::exception_ptr eptr) noexcept {
        ASSERT_TRUE(eptr);

        EXPECT_NO_THROW({
          try {
            std::rethrow_exception(eptr);
          } catch (std::runtime_error& e) {
            EXPECT_STREQ("uh oh", e.what());
          }
        });
      }));

  start(op);
}
#endif // !UNIFEX_NO_EXCEPTIONS

