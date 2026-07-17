#include "iot_cloud_bridge/cloud_upload_node.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

// ---- Base64 编码（简单实现，保留备用） ----
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const unsigned char* data, size_t len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];

        while (i++ < 3) ret += '=';
    }

    return ret;
}

namespace iot_cloud_bridge {

// ============================================================
//  构造函数
// ============================================================
CloudUploadNode::CloudUploadNode(const rclcpp::NodeOptions& options)
    : Node("cloud_upload_node", options),
      running_(true),
      mqtt_connected_(false),
      reconnecting_(false),
      last_face_time_(std::chrono::steady_clock::now()),
      last_keepalive_check_(std::chrono::steady_clock::now()) {

  // ---- 声明参数 ----
  declare_parameter("serial_port", "/dev/ttyUSB0");
  declare_parameter("baudrate", 115200);
  declare_parameter("device_id", "");
  declare_parameter("broker", "64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com");
  declare_parameter("port", "1883");
  declare_parameter("password", "");
  declare_parameter("emotion_topic", "/emotion/result");
  declare_parameter("face_report_interval", 0.067);

  // ---- 加载参数 ----
  serial_port_ = get_parameter("serial_port").as_string();
  baudrate_ = get_parameter("baudrate").as_int();
  device_id_ = get_parameter("device_id").as_string();
  broker_ = get_parameter("broker").as_string();
  port_ = get_parameter("port").as_string();
  password_ = get_parameter("password").as_string();
  emotion_topic_ = get_parameter("emotion_topic").as_string();
  face_interval_ = get_parameter("face_report_interval").as_double();

  // ---- 校验 ----
  if (device_id_.empty() || password_.empty()) {
    RCLCPP_ERROR(get_logger(), "device_id and password must be set!");
    throw std::runtime_error("Missing device_id or password");
  }

  // ---- 打开串口 ----
  if (!l610_.Open(serial_port_, baudrate_)) {
    RCLCPP_ERROR(get_logger(), "Failed to open serial port %s", serial_port_.c_str());
    throw std::runtime_error("Serial port open failed");
  }
  RCLCPP_INFO(get_logger(), "Serial port %s opened", serial_port_.c_str());

  // ---- 握手检查：验证 L610 是否存活 ----
  RCLCPP_INFO(get_logger(), "Handshaking with L610...");
  auto handshake_resp = l610_.SendATWithRetry("AT", 2000, 3, 500);
  bool handshake_ok = false;
  for (const auto& line : handshake_resp) {
    if (line.find("OK") != std::string::npos) {
      handshake_ok = true;
      break;
    }
  }
  if (!handshake_ok) {
    RCLCPP_ERROR(get_logger(), "L610 handshake failed! No response to AT.");
    throw std::runtime_error("L610 handshake failed");
  }
  RCLCPP_INFO(get_logger(), "Handshake successful.");

  // ---- 连接华为云 ----
  if (!ConnectHuaweiCloud()) {
    RCLCPP_ERROR(get_logger(), "Failed to connect to Huawei Cloud");
    throw std::runtime_error("MQTT connect failed");
  }

  // ---- 设置下行回调 ----
  l610_.SetRxCallback([this](const std::string& line) {
    HandleIncomingLine(line);
  });

  // ---- 订阅 ROS2 话题 ----
  auto qos = rclcpp::QoS(1).reliable().volatile_durability();

  // 1. 订阅情绪结果
  emotion_sub_ = create_subscription<emotion_msgs::msg::EmotionResult>(
      emotion_topic_, qos,
      std::bind(&CloudUploadNode::OnEmotion, this, std::placeholders::_1));

  // 2. 订阅人脸检测结果（用于获取框坐标和关键点）
  detections_sub_ = create_subscription<emotion_msgs::msg::FaceDetections>(
      "/retinaface/detections", qos,
      [this](const emotion_msgs::msg::FaceDetections::SharedPtr msg) {
          std::lock_guard<std::mutex> lock(detections_mutex_);
          latest_detections_ = msg;
      });

  // 3. 订阅用户说的话（来自 ASR）
  user_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/asr/filtered_text", qos,
      [this](const std_msgs::msg::String::SharedPtr msg) {
          if (msg->data.empty()) return;
          std::lock_guard<std::mutex> lock(text_mutex_);
          latest_user_text_ = msg;
          RCLCPP_INFO(get_logger(), "[User] %s", msg->data.c_str());
          SendChatData("user", msg->data,
                       latest_robot_text_ ? latest_robot_text_->data : "");
      });

