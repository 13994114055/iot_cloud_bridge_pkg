#ifndef IOT_CLOUD_BRIDGE__CLOUD_UPLOAD_NODE_HPP_
#define IOT_CLOUD_BRIDGE__CLOUD_UPLOAD_NODE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <emotion_msgs/msg/emotion_result.hpp>
#include <emotion_msgs/msg/face_detections.hpp>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#include "iot_cloud_bridge/serial_utils.hpp"

namespace iot_cloud_bridge {

class CloudUploadNode : public rclcpp::Node {
public:
  CloudUploadNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~CloudUploadNode();

private:
  // ---- 参数 ----
  std::string serial_port_;
  int baudrate_;
  std::string device_id_;
  std::string broker_;
  std::string port_;
  std::string password_;
  std::string emotion_topic_;
  std::atomic<bool> reconnecting_{false};
  double face_interval_;

  // ---- 串口对象 ----
  L610Serial l610_;

  // ---- 订阅 ----
  rclcpp::Subscription<emotion_msgs::msg::EmotionResult>::SharedPtr emotion_sub_;
  rclcpp::Subscription<emotion_msgs::msg::FaceDetections>::SharedPtr detections_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr user_text_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr robot_text_sub_;

  // ---- 缓存 ----
  emotion_msgs::msg::EmotionResult::SharedPtr latest_face_;
  emotion_msgs::msg::FaceDetections::SharedPtr latest_detections_;
  std_msgs::msg::String::SharedPtr latest_user_text_;
  std_msgs::msg::String::SharedPtr latest_robot_text_;

  std::mutex data_mutex_;
  std::mutex detections_mutex_;
  std::mutex text_mutex_;

  // ---- 时间记录 ----
  std::chrono::steady_clock::time_point last_face_time_;

  // ---- 线程控制 ----
  std::thread cmd_thread_;
  std::atomic<bool> running_;

  // ---- 回调函数 ----
  void OnEmotion(const emotion_msgs::msg::EmotionResult::SharedPtr msg);
  void OnUserText(const std_msgs::msg::String::SharedPtr msg);
  void OnRobotText(const std_msgs::msg::String::SharedPtr msg);

  // ---- 定时检查发送 ----
  void CheckAndSend();

  // ---- 发送函数 ----
  void SendReportData();
  void SendChatData(const std::string& type, const std::string& text, const std::string& last_other);
  void PublishToCloud(const nlohmann::json& payload, const std::string& topic);

  // ---- 命令处理 ----
  void HandleIncomingLine(const std::string& line);
  void ProcessCommand(const nlohmann::json& cmd_json);

  // ---- 连接华为云 ----
  bool ConnectHuaweiCloud();

  // ---- 辅助 ----
  std::string EmotionLabelToString(int label);

  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace iot_cloud_bridge

#endif