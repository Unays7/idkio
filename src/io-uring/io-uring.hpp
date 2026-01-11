#pragma once

#include <atomic>
#include <cstddef>
#include <linux/io_uring.h>
#include <sys/types.h>

namespace io_uring {

constexpr auto RING_SIZE = 1000;

class Ring {
public:
  Ring() = default;
  int init();
  ~Ring();
  io_uring_sqe *get_sqe_slot();
  int submit();
  int submit_and_wait(unsigned n);
  io_uring_cqe *peek_cqe();
  void cqe_seen();

  Ring(const Ring &) = delete;
  Ring &operator=(const Ring &) = delete;

private:
  int ring_fd;
  unsigned *sq_head;
  unsigned *sq_tail;
  unsigned *sq_mask;
  unsigned *sq_array;
  io_uring_sqe *sqes;

  unsigned *cq_head;
  unsigned *cq_tail;
  unsigned *cq_mask;
  io_uring_cqe *cqes;

  unsigned sq_submitted_tail = 0;
};

template <typename T> T *offset_ptr(void *base, size_t offset) {
  return (T *)((char *)base + offset);
}

template <typename T> void store_release(T *ptr, T val) {
  reinterpret_cast<std::atomic<T> *>(ptr)->store(val,
                                                 std::memory_order_release);
}

template <typename T> T load_acquire(T *ptr) {
  return reinterpret_cast<std::atomic<T> *>(ptr)->load(
      std::memory_order_acquire);
}

} // namespace io_uring
