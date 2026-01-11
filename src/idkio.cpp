#include "idkio.hpp"
#include <iostream>
#include <linux/io_uring.h>
#include <unistd.h>

namespace idkio {

int IdkIo::build() {
  auto ret = ring.init();
  if (ret != 0) {
    return -1;
  }

  event_fd = eventfd(0, EFD_NONBLOCK);
  if (event_fd < 0) {
    return -errno;
  }

  auto sqe = ring.get_sqe_slot();
  sqe->opcode = IORING_OP_READ;
  sqe->fd = event_fd;
  sqe->addr = (unsigned long)&wake_buf;
  sqe->len = sizeof(uint64_t);
  sqe->user_data = 0;

  ring.submit();
  running = true;
  std::cout << "idkio: started" << std::endl;
  run();
  return 0;
}

IdkIo::~IdkIo() {
  if (event_fd >= 0) {
    close(event_fd);
  }
}

void IdkIo::async_read(int fd, char *buf, size_t len, Callback cb) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    requests.push({fd, buf, len, IORING_OP_READ, cb});
  }
  wake();
}

void IdkIo::async_write(int fd, const char *buf, size_t len, Callback cb) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    requests.push({fd, const_cast<char *>(buf), len, IORING_OP_WRITE, cb});
  }
  wake();
}

void IdkIo::wake() {
  uint64_t val = 1;
  write(event_fd, &val, sizeof(val));
}

void IdkIo::process_requests() {
  std::lock_guard<std::mutex> lock(mutex);
  while (!requests.empty()) {
    auto req = requests.front();
    requests.pop();

    auto slot = ring.get_sqe_slot();
    if (!slot) {
      requests.push(req);
      break;
    }

    slot->opcode = req.op;
    slot->fd = req.fd;
    slot->addr = (unsigned long)req.buf;
    slot->len = req.len;
    slot->off = -1;
    slot->user_data = next_id;

    pending[next_id++] = req.cb;
  }
}

void IdkIo::dispatch_completions() {
  while (auto cqe = ring.peek_cqe()) {

    auto id = cqe->user_data;
    auto res = cqe->res;
    ring.cqe_seen();

    if (id == 0) {
      auto sqe = ring.get_sqe_slot();
      if (sqe) {
        sqe->opcode = IORING_OP_READ;
        sqe->fd = event_fd;
        sqe->addr = (unsigned long)&wake_buf;
        sqe->len = sizeof(uint64_t);
        sqe->user_data = 0;
      }
      continue;
    }

    auto it = pending.find(id);
    if (it == pending.end()) {
      continue;
    }

    auto cb = std::move(it->second);
    pending.erase(it);
    cb(res);
  }
}

void IdkIo::run() {
  while (running) {
    process_requests();
    ring.submit_and_wait(1);
    dispatch_completions();
  }
}

void IdkIo::stop() {
  running = false;
  wake();
}

} // namespace idkio
