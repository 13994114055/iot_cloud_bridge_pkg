#ifndef IOT_CLOUD_BRIDGE__CLOUD_UPLOAD_NODE_HPP_
#define IOT_CLOUD_BRIDGE__CLOUD_UPLOAD_NODE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <emotion_msgs/msg/emotion_result.hpp>
#include <emotion_msgs/msg/face_detections.hpp>   // 【新增】

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

#include "iot_cloud_bridge/serial_utils.hpp"

namespace iot_cloud_bridge {

class CloudUploadNode : public rclcpp::Node {
public:
  CloudUploadNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~CloudUploadNode();

private:
  // 参数
  std::string serial_port_;
  int baudrate_;
  std::string device_id_;
  std::string broker_;
  std::string port_;
  std::string password_;
  std::string emotion_topic_;
  std::string jpeg_topic_;
  double face_interval_;
  double image_interval_;
  // 【新增】动态密码生成相关辅助函数
  std::string GetCurrentTimestamp();          // 获取 UTC 时间 YYYYMMDDHH
  std::string HmacSha256(const std::string& key, const std::string& data);

  // 串口对象
  L610Serial l610_;

  // 订阅
  rclcpp::Subscription<emotion_msgs::msg::EmotionResult>::SharedPtr emotion_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr jpeg_sub_;
  rclcpp::Subscription<emotion_msgs::msg::FaceDetections>::SharedPtr detections_sub_;  // 【新增】

  // 缓存最新数据
  emotion_msgs::msg::EmotionResult::SharedPtr latest_face_;
  sensor_msgs::msg::CompressedImage::SharedPtr latest_jpeg_;
  emotion_msgs::msg::FaceDetections::SharedPtr latest_detections_;  // 【新增】
  std::mutex data_mutex_;
  std::mutex detections_mutex_;  // 【新增】

  // 时间记录
  std::chrono::steady_clock::time_point last_face_time_;
  std::chrono::steady_clock::time_point last_image_time_;

  // 下行命令处理线程
  std::thread cmd_thread_;
  std::atomic<bool> running_;

  // 回调函数
  void OnEmotion(const emotion_msgs::msg::EmotionResult::SharedPtr msg);
  void OnJpeg(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

  // 定时检查发送
  void CheckAndSend();

  // 【修改】三个新发送函数，替换原来的 SendFaceData 和 SendJpegData
  void SendReportData();     // 情绪 + 置信度 + 关键点 + 框坐标 → /user/report
  void SendResponseData();   // 疏导建议 + 交互文本 → /user/response
  void SendImageData();      // JPEG 图片 → /user/image

  // 【修改】PublishToCloud 增加 topic 参数
  void PublishToCloud(const nlohmann::json& payload, const std::string& topic);

  // 命令处理
  void HandleIncomingLine(const std::string& line);
  void ProcessCommand(const nlohmann::json& cmd_json);

  // 连接华为云
  bool ConnectHuaweiCloud();

  // 辅助
  std::string EmotionLabelToString(int label);
  std::string Base64Encode(const unsigned char* data, size_t len);  // 【新增】Base64编码

  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace iot_cloud_bridge

#endif