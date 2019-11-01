#include <unifex/linux/safe_file_descriptor.hpp>

#include <cassert>

#include <unistd.h>

namespace unifex::linux {

void safe_file_descriptor::close() noexcept {
  assert(valid());
  [[maybe_unused]] int result = ::close(std::exchange(fd_, -1));
  assert(result == 0);
}

} // namespace unifex::linux
