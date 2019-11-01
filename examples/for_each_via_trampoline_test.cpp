#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/trampoline_scheduler.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/typed_via_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  sync_wait(transform(
      cpo::for_each(
          typed_via_stream(
              trampoline_scheduler{},
              transform_stream(
                  range_stream{0, 10'000},
                  [](int value) { return value * value; })),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done"); }));

  return 0;
}
