#include "iot_cloud_bridge/cloud_upload_node.hpp"
#include "emotion_msgs/msg/face_detections.hpp"   

#include <nlohmann/json.hpp>
#include <rclcpp/logging.hpp>
#include <openssl/hmac.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <base64.h>   

namespace iot_cloud_bridge {

// ============================================================
//  简易 Base64 编码（避免引入额外库）
// ============================================================
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* data, size_t len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

// ============================================================
//  节点实现
// ============================================================
CloudUploadNode::CloudUploadNode(const rclcpp::NodeOptions& options)
    : Node("cloud_upload_node", options),
      running_(true),
      last_face_time_(std::chrono::steady_clock::now()),
      last_image_time_(std::chrono::steady_clock::now()) {

  // ---- 声明参数 ----
  declare_parameter("serial_port", "/dev/ttyUSB0");
  declare_parameter("baudrate", 115200);
  declare_parameter("device_id", "");
  declare_parameter("broker", "iotda.cn-north-4.myhuaweicloud.com");
  declare_parameter("port", "8883");
  declare_parameter("password", "");
  declare_parameter("emotion_topic", "/emotion/result");
  declare_parameter("jpeg_topic", "/camera/jpeg");
  declare_parameter("face_report_interval", 0.5);
  declare_parameter("image_report_interval", 2.0);

  // ---- 加载参数 ----
  serial_port_ = get_parameter("serial_port").as_string();
  baudrate_ = get_parameter("baudrate").as_int();
  device_id_ = get_parameter("device_id").as_string();
  broker_ = get_parameter("broker").as_string();
  port_ = get_parameter("port").as_string();
  password_ = get_parameter("password").as_string();
  emotion_topic_ = get_parameter("emotion_topic").as_string();
  jpeg_topic_ = get_parameter("jpeg_topic").as_string();
  face_interval_ = get_parameter("face_report_interval").as_double();
  image_interval_ = get_parameter("image_report_interval").as_double();

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

//-----
  std::string CloudUploadNode::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&tt);  // UTC 时间，不要用 localtime
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H", tm);
    return std::string(buf);
}

//----
std::string CloudUploadNode::HmacSha256(const std::string& key, const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.c_str(), key.size(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
         result, &len);
    std::stringstream ss;
    for (unsigned int i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(result[i]);
    }
    return ss.str();
}

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
  emotion_sub_ = create_subscription<emotion_msgs::msg::EmotionResult>(
      emotion_topic_, qos, std::bind(&CloudUploadNode::OnEmotion, this, std::placeholders::_1));
  jpeg_sub_ = create_subscription<sensor_msgs::msg::CompressedImage>(
      jpeg_topic_, qos, std::bind(&CloudUploadNode::OnJpeg, this, std::placeholders::_1));
// 【新增】订阅人脸检测结果（用于获取框坐标和关键点）
detections_sub_ = create_subscription<emotion_msgs::msg::FaceDetections>(
    "/retinaface/detections", qos,
    [this](const emotion_msgs::msg::FaceDetections::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(detections_mutex_);
        latest_detections_ = msg;
    });

  // ---- 启动命令处理线程 ----
  cmd_thread_ = std::thread([this]() {
    while (running_ && rclcpp::ok()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // ---- 定时器检查发送 ----
  timer_ = create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&CloudUploadNode::CheckAndSend, this));

  RCLCPP_INFO(get_logger(), "CloudUploadNode initialized");
}

CloudUploadNode::~CloudUploadNode() {
  running_ = false;
  if (cmd_thread_.joinable()) cmd_thread_.join();
  l610_.Close();
}

// ============================================================
//  连接华为云
// ============================================================
bool CloudUploadNode::ConnectHuaweiCloud() {
    RCLCPP_INFO(get_logger(), "Connecting to Huawei Cloud with dynamic password...");

    // 1. 获取当前 UTC 时间戳（格式：YYYYMMDDHH）
    std::string timestamp = GetCurrentTimestamp();

    // 2. 用设备密钥（明文）和时间戳计算 HMAC-SHA256 密码
    std::string device_secret = password_;   // password_ 现在是明文密钥
    std::string dynamic_password = HmacSha256(device_secret, timestamp);

    // 3. 构建 ClientId 和 Username（按华为云规范）
    //    签名类型：0 表示不校验时间戳
    std::string client_id = device_id_ + "_0_0_" + timestamp;
    std::string username = device_id_;       // Username 就是 device_id

    // 4. 发送 AT 指令
    l610_.SendAT("AT+MQTTSVR=" + broker_ + "," + port_, 3000);
    l610_.SendAT("AT+MQTTCLIENT=" + client_id, 3000);
    l610_.SendAT("AT+MQTTUSER=" + username, 3000);
    l610_.SendAT("AT+MQTTPSW=" + dynamic_password, 3000);
    auto responses = l610_.SendAT("AT+MQTTOPEN=1,1,0", 10000);

    for (const auto& line : responses) {
        if (line.find("+MQTTOPEN: OK") != std::string::npos) {
            RCLCPP_INFO(get_logger(), "MQTT connection successful (timestamp=%s)", timestamp.c_str());
            return true;
        }
    }
    RCLCPP_ERROR(get_logger(), "MQTT connection failed");
    return false;
}

