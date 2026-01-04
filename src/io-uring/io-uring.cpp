#include "io-uring.hpp"
#include <cerrno>
#include <iostream>
#include <linux/fs.h>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace io_uring {

auto io_uring_setup(uint entries, io_uring_params *p) {
  auto ret = syscall(__NR_io_uring_setup, entries, p);
  return (ret < 0) ? -errno : ret;
}

auto io_uring_enter(uint ring_fd, uint to_submit, uint min_complete,
                    uint flags) {
  auto ret = syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                     flags, NULL, 0);
  return (ret < 0) ? -errno : ret;
}

int Ring::init() {
  io_uring_params params{};
  auto ring_fd = io_uring_setup(RING_SIZE, &params);
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

  ring_fd = ring_fd;
  sq_head = offset_ptr<unsigned>(sq_ptr, params.sq_off.head);
  sq_tail = offset_ptr<unsigned>(sq_ptr, params.sq_off.tail);
  sq_mask = offset_ptr<unsigned>(sq_ptr, params.sq_off.ring_mask);
  sq_array = offset_ptr<unsigned>(sq_ptr, params.sq_off.array);
  sqes = (io_uring_sqe *)sqes_ptr;

  cq_head = offset_ptr<unsigned>(cq_ptr, params.cq_off.head);
  cq_tail = offset_ptr<unsigned>(cq_ptr, params.cq_off.tail);
  cq_mask = offset_ptr<unsigned>(cq_ptr, params.cq_off.ring_mask);
  cqes = offset_ptr<io_uring_cqe>(cq_ptr, params.cq_off.cqes);

  return 0;
}

Ring::~Ring() {
  if (ring_fd >= 0) {
    close(ring_fd);
  }
}

int Ring::submit_read(int fd, int op_code) { return 0; }
int Ring::wait() { return 0; }
} // namespace io_uring
