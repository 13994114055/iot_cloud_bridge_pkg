#ifndef IOT_CLOUD_BRIDGE__SERIAL_UTILS_HPP_
#define IOT_CLOUD_BRIDGE__SERIAL_UTILS_HPP_

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace iot_cloud_bridge {

class L610Serial {
public:
  L610Serial();
  ~L610Serial();

  // ---- 串口操作 ----
  bool Open(const std::string& port, int baudrate = 115200);
  void Close();

  // ---- AT 指令发送 ----
  // 单次发送（无重试）
  std::vector<std::string> SendAT(const std::string& cmd, int timeout_ms = 3000);
  
  // 带重试发送
  std::vector<std::string> SendATWithRetry(const std::string& cmd, 
                                            int timeout_ms = 3000, 
                                            int retries = 3,
                                            int retry_delay_ms = 500);

  // ---- MQTT 发布 ----
  bool PublishMQTT(const std::string& topic, const std::string& payload, int qos = 1);

  // ---- 下行消息回调 ----
  void SetRxCallback(std::function<void(const std::string&)> cb);

private:
  int fd_;
  std::atomic<bool> running_;
  std::thread rx_thread_;
  std::queue<std::string> rx_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::string leftover_;
  std::function<void(const std::string&)> rx_callback_;

  void RxLoop();
  void WriteString(const std::string& data);
};

}  // namespace iot_cloud_bridge

#endif