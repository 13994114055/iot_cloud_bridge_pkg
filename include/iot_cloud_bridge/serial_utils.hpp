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

  bool Open(const std::string& port, int baudrate = 115200);
  void Close();

  // 发送 AT 命令，等待响应（最多 timeout_ms 毫秒）
  // 返回响应行列表（包括最终 OK/ERROR）
  std::vector<std::string> SendAT(const std::string& cmd, int timeout_ms = 3000);

  // 发送 MQTT PUBLISH（封装 AT+HMPUB），自动等待 OK
  bool PublishMQTT(const std::string& topic, const std::string& payload, int qos = 1);

  // 注册下行消息回调（当收到 +HMRECV 或非 AT 响应时触发）
  void SetRxCallback(std::function<void(const std::string&)> cb);

private:
  int fd_;
  std::atomic<bool> running_;
  std::thread rx_thread_;
  std::queue<std::string> rx_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::function<void(const std::string&)> rx_callback_;

  void RxLoop();
  void WriteString(const std::string& data);
};

}  // namespace iot_cloud_bridge

#endif