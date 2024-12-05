/*
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 
 */
#include <unifex/type_list.hpp>

using namespace unifex;

void verify_unique_type_list_elements() {
  static_assert(
      std::is_same_v<unique_type_list_elements_t<type_list<>>, type_list<>>);
  static_assert(std::is_same_v<
                unique_type_list_elements_t<type_list<int>>,
                type_list<int>>);
  static_assert(std::is_same_v<
                unique_type_list_elements_t<type_list<int, int>>,
                type_list<int>>);
  static_assert(std::is_same_v<
                unique_type_list_elements_t<type_list<int, double, int>>,
                type_list<int, double>>);
  static_assert(
      std::is_same_v<
          unique_type_list_elements_t<type_list<int, double, double, int>>,
          type_list<int, double>>);
  static_assert(std::is_same_v<
                unique_type_list_elements_t<
                    type_list<int, double, float, double, int>>,
                type_list<int, double, float>>);
  static_assert(std::is_same_v<
                unique_type_list_elements_t<type_list<double, int>>,
                type_list<double, int>>);
}

void verify_concat_type_lists_unique() {
  static_assert(std::is_same_v<
                concat_type_lists_unique_t<type_list<int, int>, type_list<int>>,
                type_list<int>>);
  static_assert(std::is_same_v<
                concat_type_lists_unique_t<type_list<int>, type_list<int, int>>,
                type_list<int>>);
  static_assert(std::is_same_v<
                concat_type_lists_unique_t<
                    type_list<bool, int, double>,
                    type_list<double, int, float>>,
                type_list<bool, int, double, float>>);
}
