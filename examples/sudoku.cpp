#include <cstdio>
#include <cstdlib>

/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/any_scheduler.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/let_value_with_stop_source.hpp>
#include <unifex/on.hpp>

#include <chrono>
#include <charconv>
#include <iostream>
#include <iomanip>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <thread>
#include <tuple>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

inline constexpr auto sink = [](auto&&...){};

inline constexpr auto discard = then(sink);

template <typename F>
auto defer(F f) {
  return let_value(just(), (F&&) f);
}

//
// this sudoku example was taken from TBB examples/thread_group/sudoku
//

const unsigned BOARD_SIZE = 81;
const unsigned BOARD_DIM = 9;
std::atomic<unsigned> nSols;
std::atomic<unsigned> nPotentialBoards;
std::atomic<unsigned> nDeletedBoards;
bool find_one = false;
bool verbose = false;
unsigned short init_values[BOARD_SIZE] = { 1, 0, 0, 9, 0, 0, 0, 8, 0, 0, 8, 0, 2, 0, 0, 0, 0,
                                           0, 0, 0, 5, 0, 0, 0, 7, 0, 0, 0, 5, 2, 1, 0, 0, 4,
                                           0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 7, 4, 0, 0, 7, 0, 0,
                                           0, 3, 0, 0, 3, 0, 0, 0, 2, 0, 0, 5, 0, 0, 0, 0, 0,
                                           0, 1, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0 };

typedef struct {
    unsigned short solved_element;
    unsigned potential_set;
} board_element;

void read_board(const char *filename) {
    FILE *fp;
    int input;
    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "sudoku: Could not open input file '%s'.\n", filename);
        std::exit(-1);
    }
    for (unsigned i = 0; i < BOARD_SIZE; ++i) {
        if (fscanf(fp, "%d", &input))
            init_values[i] = input;
        else {
            fprintf(stderr, "sudoku: Error in input file at entry %d, assuming 0.\n", i);
            init_values[i] = 0;
        }
    }
    fclose(fp);
}

void print_board(board_element *b) {
    for (unsigned row = 0; row < BOARD_DIM; ++row) {
        for (unsigned col = 0; col < BOARD_DIM; ++col) {
            printf(" %d", b[row * BOARD_DIM + col].solved_element);
            if (col == 2 || col == 5)
                printf(" |");
        }
        printf("\n");
        if (row == 2 || row == 5)
            printf(" ---------------------\n");
    }
}

void print_potential_board(board_element *b) {
    for (unsigned row = 0; row < BOARD_DIM; ++row) {
        for (unsigned col = 0; col < BOARD_DIM; ++col) {
            if (b[row * BOARD_DIM + col].solved_element)
                printf("  %4d ", b[row * BOARD_DIM + col].solved_element);
            else
                printf(" [%4d]", b[row * BOARD_DIM + col].potential_set);
            if (col == 2 || col == 5)
                printf(" |");
        }
        printf("\n");
        if (row == 2 || row == 5)
            printf(" ------------------------------------------------------------------\n");
    }
}

void init_board(board_element *b) {
    for (unsigned i = 0; i < BOARD_SIZE; ++i)
        b[i].solved_element = b[i].potential_set = 0;
}

void init_board(board_element *b, unsigned short arr[81]) {
    for (unsigned i = 0; i < BOARD_SIZE; ++i) {
        b[i].solved_element = arr[i];
        b[i].potential_set = 0;
    }
}

void init_potentials(board_element *b) {
    for (unsigned i = 0; i < BOARD_SIZE; ++i)
        b[i].potential_set = 0;
}

void copy_board(board_element *src, board_element *dst) {
    for (unsigned i = 0; i < BOARD_SIZE; ++i)
        dst[i].solved_element = src[i].solved_element;
}

bool fixed_board(board_element *b) {
    for (int i = BOARD_SIZE - 1; i >= 0; --i)
        if (b[i].solved_element == 0)
            return false;
    return true;
}

bool in_row(board_element *b, unsigned row, unsigned col, unsigned short p) {
    for (unsigned c = 0; c < BOARD_DIM; ++c)
        if (c != col && b[row * BOARD_DIM + c].solved_element == p)
            return true;
    return false;
}

bool in_col(board_element *b, unsigned row, unsigned col, unsigned short p) {
    for (unsigned r = 0; r < BOARD_DIM; ++r)
        if (r != row && b[r * BOARD_DIM + col].solved_element == p)
            return true;
    return false;
}

