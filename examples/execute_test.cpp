#include <unifex/single_thread_context.hpp>
#include <unifex/execute.hpp>

#include <cstdio>
#include <cassert>

using namespace unifex;

int main() {
    single_thread_context ctx;

    for (int i = 0; i < 5; ++i) {
        execute(ctx.get_scheduler(), [i]() {
            printf("hello execute() %i\n", i);
        });
    }

    return 0;
}