  // 4. 订阅机器人说的话（来自 TTS）
  robot_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/tts_text", qos,
      [this](const std_msgs::msg::String::SharedPtr msg) {
          if (msg->data.empty()) return;
          std::lock_guard<std::mutex> lock(text_mutex_);
          latest_robot_text_ = msg;
          RCLCPP_INFO(get_logger(), "[Robot] %s", msg->data.c_str());
          SendChatData("robot", msg->data,
                       latest_user_text_ ? latest_user_text_->data : "");
      });

  // ---- 启动命令处理线程 ----
  cmd_thread_ = std::thread([this]() {
    while (running_ && rclcpp::ok()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // ---- 定时器检查发送（情绪数据） ----
  timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      std::bind(&CloudUploadNode::CheckAndSend, this));

  // ---- 保活检测定时器（每 10 秒检查一次） ----
  keepalive_timer_ = create_wall_timer(
      std::chrono::milliseconds(10000),
      std::bind(&CloudUploadNode::CheckKeepAlive, this));

  RCLCPP_INFO(get_logger(), "CloudUploadNode initialized");
}

// ============================================================
//  析构函数
// ============================================================
CloudUploadNode::~CloudUploadNode() {
  running_ = false;
  if (cmd_thread_.joinable()) cmd_thread_.join();
  l610_.Close();
}

// ============================================================
//  连接华为云（标准 MQTT AT 指令，带重试）
// ============================================================
bool CloudUploadNode::ConnectHuaweiCloud() {
    RCLCPP_INFO(get_logger(), "Connecting to Huawei Cloud with retry...");

    std::string timestamp = std::to_string(std::time(nullptr));
    // 签名类型：0 = 不校验时间戳（稳定优先）
    std::string client_id = device_id_ + "_0_0_" + timestamp;
    std::string username = device_id_;

    // 每条指令都带重试机制（3次重试，间隔500ms）
    auto resp1 = l610_.SendATWithRetry("AT+MQTTSVR=" + broker_ + "," + port_, 3000, 3, 500);
    if (resp1.empty() || resp1[0].find("OK") == std::string::npos) {
        RCLCPP_ERROR(get_logger(), "Failed to set MQTT server after retries");
        return false;
    }

    auto resp2 = l610_.SendATWithRetry("AT+MQTTCLIENT=" + client_id, 3000, 3, 500);
    if (resp2.empty() || resp2[0].find("OK") == std::string::npos) {
        RCLCPP_ERROR(get_logger(), "Failed to set MQTT client ID after retries");
        return false;
    }

    auto resp3 = l610_.SendATWithRetry("AT+MQTTUSER=" + username, 3000, 3, 500);
    if (resp3.empty() || resp3[0].find("OK") == std::string::npos) {
        RCLCPP_ERROR(get_logger(), "Failed to set MQTT username after retries");
        return false;
    }

    auto resp4 = l610_.SendATWithRetry("AT+MQTTPSW=" + password_, 3000, 3, 500);
    if (resp4.empty() || resp4[0].find("OK") == std::string::npos) {
        RCLCPP_ERROR(get_logger(), "Failed to set MQTT password after retries");
        return false;
    }

    // 连接指令也带重试，超时时间给长一点（10秒）
    auto responses = l610_.SendATWithRetry("AT+MQTTOPEN=1,1,0", 10000, 3, 1000);

    for (const auto& line : responses) {
        if (line.find("+MQTTOPEN: OK") != std::string::npos) {
            RCLCPP_INFO(get_logger(), "MQTT connection successful (timestamp=%s)", timestamp.c_str());
            mqtt_connected_.store(true);
            return true;
        }
    }
    
    RCLCPP_ERROR(get_logger(), "MQTT connection failed after all retries");
    mqtt_connected_.store(false);
    return false;
}

// ============================================================
//  保活检测：检查 L610 和 MQTT 连接状态
// ============================================================
void CloudUploadNode::CheckKeepAlive() {
    // 如果正在重连，跳过本次检测
    if (reconnecting_.load()) {
        return;
    }

    // 发送 AT 指令测试 L610 是否还在响应
    auto resp = l610_.SendAT("AT", 2000);
    bool l610_alive = false;
    for (const auto& line : resp) {
        if (line.find("OK") != std::string::npos) {
            l610_alive = true;
            break;
        }
    }
    
    if (!l610_alive) {
        RCLCPP_WARN(get_logger(), "L610 not responding, will attempt reconnect");
        ReconnectMQTT();
        return;
    }
    
    // 如果 MQTT 状态标志为 false，触发重连
    if (!mqtt_connected_.load()) {
        RCLCPP_WARN(get_logger(), "MQTT not connected, attempting reconnect...");
        ReconnectMQTT();
    }
}

// ============================================================
//  重连 MQTT（防抖）
// ============================================================
void CloudUploadNode::ReconnectMQTT() {
    // 防止并发重连
    bool expected = false;
    if (!reconnecting_.compare_exchange_strong(expected, true)) {
        RCLCPP_DEBUG(get_logger(), "Already reconnecting, skip");
        return;
    }
    // 确保函数退出时重置标志
    struct ReconnectGuard {
        std::atomic<bool>& flag;
        ReconnectGuard(std::atomic<bool>& f) : flag(f) {}
        ~ReconnectGuard() { flag.store(false); }
    } guard(reconnecting_);

    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    if (mqtt_connected_.load()) {
        RCLCPP_INFO(get_logger(), "MQTT seems connected, skip reconnect");
        return;
    }
    RCLCPP_WARN(get_logger(), "Attempting MQTT reconnect...");
    
    l610_.SendAT("AT+MQTTDISC", 2000);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (ConnectHuaweiCloud()) {
        mqtt_connected_.store(true);
        RCLCPP_INFO(get_logger(), "MQTT reconnected successfully");
    } else {
        RCLCPP_ERROR(get_logger(), "MQTT reconnect failed, will retry later");
        mqtt_connected_.store(false);
    }
}

// ============================================================
//  情绪数据回调
// ============================================================
void CloudUploadNode::OnEmotion(const emotion_msgs::msg::EmotionResult::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_face_ = msg;
}

// ============================================================
//  定时检查并发送情绪数据
// ============================================================
void CloudUploadNode::CheckAndSend() {
  auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (latest_face_) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_face_time_).count();
      if (elapsed >= face_interval_ * 1000) {
        SendReportData();
        last_face_time_ = now;
      }
    }
  }
}

