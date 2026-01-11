#include "io-uring.hpp"
#include <cerrno>
#include <cstring>
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
  auto fd = io_uring_setup(RING_SIZE, &params);
  if (fd < 0) {
    std::cout << "IO uring set-up failed" << std::endl;
    return 1;
  }

  auto sring_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);
  auto cring_sz = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);

  void *sq_ptr, *cq_ptr;

  if (params.features & IORING_FEAT_SINGLE_MMAP) {
    auto ring_sz = std::max(sring_sz, cring_sz);
    sq_ptr = mmap(nullptr, ring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    cq_ptr = sq_ptr;
    if (sq_ptr == MAP_FAILED) {
      std::cout << "sq mmap failed: " << strerror(errno) << std::endl;
      return -1;
    }
  } else {
    sq_ptr = mmap(nullptr, sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    cq_ptr = mmap(nullptr, cring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);

    if (sq_ptr == MAP_FAILED) {
      std::cout << "sq mmap failed: " << strerror(errno) << std::endl;
      return -1;
    }
    if (cq_ptr == MAP_FAILED) {
      std::cout << "cq mmap failed: " << strerror(errno) << std::endl;
      return -1;
    }
  }

  auto sqes_ptr = mmap(nullptr, params.sq_entries * sizeof(io_uring_sqe),
                       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                       IORING_OFF_SQES);

  if (sqes_ptr == MAP_FAILED) {
    std::cout << "sqes mmap failed: " << strerror(errno) << std::endl;
    return -1;
  }

  ring_fd = fd;
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

/// Fetches a submission queue slot and sets the SQE idx to point to that slot
io_uring_sqe *Ring::get_sqe_slot() {
  auto head = load_acquire(sq_head);
  auto tail = *sq_tail;

  if (tail - head >= *sq_mask + 1) {
    return nullptr;
  }

  auto idx = tail & *sq_mask;
  auto sqe = &sqes[idx];
  memset(sqe, 0, sizeof(*sqe));
  sq_array[idx] = idx;

  (*sq_tail)++;

  return sqe;
}

/// Submits the sqe entry using io_uring_enter() syscall
int Ring::submit() {
  store_release(sq_tail, *sq_tail);

  auto to_submit = *sq_tail - sq_submitted_tail;

  if (to_submit == 0) {
    return 0;
  }

  auto ret = io_uring_enter(ring_fd, to_submit, 0, 0);
  if (ret < 0) {
    return -errno;
  }

  sq_submitted_tail = *sq_tail; // mark as submitted
  return ret;
}

int Ring::submit_and_wait(unsigned n) {
  store_release(sq_tail, *sq_tail);

  auto head = load_acquire(sq_head);
  auto tail = *sq_tail;
  auto to_submit = tail - sq_submitted_tail;

  auto ret = io_uring_enter(ring_fd, to_submit, n, IORING_ENTER_GETEVENTS);
  if (ret < 0) {
    return -errno;
  }

  sq_submitted_tail = tail;
  return ret;
}

io_uring_cqe *Ring::peek_cqe() {
  auto head = *cq_head;
  auto tail = load_acquire(cq_tail);

  if (head == tail) {
    return nullptr;
  }

  auto idx = head & *cq_mask;
  return &cqes[idx];
}

void Ring::cqe_seen() { store_release(cq_head, *cq_head + 1); }

} // namespace io_uring
