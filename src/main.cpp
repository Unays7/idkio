#include "idkio.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>

int main() {
  idkio::IdkIo io;

  int write_fd =
      open("/tmp/idkio_test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (write_fd < 0) {
    std::cout << "Failed to open file for writing" << std::endl;
    return 1;
  }

  int read_fd = open("/tmp/idkio_test.txt", O_RDONLY);
  if (read_fd < 0) {
    std::cout << "Failed to open file for reading" << std::endl;
    return 1;
  }

  const char *write_buf = "yoyo from idkio";
  char read_buf[128] = {0};

  std::thread loop_thread([&]() { io.build(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  io.async_write(write_fd, write_buf, strlen(write_buf), [&](int res) {
    if (res < 0) {
      std::cout << "Write failed: " << strerror(-res) << std::endl;
      io.stop();
      return;
    }
    std::cout << "Wrote " << res << " bytes" << std::endl;

    io.async_read(read_fd, read_buf, sizeof(read_buf), [&](int res) {
      if (res < 0) {
        std::cout << "Read failed: " << strerror(-res) << std::endl;
      } else {
        std::cout << "Read " << res << " bytes: " << read_buf << std::endl;
      }
      io.stop();
    });
  });

  loop_thread.join();

  close(write_fd);
  close(read_fd);

  return 0;
}