// ============================================================
//  发送报告数据（情绪 + 置信度 + 关键点 + 框坐标）
// ============================================================
void CloudUploadNode::SendReportData() {
  if (!latest_face_) return;

  nlohmann::json payload;
  payload["type"] = "report";
  payload["timestamp"] = std::time(nullptr);
  payload["emotion"] = EmotionLabelToString(latest_face_->emotion_label);
  payload["confidence"] = latest_face_->confidence;

  // 从检测结果中匹配 bbox 和 landmarks
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(detections_mutex_);
    if (latest_detections_) {
      for (const auto& det : latest_detections_->face_boxes) {
        if (det.face_uid == latest_face_->face_uid) {
          payload["bbox"]["x"] = det.center_x - det.size_x / 2;
          payload["bbox"]["y"] = det.center_y - det.size_y / 2;
          payload["bbox"]["w"] = det.size_x;
          payload["bbox"]["h"] = det.size_y;
          payload["landmarks"] = nlohmann::json::array();
          for (size_t i = 0; i < det.landmarks.size(); ++i) {
            payload["landmarks"].push_back(det.landmarks[i]);
          }
          found = true;
          break;
        }
      }
    }
  }

  // 没匹配到则填默认值
  if (!found) {
    payload["bbox"] = {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}};
    payload["landmarks"] = nlohmann::json::array();
    for (int i = 0; i < 10; ++i) payload["landmarks"].push_back(0);
  }

  std::string topic = "/v1/" + device_id_ + "/user/report";
  PublishToCloud(payload, topic);
}