// ============================================================
//  ROS2 回调
// ============================================================
void CloudUploadNode::OnEmotion(const emotion_msgs::msg::EmotionResult::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_face_ = msg;
}

void CloudUploadNode::OnJpeg(const sensor_msgs::msg::CompressedImage::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_jpeg_ = msg;
}

// ============================================================
//  定时检查并发送
// ============================================================
void CloudUploadNode::CheckAndSend() {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (latest_face_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_face_time_).count();
            if (elapsed >= face_interval_ * 1000) {
                SendReportData();      // 发报告（情绪+框+关键点）
                SendResponseData();    // 发对话响应（建议+文本）
                last_face_time_ = now;
            }
        }
        if (latest_jpeg_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_image_time_).count();
            if (elapsed >= image_interval_ * 1000) {
                SendImageData();       // 发图片
                last_image_time_ = now;
            }
        }
    }
}

// ============================================================
//  发送数据
// ============================================================

// ============================================================
// 新函数 1：发送报告数据（情绪 + 置信度 + 关键点 + 框坐标）
// ============================================================
void CloudUploadNode::SendReportData() {
    nlohmann::json payload;
    payload["type"] = "report";

    // 从缓存的情绪结果中取数据
    if (latest_face_) {
        payload["emotion"] = EmotionLabelToString(latest_face_->emotion_label);
        payload["confidence"] = latest_face_->confidence;
    }

    // 从缓存的人脸检测结果中取框坐标和关键点（匹配 face_uid）
    {
        std::lock_guard<std::mutex> lock(detections_mutex_);
        if (latest_detections_ && latest_face_) {
            for (const auto& det : latest_detections_->face_boxes) {
                if (det.face_uid == latest_face_->face_uid) {
                    // 框坐标
                    payload["bbox"]["x"] = det.center_x - det.size_x / 2;
                    payload["bbox"]["y"] = det.center_y - det.size_y / 2;
                    payload["bbox"]["w"] = det.size_x;
                    payload["bbox"]["h"] = det.size_y;
                    // 关键点 (10个int)
                    payload["landmarks"] = nlohmann::json::array();
                    for (size_t i = 0; i < det.landmarks.size(); ++i) {
                        payload["landmarks"].push_back(det.landmarks[i]);
                    }
                    break;
                }
            }
        }
    }

    std::string topic = "/v1/" + device_id_ + "/user/report";
    PublishToCloud(payload, topic);
}

// ============================================================
// 新函数 2：发送对话响应数据（疏导建议 + 交互文本）
// ============================================================
void CloudUploadNode::SendResponseData() {
    nlohmann::json payload;
    payload["type"] = "response";
    // 注意：这两个数据源需要你另外提供，目前先用占位值
    // 你需要从 deepseek_llm 节点或其他地方获取这些数据
    payload["suggestion"] = get_latest_suggestion();   // 你需要实现这个函数
    payload["dialogue"] = get_latest_dialogue();       // 你需要实现这个函数

    std::string topic = "/v1/" + device_id_ + "/user/response";
    PublishToCloud(payload, topic);
}

// ============================================================
// 新函数 3：发送图像数据
// ============================================================
void CloudUploadNode::SendImageData() {
    if (!latest_jpeg_) return;

    // Base64 编码 JPEG
    std::string b64 = Base64Encode(latest_jpeg_->data.data(), latest_jpeg_->data.size());

    nlohmann::json payload;
    payload["type"] = "image";
    payload["image"] = b64;

    std::string topic = "/v1/" + device_id_ + "/user/image";
    PublishToCloud(payload, topic);
}


void CloudUploadNode::PublishToCloud(const nlohmann::json& payload, const std::string& topic) {
    std::string json_str = payload.dump();
    if (l610_.PublishMQTT(topic, json_str, 1)) {
        RCLCPP_DEBUG(get_logger(), "Published to %s", topic.c_str());
    } else {
        RCLCPP_WARN(get_logger(), "Failed to publish to %s", topic.c_str());
    }
}

// ============================================================
//  下行命令处理
// ============================================================
void CloudUploadNode::HandleIncomingLine(const std::string& line) {
  // 检查是否包含 JSON 或命令指示
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
    auto paras = cmd_json["paras"];
    std::string file_name = paras.value("file_name", "unknown");
    int total = paras.value("total_packages", 1);
    int index = paras.value("package_index", 1);
    std::string chunk_b64 = paras.value("chunk_data", "");

    // 解码 Base64（简单实现，此处略）
    // 实际应用中需要将 chunk_b64 解码并写入文件
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
  static const std::vector<std::string> names = {"angry","disgust","fear","happy","neutral","sad","surprise"};
  if (label >= 0 && label < static_cast<int>(names.size()))
    return names[label];
  return "unknown";
}

}  // namespace iot_cloud_bridge

#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<iot_cloud_bridge::CloudUploadNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}