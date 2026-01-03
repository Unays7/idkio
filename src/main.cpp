#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <linux/fs.h>
#include <linux/io_uring.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

template <typename T> T *offset_ptr(void *base, size_t offset) {
  return (T *)((char *)base + offset);
}

struct IoUring {
  int ring_fd;
  char buff[BLOCK_SIZE];

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

auto io_uring_enter(auto ring_fd, auto to_submit, auto min_complete,
                    auto flags) {
  auto ret = syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                     flags, NULL, 0);
  return (ret < 0) ? -errno : ret;
}

auto app_setup_uring(IoUring &ring) {
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

int read_cq(IoUring ring) {
  // dequeue completion queue entry
  // add to tail of ring buffer

  return 0;
}
int write_sq(auto file_descriptor, int op_code, IoUring ring) {
  // create some sort of submission queue entry
  // add to tail of ring buffer

  auto idx = *ring.sq_tail & *ring.sq_mask;
  auto sqe = &ring.sqes[idx];

  if (op_code == IORING_OP_READ) {
    memset(ring.buff, 0, sizeof(ring.buff));
    sqe->len = BLOCK_SIZE;
  } else {
    sqe->len = strlen(ring.buff);
  }

  sqe->off = off_t{};

  sqe->opcode = op_code;
  sqe->fd = file_descriptor;
  sqe->addr = (unsigned long)&ring.buff;

  *ring.sq_tail += 1;
  ring.sq_array[idx] = idx;

  // idk how to use cpp atomics but we need memory ordering or barriers or wtv
  // atomic_store_explicit();
  // syscall to submit entry
  auto ret =
      io_uring_enter(ring.ring_fd, 1, 1,
                     IORING_ENTER_GETEVENTS); // TO-DO how do we use sqpoll ???
  if (ret < 0) {
    std::cout << "Error submiting" << std::endl;
    return -1;
  }
  return 0;
}

int main() {
  IoUring ring{};
  if (app_setup_uring(ring) != 1) {
    std::cout << "IO uring set-up success" << std::endl;
  }
}