// ============================================================
//  发送聊天数据（用户或机器人说的话）
// ============================================================
void CloudUploadNode::SendChatData(const std::string& type,
                                   const std::string& text,
                                   const std::string& last_other) {
  if (text.empty()) return;

  nlohmann::json payload;
  payload["type"] = type;
  payload["text"] = text;
  payload["last_other"] = last_other;
  payload["timestamp"] = std::time(nullptr);

  std::string topic = "/v1/" + device_id_ + "/user/chat";
  PublishToCloud(payload, topic);
}

// ============================================================
//  发布到云平台
// ============================================================
void CloudUploadNode::PublishToCloud(const nlohmann::json& payload, const std::string& topic) {
  std::string json_str = payload.dump();
  if (l610_.PublishMQTT(topic, json_str, 1)) {
    RCLCPP_DEBUG(get_logger(), "Published to %s", topic.c_str());
  } else {
    RCLCPP_WARN(get_logger(), "Failed to publish to %s (payload len=%zu)", 
                topic.c_str(), json_str.size());
  }
}

// ============================================================
//  下行命令处理
// ============================================================
void CloudUploadNode::HandleIncomingLine(const std::string& line) {
    // 检测 MQTT 断开事件 → 立即重连
    if (line.find("+MQTTDISCONNECT") != std::string::npos) {
        RCLCPP_WARN(get_logger(), "MQTT disconnected by server or network");
        mqtt_connected_.store(false);
        // 立即触发重连（异步，避免阻塞串口读取线程）
        std::thread([this]() { ReconnectMQTT(); }).detach();
        return;
    }
    
    // 检测 MQTT 连接成功事件（用于更新状态）
    if (line.find("+MQTTOPEN: OK") != std::string::npos) {
        mqtt_connected_.store(true);
        RCLCPP_INFO(get_logger(), "MQTT connection confirmed");
        return;
    }
    
    // 原有的 JSON 处理逻辑
    if (line.find('{') != std::string::npos) {
        try {
            size_t start = line.find('{');
            std::string json_str = line.substr(start);
            auto cmd_json = nlohmann::json::parse(json_str);
            ProcessCommand(cmd_json);
        } catch (const std::exception& e) {
            // 忽略非 JSON 行
        }
    }
}

void CloudUploadNode::ProcessCommand(const nlohmann::json& cmd_json) {
  RCLCPP_INFO(get_logger(), "Received command: %s", cmd_json.dump().c_str());
  std::string cmd_name = cmd_json.value("command_name", "");
  if (cmd_name == "receive_document") {
    // 简化处理：仅打印，实际可扩展为文件接收
    auto paras = cmd_json["paras"];
    std::string file_name = paras.value("file_name", "unknown");
    int total = paras.value("total_packages", 1);
    int index = paras.value("package_index", 1);
    RCLCPP_INFO(get_logger(), "Receiving %s (%d/%d)", file_name.c_str(), index, total);
    if (index == total) {
      RCLCPP_INFO(get_logger(), "Document %s received completely", file_name.c_str());
    }
  }
}

// ============================================================
//  辅助函数
// ============================================================
std::string CloudUploadNode::EmotionLabelToString(int label) {
  static const std::vector<std::string> names = {
      "angry", "disgust", "fear", "happy", "neutral", "sad", "surprise"
  };
  if (label >= 0 && label < static_cast<int>(names.size()))
    return names[label];
  return "unknown";
}

}  // namespace iot_cloud_bridge

// ============================================================
//  主函数
// ============================================================
#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<iot_cloud_bridge::CloudUploadNode>();
    rclcpp::spin(node);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(rclcpp::get_logger("cloud_upload_node"), "Exception: %s", e.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}