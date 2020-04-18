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

// No include guard or pragma once. This file is intended to be included
// multiple times.

#ifdef UNIFEX_PROLOGUE_HPP
#error Prologue has already been included
#endif
#define UNIFEX_PROLOGUE_HPP

#define template(...) UNIFEX_TEMPLATE(__VA_ARGS__)
#define AND UNIFEX_AND
