#include <cerrno>
#include <cstddef>
#include <iostream>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

auto io_uring_setup(auto entries, io_uring_params *p) {
  auto ret = syscall(__NR_io_uring_setup, entries, p);
  return (ret < 0) ? -errno : ret;
}
auto io_uring_enter(auto ring_fd, auto to_submit, auto min_complete,
                    auto flags) {
  auto ret = syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                     flags, NULL, 0);
  return (ret < 0) ? -errno : ret;
}
auto app_setup_uring() {
  constexpr auto ring_depth = 20;
  io_uring_params params{};
  auto ring_fd = io_uring_setup(ring_depth, &params);
  if (ring_fd < 0) {
    std::cout << "IO uring set-up failed" << std::endl;
    return 1;
  }
  return 0;
}

int main() {
  if (app_setup_uring() != 1) {
    std::cout << "IO uring set-up success" << std::endl;
  }
}