bool in_block(board_element *b, unsigned row, unsigned col, unsigned short p) {
    unsigned b_row = row / 3 * 3, b_col = col / 3 * 3;
    for (unsigned i = b_row; i < b_row + 3; ++i)
        for (unsigned j = b_col; j < b_col + 3; ++j)
            if (!(i == row && j == col) && b[i * BOARD_DIM + j].solved_element == p)
                return true;
    return false;
}

void calculate_potentials(board_element *b) {
    for (unsigned i = 0; i < BOARD_SIZE; ++i) {
        b[i].potential_set = 0;
        if (!b[i].solved_element) { // element is not yet fixed
            unsigned row = i / BOARD_DIM, col = i % BOARD_DIM;
            for (unsigned potential = 1; potential <= BOARD_DIM; ++potential) {
                if (!in_row(b, row, col, potential) && !in_col(b, row, col, potential) &&
                    !in_block(b, row, col, potential))
                    b[i].potential_set |= 1 << (potential - 1);
            }
        }
    }
}

bool valid_board(board_element *b) {
    bool success = true;
    for (unsigned i = 0; i < BOARD_SIZE; ++i) {
        if (success && b[i].solved_element) { // element is fixed
            unsigned row = i / BOARD_DIM, col = i % BOARD_DIM;
            if (in_row(b, row, col, b[i].solved_element) ||
                in_col(b, row, col, b[i].solved_element) ||
                in_block(b, row, col, b[i].solved_element))
                success = false;
        }
    }
    return success;
}

bool examine_potentials(board_element *b, bool *progress) {
    bool singletons = false;
    for (unsigned i = 0; i < BOARD_SIZE; ++i) {
        if (b[i].solved_element == 0 && b[i].potential_set == 0) // empty set
            return false;
        switch (b[i].potential_set) {
            case 1: {
                b[i].solved_element = 1;
                singletons = true;
                break;
            }
            case 2: {
                b[i].solved_element = 2;
                singletons = true;
                break;
            }
            case 4: {
                b[i].solved_element = 3;
                singletons = true;
                break;
            }
            case 8: {
                b[i].solved_element = 4;
                singletons = true;
                break;
            }
            case 16: {
                b[i].solved_element = 5;
                singletons = true;
                break;
            }
            case 32: {
                b[i].solved_element = 6;
                singletons = true;
                break;
            }
            case 64: {
                b[i].solved_element = 7;
                singletons = true;
                break;
            }
            case 128: {
                b[i].solved_element = 8;
                singletons = true;
                break;
            }
            case 256: {
                b[i].solved_element = 9;
                singletons = true;
                break;
            }
        }
    }
    *progress = singletons;
    return valid_board(b);
}

struct any_solve_scheduler;

using SchedulerQueries =
  with_receiver_queries<
    overload<any_solve_scheduler(const this_&)>(unifex::get_scheduler),
    overload<inplace_stop_token(const this_&) noexcept>(get_stop_token)>;

using any_solve_scheduler_impl = SchedulerQueries::any_scheduler;

struct any_solve_scheduler {
  UNIFEX_TEMPLATE (typename Sched)
    (requires (!same_as<Sched, any_solve_scheduler>) UNIFEX_AND
      scheduler<Sched> UNIFEX_AND
      constructible_from<any_solve_scheduler_impl, Sched>)
  any_solve_scheduler(Sched sch)
    : impl_(std::move(sch))
  {}
  auto schedule() const {
    return unifex::schedule(impl_);
  }
  bool operator==(const any_solve_scheduler&) const = default;
private:
  any_solve_scheduler_impl impl_;
};

using SenderQueries =
  with_receiver_queries<
    overload<any_solve_scheduler(const this_&)>(unifex::get_scheduler),
    overload<inplace_stop_token(const this_&) noexcept>(get_stop_token)>;

using any_solve = SenderQueries::any_sender_of<>;

std::atomic<unsigned> partialsolveid;
std::atomic<unsigned> partialsolvestarts;

