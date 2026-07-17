#include "iot_cloud_bridge/cloud_upload_node.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <openssl/hmac.h>      // 新增：用于 HMAC‑SHA256
#include <thread>

// ---- Base64 编码（保留备用，暂未使用） ----
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

// ---- HMAC‑SHA256 辅助函数（新增） ----
std::string hmac_sha256(const std::string& key, const std::string& data) {
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
//  连接华为云（合并 AT 指令 + 动态密码）
// ============================================================
bool CloudUploadNode::ConnectHuaweiCloud() {
    RCLCPP_INFO(get_logger(), "Connecting to Huawei Cloud...");

    // ---- 生成时间戳和 ClientID ----
    std::string timestamp = std::to_string(std::time(nullptr));
    std::string client_id = device_id_ + "_0_0_" + timestamp;   // 签名类型 0（不校验时间戳）
    std::string username = device_id_;

    // ---- 计算动态密码 ----
    std::string plain_key = password_;   // 配置文件中的明文密钥
    std::string dynamic_password = hmac_sha256(plain_key, timestamp);

    // ---- 合并指令（与串口调试助手完全一致） ----
    // 指令1：AT+MQTTUSER=1,"用户名","动态密码","ClientID"
    std::string cmd1 = "AT+MQTTUSER=1,\"" + username + "\",\"" + dynamic_password + "\",\"" + client_id + "\"";
    // 指令2：AT+MQTTOPEN=1,"服务器",1883,1,120
    std::string cmd2 = "AT+MQTTOPEN=1,\"" + broker_ + "\"," + port_ + ",1,120";

    // 发送 cmd1（重试3次）
    auto resp1 = l610_.SendATWithRetry(cmd1, 3000, 3, 500);
    if (resp1.empty() || resp1[0].find("OK") == std::string::npos) {
        RCLCPP_ERROR(get_logger(), "AT+MQTTUSER failed");
        return false;
    }

    // 发送 cmd2（重试3次，超时10秒）
    auto responses = l610_.SendATWithRetry(cmd2, 10000, 3, 1000);
    for (const auto& line : responses) {
        if (line.find("+MQTTOPEN: OK") != std::string::npos) {
            RCLCPP_INFO(get_logger(), "MQTT connection successful (timestamp=%s)", timestamp.c_str());
            mqtt_connected_.store(true);
            return true;
        }
    }

    RCLCPP_ERROR(get_logger(), "MQTT connection failed");
    mqtt_connected_.store(false);
    return false;
}

// ============================================================
//  保活检测
// ============================================================
void CloudUploadNode::CheckKeepAlive() {
    if (reconnecting_.load()) return;

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

    if (!mqtt_connected_.load()) {
        RCLCPP_WARN(get_logger(), "MQTT not connected, attempting reconnect...");
        ReconnectMQTT();
    }
}

// ============================================================
//  重连 MQTT（防抖）
// ============================================================
void CloudUploadNode::ReconnectMQTT() {
    bool expected = false;
    if (!reconnecting_.compare_exchange_strong(expected, true)) {
        RCLCPP_DEBUG(get_logger(), "Already reconnecting, skip");
        return;
    }
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
        mqtt_connected_.store(false);
        RCLCPP_ERROR(get_logger(), "MQTT reconnect failed, will retry later");
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
    if (line.find("+MQTTDISCONNECT") != std::string::npos) {
        RCLCPP_WARN(get_logger(), "MQTT disconnected by server or network");
        mqtt_connected_.store(false);
        std::thread([this]() { ReconnectMQTT(); }).detach();
        return;
    }

    if (line.find("+MQTTOPEN: OK") != std::string::npos) {
        mqtt_connected_.store(true);
        RCLCPP_INFO(get_logger(), "MQTT connection confirmed");
        return;
    }

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