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

#include <unifex/continuations.hpp>

#include <cstdio>

#include <gtest/gtest.h>

struct noop_operation {
    auto start() {
        return unifex::noop_continuation;
    }
};

TEST(continuation, noop_continuation) {
    noop_operation op;
    run_continuation(op.start());
}

struct conditional_operation {
    bool cond = false;
    int state = 0;

    struct true_continuation : unifex::continuation_base<conditional_operation, true_continuation> {
        using base_type::base_type;

        unifex::noop_continuation_handle resume() {
            std::puts("true branch");
            op_->state = 1;
            return unifex::noop_continuation;
        }

        void destroy() {}
    };

    struct false_continuation : unifex::continuation_base<conditional_operation, false_continuation> {
        using base_type::base_type;

        unifex::noop_continuation_handle resume() {
            std::puts("false branch");
            op_->state = 2;
            return unifex::noop_continuation;
        }

        void destroy() {}
    };

    unifex::variant_continuation_handle<true_continuation, false_continuation> start() {
        std::puts("start");
        if (cond) {
            return true_continuation{*this};
        } else {
            return false_continuation{*this};
        }
    }
};

TEST(continuation, conditional_test) {
    {
        conditional_operation op{true};
        unifex::run_continuation(op.start());
          assert(op.state == 1);
    }
    {
        conditional_operation op{false};
        unifex::run_continuation(op.start());
          assert(op.state == 2);
    }
}

struct nested_conditional_operation {
    bool cond1 = false;
    bool cond2 = false;
    int state = 0;

    struct true_true_continuation : unifex::continuation_base<nested_conditional_operation, true_true_continuation> {
        using base_type::base_type;

        unifex::noop_continuation_handle resume() {
            std::puts("true true branch");
            op_->state = 1;
            return unifex::noop_continuation;
        }

        void destroy() {}
    };

    struct true_false_continuation : unifex::continuation_base<nested_conditional_operation, true_false_continuation> {
        using base_type::base_type;

        unifex::noop_continuation_handle resume() {
            std::puts("true false branch");
            op_->state = 2;
            return unifex::noop_continuation;
        }

        void destroy() {}
    };

    struct true_continuation : unifex::continuation_base<nested_conditional_operation, true_continuation> {
        using base_type::base_type;

        unifex::variant_continuation_handle<true_true_continuation, true_false_continuation> resume() {
            std::puts("true branch");
            op_->state = 4;
            if (op_->cond2) {
                return true_true_continuation{*op_};
            } else {
                return true_false_continuation{*op_};
            }
        }

        void destroy() {}
    };

    struct false_continuation : unifex::continuation_base<nested_conditional_operation, false_continuation> {
        using base_type::base_type;

        unifex::noop_continuation_handle resume() {
            std::puts("false branch");
            op_->state = 3;
            return unifex::noop_continuation;
        }

        void destroy() {}
    };

    unifex::variant_continuation_handle<true_continuation, false_continuation> start() {
        std::puts("start");
        if (cond1) {
            return true_continuation{*this};
        } else {
            return false_continuation{*this};
        }
    }
};

void nested_conditional_test(bool cond1, bool cond2) {


}

TEST(continuation, nested_conditional_test) {
    {
        nested_conditional_operation op{true, true};
        unifex::run_continuation(op.start());
        assert(op.state == 1);
    }
    {
        nested_conditional_operation op{true, false};
        unifex::run_continuation(op.start());
        assert(op.state == 2);
    }
    {
        nested_conditional_operation op{false, true};
        unifex::run_continuation(op.start());
        assert(op.state == 3);
    }
    {
        nested_conditional_operation op{false, false};
        unifex::run_continuation(op.start());
        assert(op.state == 3);
    }
}

struct looping_operation {
    int x = 0;

    struct step_1_continuation;

    struct maybe_step_1_continuation : unifex::nullable_continuation_base<looping_operation, maybe_step_1_continuation> {
        using base_type::base_type;

