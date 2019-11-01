#include <unifex/sync_wait.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/for_each.hpp>
#include <unifex/transform.hpp>
#include <unifex/range_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  sync_wait(transform(
      cpo::for_each(
          transform_stream(
              range_stream{0, 10}, [](int value) { return value * value; }),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));

  return 0;
}
