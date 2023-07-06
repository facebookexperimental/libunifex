#include <unifex/upon_error.hpp>

#include <unifex/just.hpp>
#include <unifex/just_error.hpp>
#include <unifex/sync_wait.hpp>

#include <gtest/gtest.h>
#include <tuple>
#include <type_traits>
#include <variant>

using namespace unifex;

TEST(UponError, StaticTypeCheck) {
  auto res1 = just(42)
    | upon_error([](auto){return 42;});
  static_assert(std::is_same_v<decltype(res1)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>>>);

  auto res2 = just()
    | upon_error([](auto){
        return 2;
      });
  static_assert(std::is_same_v<decltype(res2)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<>, std::tuple<int>>>);

  auto res3 = just(42)
    | upon_error([](auto){
        return;
      });
  static_assert(std::is_same_v<decltype(res3)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<>>>);

  auto res4 = just(42)
    | upon_error([](auto){
        return 2.0;
      });
  static_assert(std::is_same_v<decltype(res4)::value_types<std::variant, std::tuple>,
      std::variant<std::tuple<int>, std::tuple<double>>>);
}

TEST(UponError, Working) {
  int val = 0;
  auto res = sync_wait(upon_error(just_error(42), [&](auto err_val) {
    val = err_val;
    return 2;
  }));
  EXPECT_EQ(val, 42);
  EXPECT_EQ(res.value(), 2);
}

TEST(UponError, Pipeable) {
  int val = 0;
  auto res = just_error(42) 
    | upon_error([&](auto err_val) {
        val = err_val;
        return 2;
      })
    | sync_wait();
  EXPECT_EQ(val, 42);
  EXPECT_EQ(res.value(), 2);
}

TEST(UponError, NotCalled) {
  int val = 0;
  auto res = just(42)
    | upon_error([&](auto) {
      val++;
      return 2;
    })
    | sync_wait();
  EXPECT_EQ(val, 0);
  EXPECT_EQ(res.value(), 42);
}

TEST(UponError, ExceptionHandling) {
  int val = 0;
  try{
    just(42)
      | upon_error([&](auto) {
        val = 1;
        throw 2;
        return 2;
      })
      | sync_wait();
  } catch(int err){
    EXPECT_EQ(err, 2);
  }
  EXPECT_EQ(val, 0);
}

struct single_value_sender {

  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<int>>;

  template <template <typename...> class Variant>
  using error_types = Variant<>;

  static constexpr bool sends_done = false;

  template <class Receiver>
  struct operation {
    friend auto tag_invoke(tag_t<start>, operation& self) noexcept {
      set_value(std::move(self.receiver), 0);
    }

    Receiver receiver;
  };

  template <class Receiver>
  friend auto tag_invoke(tag_t<connect>, single_value_sender, Receiver&& receiver) {
    return operation<Receiver>{std::forward<Receiver>(receiver)};
  }
};

TEST(UponError, ZeroErrorSender) {
  auto s = single_value_sender{} | upon_error([](auto) -> double { return 0.0; });
  static_assert(std::is_same_v<
    decltype(s)::value_types<std::variant, std::tuple>,
    std::variant<std::tuple<int>>
  >);
}

struct Error1{};
struct Error2{};
struct Error3{};
struct Error4{};

struct many_error_sender {

  template <
    template <typename...> class Variant,
    template <typename...> class Tuple>
  using value_types = Variant<Tuple<double>>;

  template <template <typename...> class Variant>
  using error_types = Variant<Error1, Error2, Error3>;

  static constexpr bool sends_done = false;

  template <class Receiver>
  struct operation {
    friend auto tag_invoke(tag_t<start>, operation& self) noexcept {
      set_error(std::move(self.receiver), Error1{});
    }

    Receiver receiver;
  };

  template <class Receiver>
  friend auto tag_invoke(tag_t<connect>, many_error_sender, Receiver&& receiver) {
    return operation<Receiver>{std::forward<Receiver>(receiver)};
  }
};

TEST(UponError, ManyErrorSender) {
  auto s = many_error_sender{} | upon_error([](auto e) {
    if constexpr (std::is_same_v<decltype(e), Error3>) {
      return Error4{};
    } else {
      return e;
    }
  });
  static_assert(std::is_same_v<
    decltype(s)::value_types<std::variant, std::tuple>,
    std::variant<std::tuple<double>, std::tuple<Error1>, std::tuple<Error2>, std::tuple<Error4>>
  >);
}

TEST(UponError, ManyErrorSenderAllReturnInt) {
  auto s = many_error_sender{} | upon_error([](auto) -> int {
    return 0;
  });
  static_assert(std::is_same_v<
    decltype(s)::value_types<std::variant, std::tuple>,
    std::variant<std::tuple<double>, std::tuple<int>>
  >);
}

TEST(UponError, ManyErrorSenderIntoVoid) {
  auto s = many_error_sender{} | upon_error([](auto e) {
    if constexpr (std::is_same_v<decltype(e), Error3>) {
      return;
    } else {
      return e;
    }
  });
  static_assert(std::is_same_v<
    decltype(s)::value_types<std::variant, std::tuple>,
    std::variant<std::tuple<double>, std::tuple<Error1>, std::tuple<Error2>, std::tuple<>>
  >);
}
