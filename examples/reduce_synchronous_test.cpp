#include <unifex/sync_wait.hpp>
#include <unifex/transform_stream.hpp>
#include <unifex/reduce_stream.hpp>
#include <unifex/transform.hpp>
#include <unifex/range_stream.hpp>

#include <cstdio>

using namespace unifex;

int main() {

  int finalResult;
  sync_wait(transform(
      reduce_stream(
          transform_stream(
              range_stream{0, 10},
              [](int value) { return value * value; }),
          0,
          [](int state, int value) { return state + value; }),
      [&](int result) { finalResult = result; }));

  std::printf("result = %i\n", finalResult);

  return 0;
}
