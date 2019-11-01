#include <unifex/range_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/typed_via_stream.hpp>

#include <cstdio>

using namespace unifex;

// This test uses the trampoline_scheduler to avoid stack-overflow due to very
// deep recursion from a reduce over a synchronous stream.

int main() {
  sync_wait(transform(
      reduce_stream(
          typed_via_stream(
              trampoline_scheduler{},
              transform_stream(
                  range_stream{0, 100'000},
                  [](int value) { return value * value; })),
          0,
          [](int state, int value) { return state + 10 * value; }),
      [&](int result) { std::printf("result: %i\n", result); }));

  return 0;
}
