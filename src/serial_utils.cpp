#include "iot_cloud_bridge/serial_utils.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <chrono>

namespace iot_cloud_bridge {

L610Serial::L610Serial() : fd_(-1), running_(false) {}

L610Serial::~L610Serial() { Close(); }

bool L610Serial::Open(const std::string& port, int baudrate) {
  if (fd_ > 0) Close();

  fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd_ < 0) {
    std::cerr << "[L610Serial] Failed to open " << port << ": " << strerror(errno) << std::endl;
    return false;
  }

  // 配置串口
  struct termios options;
  tcgetattr(fd_, &options);
  cfsetispeed(&options, B115200);
  cfsetospeed(&options, B115200);
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_iflag &= ~(IXON | IXOFF | IXANY);
  options.c_oflag &= ~OPOST;
  tcsetattr(fd_, TCSANOW, &options);

  // 清空缓冲区
  tcflush(fd_, TCIOFLUSH);

  running_ = true;
  rx_thread_ = std::thread(&L610Serial::RxLoop, this);

  std::cout << "[L610Serial] Opened " << port << std::endl;
  return true;
}

void L610Serial::Close() {
  running_ = false;
  if (rx_thread_.joinable()) {
    rx_thread_.join();
  }
  if (fd_ > 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void L610Serial::WriteString(const std::string& data) {
  if (fd_ < 0) return;
  ::write(fd_, data.c_str(), data.size());
}

std::vector<std::string> L610Serial::SendAT(const std::string& cmd, int timeout_ms) {
  std::vector<std::string> responses;
  if (fd_ < 0) return responses;

  // 清空队列中已有的残留数据
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!rx_queue_.empty()) rx_queue_.pop();
  }

  // 发送命令
  WriteString(cmd + "\r\n");

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start).count() < timeout_ms) {
    std::string line;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                              [this] { return !rx_queue_.empty(); })) {
        line = rx_queue_.front();
        rx_queue_.pop();
      } else {
        continue;
      }
    }
    responses.push_back(line);
    // 如果收到 OK 或 ERROR，认为命令结束
    if (line.find("OK") != std::string::npos ||
        line.find("ERROR") != std::string::npos ||
        line.find("+HMPUB:") != std::string::npos) {
      break;
    }
  }
  return responses;
}

bool L610Serial::PublishMQTT(const std::string& topic, const std::string& payload, int qos) {
    // 转义 payload 中的双引号
    std::string escaped = payload;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\\"");
        pos += 2;
    }

    // 改成 AT+MQTTPUB，参数顺序：topic,qos,retain,payload
    std::string cmd = "AT+MQTTPUB=1,\"" + topic + "\"," + std::to_string(qos) + ",0,\"" + escaped + "\"";
    auto responses = SendAT(cmd, 5000);
    for (const auto& line : responses) {
        if (line.find("OK") != std::string::npos ||
            line.find("+MQTTPUB: 1,1") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void L610Serial::SetRxCallback(std::function<void(const std::string&)> cb) {
  rx_callback_ = cb;
}

void L610Serial::RxLoop() {
  char buf[1024];
  while (running_) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval tv = {0, 100000}; // 100ms
    int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ret == 0) continue;

    int n = ::read(fd_, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      std::string data(buf, n);
      // 按行分割
      size_t start = 0;
      size_t end;
      while ((end = data.find('\n', start)) != std::string::npos) {
        std::string line = data.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
          {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            rx_queue_.push(line);
          }
          queue_cv_.notify_one();
          // 如果有回调，调用
          if (rx_callback_) {
            rx_callback_(line);
          }
        }
        start = end + 1;
      }
    }
  }
}

}  // namespace iot_cloud_bridge