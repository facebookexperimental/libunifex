#include <unifex/type_list.hpp>

using namespace unifex;

void verify_unique_type_list_elements() {
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<>>,
    type_list<>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<int>>,
    type_list<int>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<int, int>>,
    type_list<int>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<int, double, int>>,
    type_list<int, double>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<int, double, double, int>>,
    type_list<int, double>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<int, double, float, double, int>>,
    type_list<int, double, float>
  >);
  static_assert(std::is_same_v<
    unique_type_list_elements_t<type_list<double, int>>,
    type_list<double, int>
  >);
}

void verify_concat_type_lists_unique() {
  static_assert(std::is_same_v<
    concat_type_lists_unique_t<type_list<int, int>, type_list<int>>,
    type_list<int>
  >);
  static_assert(std::is_same_v<
    concat_type_lists_unique_t<type_list<int>, type_list<int, int>>,
    type_list<int>
  >);
  static_assert(std::is_same_v<
    concat_type_lists_unique_t<type_list<bool, int, double>, type_list<double, int, float>>,
    type_list<bool, int, double, float>
  >);
}