any_solve partial_solve(board_element *board, unsigned first_potential_set) {
    unsigned id = ++partialsolveid;

    return on(current_scheduler,
    defer([=, first_set = first_potential_set]() -> any_solve {
        unsigned seq = ++partialsolvestarts;
        unsigned first_potential_set = first_set;
        std::unique_ptr<board_element> b(board);
        ++nDeletedBoards;
        if (fixed_board(b.get())) {
            if (++nSols == 1 && verbose) {
                printf("partial_solve id: %u, starts: %u\n", id, seq);
                print_board(b.get());
            }
            if (find_one) {
              return {just_done()};
            }
            return {just()};
        }
        calculate_potentials(b.get());
        bool progress = true;
        bool success = examine_potentials(b.get(), &progress);
        if (success && progress) {
            return {partial_solve(b.release(), first_potential_set)};
        }
        else if (success && !progress) {
            while (b.get()[first_potential_set].solved_element != 0)
                ++first_potential_set;
            auto potential_board = [=, b = std::move(b)](unsigned short potential) -> any_solve {
                if (1 << (potential - 1) & b.get()[first_potential_set].potential_set) {
                    std::unique_ptr<board_element[]> new_board{new board_element[BOARD_SIZE]};
                    copy_board(b.get(), new_board.get());
                    new_board.get()[first_potential_set].solved_element = potential;
                    ++nPotentialBoards;
                    return {partial_solve(new_board.release(), first_potential_set)};
                }
                return {just()};
            };
            return {
              when_all(
                potential_board(1),
                potential_board(2),
                potential_board(3),
                potential_board(4),
                potential_board(5),
                potential_board(6),
                potential_board(7),
                potential_board(8),
                potential_board(9)) 
              | discard
            };
        }
        return {just()};
    }));
}

std::tuple<unsigned, steady_clock::duration> solve(static_thread_pool::scheduler pool) {
    nSols = 0;
    nPotentialBoards = 0;
    nDeletedBoards = 0;
    partialsolveid = 0;
    partialsolvestarts = 0;
    std::unique_ptr<board_element[]> start_board{new board_element[BOARD_SIZE]};
    init_board(start_board.get(), init_values);
    auto start = steady_clock::now();
    ++nPotentialBoards;
    inplace_stop_source stop;
    auto canceled = [](){
      printf("\ncanceled\n\n");
    };
    inplace_stop_token::template callback_type<decltype(canceled)> callback(stop.get_token(), canceled);
    sync_wait(
        partial_solve(start_board.release(), 0) 
        | with_query_value(get_scheduler, pool) 
        | with_query_value(get_stop_token, stop.get_token()));
    return std::make_tuple((unsigned)nSols, steady_clock::now() - start);
}

using double_sec = std::chrono::duration<double>;

int main(int argc, char* argv[]) {
  std::string filename = "";
  bool silent = false;
  auto threadCount = std::thread::hardware_concurrency();

  std::vector<std::string_view> args(argv+1, argv+argc);
  for (auto arg : args) {
    if (arg == "find-one") {
      find_one = true;
    } else if (arg == "verbose") {
      verbose = true;
    } else if (arg == "silent") {
      silent = true;
    } else if (arg.find("filename="sv) == 0) {
      arg.remove_prefix(9);
      filename = arg;
    } else if (arg.find("n-of-threads="sv) == 0) {
      arg.remove_prefix(13);

      auto [ptr, ec] { std::from_chars(arg.data(), arg.data() + arg.size(), threadCount) };
 
      if (ec == std::errc::invalid_argument) {
        printf("That isn't a number.\n");
      } else if (ec == std::errc::result_out_of_range) {
        printf("This number is larger than an int.\n");
      }
    } else {
      printf("unrecognized argument: -> %s", arg.data());
    }
  }

  if (silent)
    verbose = false;

  if (!filename.empty())
    read_board(filename.c_str());

  for (std::uint32_t p = 1; p <= threadCount; ++p) {
    static_thread_pool poolContext(p);
    auto pool = poolContext.get_scheduler();

    any_solve_scheduler pl{pool};
    any_solve_scheduler cs{current_scheduler};

    auto [number, solve_time] = solve(pool);

    if (!silent) {
      if (find_one) {
          printf("Sudoku: Time to find first solution on %d threads: %6.6f seconds.\n",
                 p,
                 std::chrono::duration_cast<double_sec>(solve_time).count());
      }
      else {
        printf("Sudoku: Time to find all %u solutions on %d threads: %6.6f seconds.\n",
               number,
               p,
               std::chrono::duration_cast<double_sec>(solve_time).count());
      }
      if (nPotentialBoards > nDeletedBoards) {
          printf("Leaked %u boards!\n", nPotentialBoards - nDeletedBoards);
      }
    }
  }
  return 0;
}