        step_1_continuation resume() noexcept {
            assert(op_);
            return step_1_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    static_assert(std::is_default_constructible_v<maybe_step_1_continuation>);

    struct step_2_continuation : unifex::continuation_base<looping_operation, step_2_continuation> {
        using base_type::base_type;
        
        maybe_step_1_continuation resume() noexcept {
            std::puts("step 2");
            op_->x += 1;
            if (op_->x > 5) {
                return maybe_step_1_continuation{};
            } else {
                return maybe_step_1_continuation{*op_};
            }
        }

        void destroy() noexcept {}
    };

    struct step_1_continuation : unifex::continuation_base<looping_operation, step_1_continuation> {
        using base_type::base_type;
        
        step_2_continuation resume() noexcept {
            std::puts("step 1");
            op_->x += 1;
            return step_2_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    step_1_continuation start() noexcept {
        std::puts("start");
        x = 1;
        return step_1_continuation{*this};
    }

    unifex::any_continuation_handle type_erased_start() noexcept {
        return start();
    }
};

TEST(continuation, looping_test) {
    looping_operation op;
    unifex::run_continuation(op.start());
}

TEST(continuation, type_erased_looping_test) {
    looping_operation op;
    unifex::run_continuation(op.type_erased_start());
}

struct looping_with_variants_operation {
    int x = 0;

    struct step_1_continuation;

    struct step_2_continuation : unifex::continuation_base<looping_with_variants_operation, step_2_continuation> {
        using base_type::base_type;
        
        unifex::variant_continuation_handle<step_1_continuation, unifex::null_continuation_handle> resume() noexcept {
            std::puts("step 2");
            op_->x += 1;
            if (op_->x > 5) {
                return step_1_continuation{*op_};
            } else {
                return unifex::null_continuation_handle{};
            }
        }

        void destroy() noexcept {}
    };

    struct step_1_continuation : unifex::continuation_base<looping_with_variants_operation, step_1_continuation> {
        using base_type::base_type;
        
        step_2_continuation resume() noexcept {
            std::puts("step 1");
            op_->x += 1;
            return step_2_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    step_1_continuation start() noexcept {
        std::puts("start");
        x = 1;
        return step_1_continuation{*this};
    }

    unifex::any_continuation_handle type_erased_start() noexcept {
        return start();
    }

    void destroy() {
        std::puts("destroy");
    }
};

TEST(continuation, looping_with_variants_test) {
    looping_with_variants_operation op;
    unifex::run_continuation(op.start());
}

TEST(continuation, type_erased_looping_with_variants_test) {
    looping_with_variants_operation op;
    unifex::run_continuation(op.type_erased_start());
}

struct complex_looping_with_variants_operation {
    //      .---------.
    //      V         |
    // S -> 1 -> 2 -> 3
    //           |
    //          noop
    int x = 0;

    struct step_1_continuation;
    struct step_2_continuation;

    struct step_3_continuation : unifex::continuation_base<complex_looping_with_variants_operation, step_3_continuation> {
        using base_type::base_type;
        
        step_1_continuation resume() noexcept {
            std::puts("step 3");
            op_->x += 1;
            return step_1_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    struct step_2_continuation : unifex::continuation_base<complex_looping_with_variants_operation, step_2_continuation> {
        using base_type::base_type;
        
        unifex::variant_continuation_handle<step_3_continuation, unifex::noop_continuation_handle> resume() noexcept {
            std::puts("step 2");
            if (op_->x < 5) {
                return unifex::noop_continuation;
            }
            return step_3_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    struct step_1_continuation : unifex::continuation_base<complex_looping_with_variants_operation, step_1_continuation> {
        using base_type::base_type;
        
        step_2_continuation resume() noexcept {
            std::puts("step 1");
            op_->x += 1;
            return step_2_continuation{*op_};
        }

        void destroy() noexcept {}
    };

    step_1_continuation start() noexcept {
        std::puts("start");
        x = 1;
        return step_1_continuation{*this};
    }

    unifex::any_continuation_handle type_erased_start() noexcept {
        return start();
    }

    void destroy() {
        std::puts("destroy");
    }
};

TEST(continuation, complex_looping_with_variants) {
    complex_looping_with_variants_operation op;
    unifex::run_continuation(op.start());
}

TEST(continuation, type_erased_complex_looping_with_variants) {
    complex_looping_with_variants_operation op;
    unifex::run_continuation(op.type_erased_start());
}

struct collatz_operation {
    int x;

    template<typename T>
    using base_t = unifex::continuation_base<collatz_operation, T>;

    struct check_termination_step;
    struct iterate_step;

    struct divide_by_two_step : base_t<divide_by_two_step> {
        using base_type::base_type;

        check_termination_step resume() {
            op_->x /= 2;
            return check_termination_step{*op_};
        }
    };

    struct times_three_add_one_step : base_t<times_three_add_one_step> {
        using base_type::base_type;

        iterate_step resume() {
            op_->x = op_->x * 3 + 1;
            return iterate_step{*op_};
        }
    };

    struct iterate_step : base_t<iterate_step> {
        using base_type::base_type;

        unifex::variant_continuation_handle<times_three_add_one_step, divide_by_two_step> resume() {
            if ((op_->x % 2) == 0) {
                return divide_by_two_step{*op_};
            } else {
                return times_three_add_one_step{*op_};
            }
        }
    };

    struct check_termination_step : base_t<check_termination_step> {
        using base_type::base_type;

        unifex::variant_continuation_handle<unifex::noop_continuation_handle, iterate_step> resume() {
            std::printf("%i\n", op_->x);
            if (op_->x == 1) {
                return unifex::noop_continuation;
            } else {
                return iterate_step{*op_};
            }
        }
    };

    check_termination_step start() {
        return check_termination_step{*this};
    }
};

TEST(continuation, collatz_state_machine) {
    collatz_operation op{7};
    unifex::run_continuation(op.start());
}
