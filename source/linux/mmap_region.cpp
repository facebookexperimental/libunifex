#include <unifex/linux/mmap_region.hpp>

#include <sys/mman.h>

namespace unifex::linux {

mmap_region::~mmap_region() {
  if (size_ > 0) {
    ::munmap(ptr_, size_);
  }
}

} // namespace unifex::linux
