#include "io-uring/io-uring.hpp"
#include <functional>
#include <mutex>
#include <queue>
#include <sys/eventfd.h>
#include <unordered_map>

namespace idkio {

using Callback = std::function<void(int result)>;

struct Request {
  int fd;
  char *buf;
  size_t len;
  int op; // IORING_OP_READ or IORING_OP_WRITE
  Callback cb;
};

class IdkIo {
public:
  int build();
  ~IdkIo();

  void async_read(int fd, char *buf, size_t len, Callback cb);
  void async_write(int fd, const char *buf, size_t len, Callback cb);

  void stop();

private:
  void run();
  void dispatch_completions();
  void process_requests();
  void wake();

  io_uring::Ring ring;
  std::unordered_map<uint64_t, Callback> pending;
  uint64_t next_id = 1;
  std::queue<Request> requests;
  std::mutex mutex;

  int event_fd = -1;
  uint64_t wake_buf = 0;
  bool running = false;
};

} // namespace idkio
