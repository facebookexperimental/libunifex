#include <unifex/for_each.hpp>
#include <unifex/range_stream.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/transform.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/via_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {
  single_thread_context context;

  sync_wait(transform(
      cpo::for_each(
          via_stream(
              context.get_scheduler(),
              transform_stream(
                  range_stream{0, 10},
                  [](int value) { return value * value; })),
          [](int value) { std::printf("got %i\n", value); }),
      []() { std::printf("done\n"); }));

  return 0;
}
