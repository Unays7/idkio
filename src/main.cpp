#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

template <typename T> T *offset_ptr(void *base, size_t offset) {
  return (T *)((char *)base + offset);
}

struct SharedRing {
  int ring_fd;

  // submission queue pointers
  unsigned *sq_head;
  unsigned *sq_tail;
  unsigned *sq_mask;
  unsigned *sq_array;
  io_uring_sqe *sqes;

  // completion queue pointers
  unsigned *cq_head;
  unsigned *cq_tail;
  unsigned *cq_mask;
  io_uring_cqe *cqes;
};

auto io_uring_setup(auto entries, io_uring_params *p) {
  auto ret = syscall(__NR_io_uring_setup, entries, p);
  return (ret < 0) ? -errno : ret;
}

// not needed if SQPolling is enabled
auto io_uring_enter(auto ring_fd, auto to_submit, auto min_complete,
                    auto flags) {
  auto ret = syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                     flags, NULL, 0);
  return (ret < 0) ? -errno : ret;
}

auto app_setup_uring(SharedRing &ring) {
  constexpr auto ring_depth = 20;
  io_uring_params params{};
  auto ring_fd = io_uring_setup(ring_depth, &params);
  if (ring_fd < 0) {
    std::cout << "IO uring set-up failed" << std::endl;
    return 1;
  }

  auto sring_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);
  auto cring_sz = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);

  void *sq_ptr, *cq_ptr;

  if (params.features & IORING_FEAT_SINGLE_MMAP) {
    auto ring_sz = std::max(sring_sz, cring_sz);
    sq_ptr = mmap(nullptr, ring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    cq_ptr = sq_ptr;
  } else {
    sq_ptr = mmap(nullptr, sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    cq_ptr = mmap(nullptr, cring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);
  }

  auto sqes_ptr = mmap(nullptr, params.sq_entries * sizeof(io_uring_sqe),
                       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                       ring_fd, IORING_OFF_SQES);

  ring.ring_fd = ring_fd;
  ring.sq_head = offset_ptr<unsigned>(sq_ptr, params.sq_off.head);
  ring.sq_tail = offset_ptr<unsigned>(sq_ptr, params.sq_off.tail);
  ring.sq_mask = offset_ptr<unsigned>(sq_ptr, params.sq_off.ring_mask);
  ring.sq_array = offset_ptr<unsigned>(sq_ptr, params.sq_off.array);
  ring.sqes = (io_uring_sqe *)sqes_ptr;

  ring.cq_head = offset_ptr<unsigned>(cq_ptr, params.cq_off.head);
  ring.cq_tail = offset_ptr<unsigned>(cq_ptr, params.cq_off.tail);
  ring.cq_mask = offset_ptr<unsigned>(cq_ptr, params.cq_off.ring_mask);
  ring.cqes = offset_ptr<io_uring_cqe>(cq_ptr, params.cq_off.cqes);

  return 0;
}

int main() {
  SharedRing ring{};
  if (app_setup_uring(ring) != 1) {
    std::cout << "IO uring set-up success" << std::endl;
  }
}
