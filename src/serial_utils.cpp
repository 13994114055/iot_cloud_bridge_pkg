#include "iot_cloud_bridge/serial_utils.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

namespace iot_cloud_bridge {

// ============================================================
//  构造函数 / 析构函数
// ============================================================
L610Serial::L610Serial() : fd_(-1), running_(false) {}

L610Serial::~L610Serial() { Close(); }

// ============================================================
//  打开串口
// ============================================================
bool L610Serial::Open(const std::string& port, int baudrate) {
  if (fd_ > 0) Close();

  fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd_ < 0) {
    std::cerr << "[L610Serial] Failed to open " << port << ": " << strerror(errno) << std::endl;
    return false;
  }

  struct termios options;
  tcgetattr(fd_, &options);
  
  // 设置波特率
  speed_t speed;
  switch (baudrate) {
    case 9600:   speed = B9600; break;
    case 19200:  speed = B19200; break;
    case 38400:  speed = B38400; break;
    case 57600:  speed = B57600; break;
    case 115200: speed = B115200; break;
    default:     speed = B115200; break;
  }
  cfsetispeed(&options, speed);
  cfsetospeed(&options, speed);
  
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_cflag &= ~PARENB;      // 无校验
  options.c_cflag &= ~CSTOPB;      // 1位停止位
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;          // 8位数据位
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // 原始模式
  options.c_iflag &= ~(IXON | IXOFF | IXANY);          // 禁用软件流控
  options.c_iflag &= ~(INLCR | ICRNL | IGNCR);         // 禁用换行符转换
  options.c_oflag &= ~OPOST;       // 原始输出
  
  // 设置读取超时
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 10;        // 1秒超时
  
  tcsetattr(fd_, TCSANOW, &options);
  tcflush(fd_, TCIOFLUSH);

  running_ = true;
  rx_thread_ = std::thread(&L610Serial::RxLoop, this);

  std::cout << "[L610Serial] Opened " << port << " at " << baudrate << " baud" << std::endl;
  return true;
}

// ============================================================
//  关闭串口
// ============================================================
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

// ============================================================
//  发送数据到串口
// ============================================================
void L610Serial::WriteString(const std::string& data) {
  if (fd_ < 0) return;
  ::write(fd_, data.c_str(), data.size());
}

// ============================================================
//  发送 AT 指令（单次，无重试）
// ============================================================
std::vector<std::string> L610Serial::SendAT(const std::string& cmd, int timeout_ms) {
  std::vector<std::string> responses;
  if (fd_ < 0) {
    std::cerr << "[L610Serial] SendAT: serial port not open" << std::endl;
    return responses;
  }

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
        line.find("+MQTTOPEN: OK") != std::string::npos ||
        line.find("+MQTTPUB:") != std::string::npos ||
        line.find("+MQTTCONN:") != std::string::npos ||
        line.find("+MQTTDISCONNECT") != std::string::npos) {
      break;
    }
  }
  return responses;
}

// ============================================================
//  发送 AT 指令（带自动重试）
// ============================================================
std::vector<std::string> L610Serial::SendATWithRetry(const std::string& cmd,
                                                       int timeout_ms,
                                                       int retries,
                                                       int retry_delay_ms) {
  std::vector<std::string> responses;
  
  for (int attempt = 1; attempt <= retries; ++attempt) {
    responses = SendAT(cmd, timeout_ms);
    
    // 检查是否成功
    bool success = false;
    for (const auto& line : responses) {
      if (line.find("OK") != std::string::npos ||
          line.find("+MQTTOPEN: OK") != std::string::npos ||
          line.find("+MQTTPUB:") != std::string::npos ||
          line.find("+MQTTCONN:") != std::string::npos) {
        success = true;
        break;
      }
    }
    
    if (success) {
      return responses;
    }
    
    // 如果还有重试次数，等待后重试
    if (attempt < retries) {
      std::cout << "[L610Serial] Retry " << attempt << "/" << retries 
                << " for: " << cmd << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
    }
  }
  
  std::cerr << "[L610Serial] All " << retries << " retries failed for: " << cmd << std::endl;
  return responses;
}

// ============================================================
//  MQTT 发布（标准 AT+MQTTPUB 指令）
// ============================================================
bool L610Serial::PublishMQTT(const std::string& topic, const std::string& payload, int qos) {
  // 转义 payload 中的双引号和反斜杠
  std::string escaped = payload;
  size_t pos = 0;
  while ((pos = escaped.find('\\', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\\\");
    pos += 2;
  }
  pos = 0;
  while ((pos = escaped.find('"', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\\"");
    pos += 2;
  }

  // 使用标准 MQTT AT 指令
  std::string cmd = "AT+MQTTPUB=1,\"" + topic + "\"," + std::to_string(qos) + ",0,\"" + escaped + "\"";
  auto responses = SendATWithRetry(cmd, 5000, 3, 500);
  
  for (const auto& line : responses) {
    if (line.find("OK") != std::string::npos ||
        line.find("+MQTTPUB: 1,1") != std::string::npos ||
        line.find("+MQTTPUB:") != std::string::npos) {
      return true;
    }
  }
  return false;
}

// ============================================================
//  设置下行消息回调
// ============================================================
void L610Serial::SetRxCallback(std::function<void(const std::string&)> cb) {
  rx_callback_ = cb;
}

// ============================================================
//  串口接收线程（持续读取）
// ============================================================
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
      std::string data = leftover_ + std::string(buf, n);
      leftover_.clear();
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
          if (rx_callback_) {
            rx_callback_(line);
          }
        }
        start = end + 1;
      }
      // 保存未完成行，等待下次拼起来
      if (start < data.size()) {
        leftover_ = data.substr(start);
      }
    }
  }
}

}  // namespace iot_cloud_bridge