#include "io-uring.hpp"

namespace io_uring {

Ring::Ring() {}
Ring::~Ring() {}

int Ring::submit(int fd, int op_code) { return 0; }
int Ring::reap() { return 0; }
} // namespace io_uring
