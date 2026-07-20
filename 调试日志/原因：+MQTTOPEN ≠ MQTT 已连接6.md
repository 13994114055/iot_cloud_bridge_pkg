root@ubuntu:/userdata# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
      --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/
  config/
  l610_config.yaml
  [L610Serial] Opened /dev/ttyS1 at 115200 baud
  [INFO] [0946687544.275312747] [cloud_upload_node]: Serial port /dev/ttyS1 opened
  [INFO] [0946687544.275627758] [cloud_upload_node]: Handshaking with L610...
  [INFO] [0946687544.277525743] [cloud_upload_node]: Handshake successful.
  [INFO] [0946687544.277577412] [cloud_upload_node]: Connecting to Huawei Cloud...
  [INFO] [0946687544.282577925] [cloud_upload_node]: [CPIN] AT+CPIN?
  [INFO] [0946687544.282706222] [cloud_upload_node]: [CPIN] +CPIN: READY
  [INFO] [0946687544.282755307] [cloud_upload_node]: [CPIN] OK
  [INFO] [0946687544.289477300] [cloud_upload_node]: [CSQ] AT+CSQ
  [INFO] [0946687544.289628055] [cloud_upload_node]: [CSQ] +CSQ: 31,99
  [INFO] [0946687544.289677723] [cloud_upload_node]: [CSQ] OK
  [INFO] [0946687544.296341381] [cloud_upload_node]: [CREG] AT+CREG?
  [INFO] [0946687544.296497761] [cloud_upload_node]: [CREG] +CREG: 0,1
  [INFO] [0946687544.296545263] [cloud_upload_node]: [CREG] OK
  [INFO] [0946687544.304487300] [cloud_upload_node]: [COPS] AT+COPS?
  [INFO] [0946687544.304624180] [cloud_upload_node]: [COPS] +COPS: 0,0,"CHN-UNICOM",7
  [INFO] [0946687544.304673015] [cloud_upload_node]: [COPS] OK
  [INFO] [0946687544.304717808] [cloud_upload_node]: Configuring APN...
  [INFO] [0946687544.314474369] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
  [INFO] [0946687544.314629416] [cloud_upload_node]: [CGDCONT] OK
  [INFO] [0946687544.314689751] [cloud_upload_node]: Attaching GPRS...
  [INFO] [0946687544.320583589] [cloud_upload_node]: [CGATT] AT+CGATT=1
  [INFO] [0946687544.320712552] [cloud_upload_node]: [CGATT] OK
  [INFO] [0946687544.320771554] [cloud_upload_node]: Activating IP session (APN: UNINET)...
  [INFO] [0946687545.239351546] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
  [INFO] [0946687545.239525885] [cloud_upload_node]: [MIPCALL] OK
  [INFO] [0946687548.247207621] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
  [INFO] [0946687548.247452713] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.85.71.198"
  [INFO] [0946687548.247573884] [cloud_upload_node]: [CGPADDR] OK
  [INFO] [0946687548.304273014] [cloud_upload_node]: [MQTTUSER]
  AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","2b1f65ac83f5542fa5e91d7600c76b4dd749b6a41
  4ef0ccd0b466ab04691c1c0","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_946687548"
  [INFO] [0946687548.304620026] [cloud_upload_node]: [MQTTUSER] OK
  [INFO] [0946687548.304834659] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-
  north-4.myhuaweicloud.com:1883 (ssl=0)
  [INFO] [0946687548.416670113] [cloud_upload_node]: [MQTTOPEN_RAW]
  AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
  [INFO] [0946687548.417016084] [cloud_upload_node]: [MQTTOPEN_RAW] OK
  [INFO] [0946687551.248594004] [cloud_upload_node]: MQTT connection confirmed: +MQTTOPEN: 1,0
  [INFO] [0946687551.418940070] [cloud_upload_node]: MQTT connection successful
  [INFO] [0946687551.437098683] [cloud_upload_node]: CloudUploadNode initialized
  卡在这里是怎么回事，华为云iot控制台还是显示离线


• 不需要修改，这是代码逻辑问题，我解释原因。

  ## 原因：+MQTTOPEN ≠ MQTT 已连接

  看日志关键时序：

  [MQTTOPEN_RAW] OK                           ← 模块接受命令
  ... 3秒后 ...
  MQTT connection confirmed: +MQTTOPEN: 1,0   ← TCP 连接建立
  MQTT connection successful                   ← 代码认为连接成功，退出等待
  CloudUploadNode initialized                  ← 节点开始正常运转

  +MQTTOPEN: 1,0 只代表到服务器的 TCP 连接建立了，不代表 MQTT 的 CONNECT 认证包被服务器接受。

  对于华为云 IoT 平台，完整的流程是：

  AT+MQTTUSER → 配置凭据
  AT+MQTTOPEN → TCP 连接 + 模块自动发送 MQTT CONNECT
                ↓
                +MQTTOPEN: 1,0     ← TCP 通了
                +MQTTCONNECT: 1,0  ← MQTT CONNECT 被接受（设备上线）

  但代码里 HandleIncomingLine 只处理了 +MQTTOPEN: URC 就设置 mqtt_connected_ = true 退出等待，根本没等
  +MQTTCONNECT: UR C。所以：

  - TCP 连上了 → 代码认为成功 → 设备显示离线
  - MQTT CONNECT 可能根本没发出去，或者被服务器拒绝了，但代码已经不管了

  这就像你打电话给银行——电话通了（MQTTOPEN），但还没报身份证号验证身份（MQTTCONNECT），代码就以为办完
  事了挂机了。


› 那为什么一直卡在这个里[INFO] [0946687551.437098683] [cloud_upload_node]: CloudUploadNode
  initialized，已经发送了AT+MQTTuser，MQTTCONNECT和AT+MQTTuser是同一含义的指令吗


• 两个问题分开解释：

  ## 1. 为什么卡在 "CloudUploadNode initialized"

  没卡住，节点正常运行中。

  CloudUploadNode initialized 之后，节点进入 rclcpp::spin(node)——这是 ROS2 的事件循环，它在等订阅的
  Topic 消息：

  - /emotion/result（情绪结果）
  - /retinaface/detections（人脸检测框）
  - /asr/filtered_text（语音识别）
  - /tts_text（TTS 文本）


  ## 2. AT+MQTTUSER 和 +MQTTCONNECT 是同一回事吗？

  完全不是，两码事。

   命令            作用                                            类型
  ━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   AT+MQTTUSER     设置 MQTT 的用户名、密码、ClientID              主动指令，返回 OK
  ──────────────  ──────────────────────────────────────────────  ──────────────────────────────────
   +MQTTCONNECT    模块自动发送 MQTT CONNECT 报文并告知认证结果    异步 URC，格式 +MQTTCONNECT: 1,0

  ### 完整握手流程

  你发 → AT+MQTTUSER=1,"用户名","密码","ClientID"
  模块 → OK                     ← 凭据已存好

  你发 → AT+MQTTOPEN=1,"服务器",1883,0,120
  模块 → OK                     ← 命令已接受
  ...几秒后...
  模块 → +MQTTOPEN: 1,0         ← TCP 三次握手完成
  模块自动 → (发送 MQTT CONNECT 报文到服务器)
  ...
  模块 → +MQTTCONNECT: 1,0      ← ✅ MQTT 认证通过，设备上线

  现在代码在收到 +MQTTOPEN: 1,0（TCP 通了）就把 mqtt_connected_ 设成 true 退出了，没等 +MQTTCONNECT:。
  所以华为云那边只看到 TCP 连上来但没有 MQTT CONNECT，所以显示离线。


› 意思是现在可以另起一个终端给该节点发送消息吗，发送的消息会上传到云端吗


• 可以另起终端发消息，但消息传不到云端。

  ### 能发吗？✅ 能

  另起终端：

  source /userdata/emotion_ros2_ws/install_arm64/setup.bash

  # 发情绪结果
  ros2 topic pub /emotion/result emotion_msgs/msg/EmotionResult \
    "{header: {stamp: {sec: 0, nanosec: 0}, frame_id: ''}, frame_uid: 1, emotion_label: 3, confidence:
    0.95}" -1

  # 发聊天文本
  ros2 topic pub /asr/filtered_text std_msgs/msg/String \
    "{data: '今天天气怎么样'}" -1

  你会看到节点日志里打出 SendChatData 或 SendReportData 的调试信息。

  ### 能上传云端吗？❌ 不能

  原因是上次说的：代码只等了 +MQTTOPEN: 1,0（TCP 通了），没等 +MQTTCONNECT:（MQTT 认证通过）。所以
  PublishMQTT 调 AT+MQTTPUB 时，MQTT 会话根本没建立，模块会返回 ERROR，日志里你会看到：

  [WARN] Failed to publish to /v1/.../user/chat

  ### 怎么确认？

  发一条消息后，看节点日志有没有这行——有就说明 MQTT 会话没通，数据没出去。需要我在代码里加上等
  +MQTTCONNECT: URC 的逻辑，才能真正连上云端。





root@ubuntu:/userdata# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [0946688759.237104882] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [0946688759.237437894] [cloud_upload_node]: Handshaking with L610...
[INFO] [0946688759.239958027] [cloud_upload_node]: Handshake successful.
[INFO] [0946688759.240028821] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [0946688759.244771534] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [0946688759.244898330] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [0946688759.244947790] [cloud_upload_node]: [CPIN] OK
[INFO] [0946688759.251582446] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [0946688759.251655949] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [0946688759.251701159] [cloud_upload_node]: [CSQ] OK
[INFO] [0946688759.258310689] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [0946688759.258475612] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [0946688759.258547531] [cloud_upload_node]: [CREG] OK
[INFO] [0946688759.265840336] [cloud_upload_node]: [COPS] AT+COPS?
[INFO] [0946688759.266007509] [cloud_upload_node]: [COPS] +COPS: 0,0,"CHN-UNICOM",7
[INFO] [0946688759.266092178] [cloud_upload_node]: [COPS] OK
[INFO] [0946688759.266152639] [cloud_upload_node]: Configuring APN...
[INFO] [0946688759.276558723] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [0946688759.276718145] [cloud_upload_node]: [CGDCONT] OK
[INFO] [0946688759.276788731] [cloud_upload_node]: Attaching GPRS...
[INFO] [0946688759.282721612] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [0946688759.282898827] [cloud_upload_node]: [CGATT] OK
[INFO] [0946688759.282971954] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[L610Serial] Retry 1/3 for: AT+MIPCALL=1,"UNINET"
[L610Serial] Retry 2/3 for: AT+MIPCALL=1,"UNINET"
[L610Serial] All 3 retries failed for: AT+MIPCALL=1,"UNINET"
[INFO] [0946688765.301580792] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [0946688765.301818926] [cloud_upload_node]: [MIPCALL] ERROR
[ERROR] [0946688765.301929680] [cloud_upload_node]: AT+MIPCALL failed (APN: UNINET)
[ERROR] [0946688765.302128271] [cloud_upload_node]: Failed to connect to Huawei Cloud
[ERROR] [0946688765.413054566] [cloud_upload_node]: Exception: MQTT connect failed
[ros2run]: Process exited with failure 1



root@ubuntu:/userdata# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [0946690417.764103214] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [0946690417.764441518] [cloud_upload_node]: Handshaking with L610...
[INFO] [0946690417.766381922] [cloud_upload_node]: Handshake successful.
[INFO] [0946690417.766449424] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [0946690417.771442354] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [0946690417.771554900] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [0946690417.771617069] [cloud_upload_node]: [CPIN] OK
[INFO] [0946690417.776408700] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [0946690417.776482703] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [0946690417.776537413] [cloud_upload_node]: [CSQ] OK
[INFO] [0946690417.783177736] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [0946690417.783290907] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [0946690417.783349242] [cloud_upload_node]: [CREG] OK
[INFO] [0946690417.791309280] [cloud_upload_node]: [COPS] AT+COPS?
[INFO] [0946690417.791429951] [cloud_upload_node]: [COPS] +COPS: 0,0,"CHN-UNICOM",7
[INFO] [0946690417.791495661] [cloud_upload_node]: [COPS] OK
[INFO] [0946690417.791547497] [cloud_upload_node]: Configuring APN...
[INFO] [0946690417.800919877] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [0946690417.801066340] [cloud_upload_node]: [CGDCONT] OK
[INFO] [0946690417.801124467] [cloud_upload_node]: Attaching GPRS...
[INFO] [0946690417.807071890] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [0946690417.807261939] [cloud_upload_node]: [CGATT] OK
[INFO] [0946690417.807337650] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [0946690417.816253597] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [0946690417.816404436] [cloud_upload_node]: [MIPCALL] OK
[INFO] [0946690420.825057706] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [0946690420.825252255] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.98.90.240"
[INFO] [0946690420.825318090] [cloud_upload_node]: [CGPADDR] OK
[INFO] [0946690420.871035907] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","880091f671ee6f5182d7113401b0ad4c4d25d4e0755c1e1626448e3bfe014811","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_946690420"
[INFO] [0946690420.871264832] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [0946690420.871382295] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [0946690421.046499741] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [0946690421.046773959] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [0946690425.602491616] [cloud_upload_node]: TCP connection established: +MQTTOPEN: 1,0
[WARN] [0946690426.049224367] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946690426.249596683] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946690426.449961916] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946690426.650270271] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946690426.850620795] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946690431.052888110] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946690431.253249009] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946690431.453562157] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946690431.653844261] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946690431.854175368] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946690436.056396264] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946690436.256739704] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946690436.457038726] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946690436.657307914] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946690436.857613061] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946690441.059739162] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946690441.260032934] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946690441.460349457] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946690441.660625769] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946690441.860953251] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946690446.063086561] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946690446.263387041] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946690446.463696355] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946690446.664002627] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946690446.864342775] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946690451.066329372] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946690451.266623394] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946690451.466958708] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946690451.667279106] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946690451.867611504] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[ERROR] [0946690456.069748523] [cloud_upload_node]: MQTT connection failed (no +MQTTCONNECT URC within 35s)
[ERROR] [0946690456.070005449] [cloud_upload_node]: Failed to connect to Huawei Cloud
[ERROR] [0946690456.161286202] [cloud_upload_node]: Exception: MQTT connect failed
[ros2run]: Process exited with failure 1



root@ubuntu:/userdata# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [0946691715.082951998] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [0946691715.083311636] [cloud_upload_node]: Handshaking with L610...
[INFO] [0946691715.085224871] [cloud_upload_node]: Handshake successful.
[INFO] [0946691715.085285290] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [0946691715.090339723] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [0946691715.090484436] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [0946691715.090536772] [cloud_upload_node]: [CPIN] OK
[INFO] [0946691715.095368321] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [0946691715.095517285] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [0946691715.095568745] [cloud_upload_node]: [CSQ] OK
[INFO] [0946691715.100381419] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [0946691715.100542758] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [0946691715.100598468] [cloud_upload_node]: [CREG] OK
[INFO] [0946691715.100645345] [cloud_upload_node]: Configuring APN...
[INFO] [0946691715.113874072] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [0946691715.114004410] [cloud_upload_node]: [CGDCONT] OK
[INFO] [0946691715.114057371] [cloud_upload_node]: Attaching GPRS...
[INFO] [0946691715.120138715] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [0946691715.120278720] [cloud_upload_node]: [CGATT] OK
[INFO] [0946691715.120331472] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [0946691715.127989373] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [0946691715.128180422] [cloud_upload_node]: [MIPCALL] OK
[INFO] [0946691718.136937321] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [0946691718.137169746] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.137.130.112"
[INFO] [0946691718.137257875] [cloud_upload_node]: [CGPADDR] OK
[INFO] [0946691718.176790385] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","00bb53d88eaef92d0f71fdb9eef49594fe13653a437e5bd001124d6f7e135bec","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_946691718"
[INFO] [0946691718.176958266] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [0946691718.177021310] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [0946691718.378667380] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [0946691718.378847179] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [0946691722.950884091] [cloud_upload_node]: TCP connection established: +MQTTOPEN: 1,0
[INFO] [0946691722.981142558] [cloud_upload_node]: TCP ready, sending AT+MQTTCONN...
[INFO] [0946691722.986174782] [cloud_upload_node]: [MQTTCONN] AT+MQTTCONN=1
[INFO] [0946691722.986326329] [cloud_upload_node]: [MQTTCONN] ERROR
[WARN] [0946691723.386637904] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946691723.587010720] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946691723.787375369] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946691723.987646932] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946691724.188007498] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 5s/35s
[WARN] [0946691728.390370233] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946691728.590754591] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946691728.791086864] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946691728.991367302] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946691729.191721951] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 10s/35s
[WARN] [0946691733.393957931] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946691733.594244661] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946691733.794547017] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946691733.994802329] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946691734.195127976] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 15s/35s
[WARN] [0946691738.397336706] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946691738.597699272] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946691738.798009877] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946691738.998280065] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946691739.198617547] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 20s/35s
[WARN] [0946691743.400827609] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946691743.601124881] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946691743.801437237] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946691744.001771677] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946691744.202115117] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 25s/35s
[WARN] [0946691748.404283303] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946691748.604653494] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946691748.804998101] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946691749.005334166] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[WARN] [0946691749.205704274] [cloud_upload_node]: Waiting for +MQTTCONNECT URC... 30s/35s
[ERROR] [0946691753.407873585] [cloud_upload_node]: MQTT connection failed (no +MQTTCONNECT URC within 35s)
[ERROR] [0946691753.408232140] [cloud_upload_node]: Failed to connect to Huawei Cloud
[ERROR] [0946691753.446765781] [cloud_upload_node]: Exception: MQTT connect failed
[ros2run]: Process exited with failure 1





发送串口的测试日志

root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [0946694303.939922980] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [0946694303.940243367] [cloud_upload_node]: Handshaking with L610...
[INFO] [0946694303.942140310] [cloud_upload_node]: Handshake successful.
[INFO] [0946694303.942192895] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [0946694303.947206576] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [0946694303.947277079] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [0946694303.947325997] [cloud_upload_node]: [CPIN] OK
[INFO] [0946694303.952155172] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [0946694303.952217882] [cloud_upload_node]: [CSQ] +CSQ: 0,0
[INFO] [0946694303.952263634] [cloud_upload_node]: [CSQ] OK
[INFO] [0946694303.957211604] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [0946694303.957342567] [cloud_upload_node]: [CREG] +CREG: 0,0
[INFO] [0946694303.957414237] [cloud_upload_node]: [CREG] OK
[INFO] [0946694303.957469697] [cloud_upload_node]: Configuring APN...
[INFO] [0946694303.966485689] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [0946694303.966615402] [cloud_upload_node]: [CGDCONT] OK
[INFO] [0946694303.966678946] [cloud_upload_node]: Attaching GPRS...
[INFO] [0946694304.484419052] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [0946694304.484668019] [cloud_upload_node]: [CGATT] OK
[INFO] [0946694304.484777190] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [0946694305.383378876] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [0946694305.383686596] [cloud_upload_node]: [MIPCALL] OK
[INFO] [0946694308.391429250] [cloud_upload_node]: [CGPADDR] �����AT+CGPADDR=1
[INFO] [0946694308.391620382] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.72.126.184"
[INFO] [0946694308.391704719] [cloud_upload_node]: [CGPADDR] OK
[INFO] [0946694308.431319732] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","e0a56bfeb9f9b0822fe4ca74fdf59842cc35056e94e577a1ebb39931fe1b56d7","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_946694308"
[INFO] [0946694308.431492030] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [0946694308.431584033] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [0946694308.539447135] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [0946694308.539679310] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [0946694309.950460152] [cloud_upload_node]: MQTT connection confirmed: +MQTTOPEN: 1,0
[INFO] [0946694310.140613808] [cloud_upload_node]: MQTT connection successful
[INFO] [0946694310.151500742] [cloud_upload_node]: CloudUploadNode initialized
[WARN] [0946694462.159146454] [cloud_upload_node]: L610 not responding, will attempt reconnect
[INFO] [0946694462.159432506] [cloud_upload_node]: MQTT seems connected, skip reconnect
[WARN] [0946694472.159371142] [cloud_upload_node]: L610 not responding, will attempt reconnect
[INFO] [0946694472.159585275] [cloud_upload_node]: MQTT seems connected, skip reconnect
[WARN] [0946694482.159769920] [cloud_upload_node]: L610 not responding, will attempt reconnect
[INFO] [0946694482.160040055] [cloud_upload_node]: MQTT seems connected, skip reconnect
[WARN] [0946694492.160203699] [cloud_upload_node]: L610 not responding, will attempt reconnect
[INFO] [0946694492.160465333] [cloud_upload_node]: MQTT seems connected, skip reconnect
^C[INFO] [0946694499.994418783] [rclcpp]: signal_handler(SIGINT/SIGTERM)

root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# stty -F /dev/ttyS1 raw speed 115200 -echo
115200
root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# echo -e "AT+MQTTPUB=1,\"/test\",\"{}\",0,0\r\n" > /dev/ttyS1
root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# cat /dev/ttyS1
AT
OK
AT
OK
AT
OK
AT
OK
^C





root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [1784478706.201070654] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [1784478706.201371737] [cloud_upload_node]: Handshaking with L610...
[INFO] [1784478706.203300068] [cloud_upload_node]: Handshake successful.
[INFO] [1784478706.203361527] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [1784478707.001310238] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [1784478707.001565030] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [1784478707.001675113] [cloud_upload_node]: [CPIN] OK
[INFO] [1784478707.007054649] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [1784478707.007169233] [cloud_upload_node]: [CSQ] +CSQ: 0,0
[INFO] [1784478707.007299566] [cloud_upload_node]: [CSQ] OK
[INFO] [1784478707.012124311] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [1784478707.012206811] [cloud_upload_node]: [CREG] +CREG: 0,0
[INFO] [1784478707.012253853] [cloud_upload_node]: [CREG] OK
[INFO] [1784478707.012299520] [cloud_upload_node]: Configuring APN...
[INFO] [1784478707.026964672] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [1784478707.027135464] [cloud_upload_node]: [CGDCONT] OK
[INFO] [1784478707.027240089] [cloud_upload_node]: Attaching GPRS...
[INFO] [1784478709.522798006] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [1784478709.522991131] [cloud_upload_node]: [CGATT] +SIM READY
[INFO] [1784478709.523055422] [cloud_upload_node]: [CGATT] OK
[INFO] [1784478709.523113589] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [1784478709.533102746] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [1784478709.533210163] [cloud_upload_node]: [MIPCALL] OK
[INFO] [1784478712.540700857] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [1784478712.540887690] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.18.248.5"
[INFO] [1784478712.540943773] [cloud_upload_node]: [CGPADDR] OK
[INFO] [1784478712.576753740] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","036Ub+5TkzcPBW40Z7dpdLmwF6XSEGBTeULW00kCiFk=","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_2026071916"
[INFO] [1784478712.576977490] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [1784478712.577140864] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [1784478712.666327448] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [1784478712.666524781] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [1784478716.360482551] [cloud_upload_node]: MQTT connection confirmed: +MQTTOPEN: 1,0
[INFO] [1784478716.468496742] [cloud_upload_node]: MQTT connection successful
[L610Serial] Retry 1/2 for: AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/commands/#",1
[L610Serial] All 2 retries failed for: AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/commands/#",1
[INFO] [1784478717.503063156] [cloud_upload_node]: [MQTTSUB] AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/commands/#",1
[INFO] [1784478717.503294197] [cloud_upload_node]: [MQTTSUB] ERROR
[L610Serial] Retry 1/2 for: AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/messages/down",1
[L610Serial] All 2 retries failed for: AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/messages/down",1
[INFO] [1784478718.538296945] [cloud_upload_node]: [MQTTSUB_MSG] AT+MQTTSUB=1,"$oc/devices/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/sys/messages/down",1
[INFO] [1784478718.538534069] [cloud_upload_node]: [MQTTSUB_MSG] ERROR
[INFO] [1784478718.549725392] [cloud_upload_node]: CloudUploadNode initialized
[L610Serial] Retry 1/3 for: AT+MQTTPUB=1,"/v1/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/user/report",1,0,"{\"timestamp\":1784478748,\"type\":\"heartbeat\"}"
[L610Serial] Retry 2/3 for: AT+MQTTPUB=1,"/v1/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/user/report",1,0,"{\"timestamp\":1784478748,\"type\":\"heartbeat\"}"
[L610Serial] All 3 retries failed for: AT+MQTTPUB=1,"/v1/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/user/report",1,0,"{\"timestamp\":1784478748,\"type\":\"heartbeat\"}"
[WARN] [1784478749.627026186] [cloud_upload_node]: Failed to publish to /v1/6a4b1560cbb0cf6bb96c615f_agent_2026_7_10/user/report (payload len=43)
^C[INFO] [1784478760.238688415] [rclcpp]: signal_handler(SIGINT/SIGTERM)

root@ubuntu:/userdata/sherpa-test/sherpa-onnx-kws# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [1784479706.477487676] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [1784479706.477835634] [cloud_upload_node]: Handshaking with L610...
[INFO] [1784479706.479823925] [cloud_upload_node]: Handshake successful.
[INFO] [1784479706.479933550] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [1784479706.484988464] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [1784479706.485167881] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [1784479706.485232381] [cloud_upload_node]: [CPIN] OK
[INFO] [1784479706.490323837] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [1784479706.490490003] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [1784479706.490573712] [cloud_upload_node]: [CSQ] OK
[INFO] [1784479706.495613168] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [1784479706.495769543] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [1784479706.495819876] [cloud_upload_node]: [CREG] OK
[INFO] [1784479706.495865459] [cloud_upload_node]: Configuring APN...
[INFO] [1784479706.506818663] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [1784479706.506984246] [cloud_upload_node]: [CGDCONT] OK
[INFO] [1784479706.507036830] [cloud_upload_node]: Attaching GPRS...
[INFO] [1784479706.511654453] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [1784479706.511777494] [cloud_upload_node]: [CGATT] OK
[INFO] [1784479706.511829786] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [1784479707.371702211] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [1784479707.371893420] [cloud_upload_node]: [MIPCALL] OK
[INFO] [1784479710.379422350] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [1784479710.379609766] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.99.46.109"
[INFO] [1784479710.379669641] [cloud_upload_node]: [CGPADDR] OK
[INFO] [1784479710.415548376] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","gGkzUizMoZe1WompB+8B/y1vYhfK8mUbBBw/UnxvEuQ=","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_2026071916"
[INFO] [1784479710.415710709] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [1784479710.415801251] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [1784479710.526814871] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [1784479710.526979287] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [1784479714.658813246] [cloud_upload_node]: MQTT connection confirmed: +MQTTOPEN: 1,0
[INFO] [1784479714.728745049] [cloud_upload_node]: MQTT connection successful
[L610Serial] Retry 1/3 for: AT+MQTTCONN=1
[L610Serial] Retry 2/3 for: AT+MQTTCONN=1
[L610Serial] All 3 retries failed for: AT+MQTTCONN=1
[INFO] [1784479717.243461988] [cloud_upload_node]: [MQTTCONN] AT+MQTTCONN=1
[INFO] [1784479717.243697071] [cloud_upload_node]: [MQTTCONN] ERROR
[ERROR] [1784479717.243808446] [cloud_upload_node]: AT+MQTTCONN failed, MQTT not connected
[ERROR] [1784479717.243999946] [cloud_upload_node]: Failed to connect to Huawei Cloud
[ERROR] [1784479717.358389731] [cloud_upload_node]: Exception: MQTT connect failed
[ros2run]: Process exited with failure 1



root@ubuntu:/userdata/emotion_ros2_ws# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [1784486870.419428718] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [1784486870.419757467] [cloud_upload_node]: Handshaking with L610...
[INFO] [1784486870.421674458] [cloud_upload_node]: Handshake successful.
[INFO] [1784486870.421739707] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [1784486870.426741641] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [1784486870.426832391] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [1784486870.426878432] [cloud_upload_node]: [CPIN] OK
[INFO] [1784486870.433511108] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [1784486870.433590191] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [1784486870.433636400] [cloud_upload_node]: [CSQ] OK
[INFO] [1784486870.440269909] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [1784486870.440375784] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [1784486870.440422617] [cloud_upload_node]: [CREG] OK
[INFO] [1784486870.440468241] [cloud_upload_node]: Configuring APN...
[INFO] [1784486870.449864571] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [1784486870.450027070] [cloud_upload_node]: [CGDCONT] OK
[INFO] [1784486870.450078653] [cloud_upload_node]: Attaching GPRS...
[INFO] [1784486870.455950041] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [1784486870.456059416] [cloud_upload_node]: [CGATT] OK
[INFO] [1784486870.456107832] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [1784486870.465113372] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [1784486870.465253163] [cloud_upload_node]: [MIPCALL] OK
[INFO] [1784486873.473956008] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [1784486873.474133007] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.101.218.186"
[INFO] [1784486873.474186840] [cloud_upload_node]: [CGPADDR] OK
[INFO] [1784486873.507651512] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","c5ff824bf42bcc673e3e55dcaa186ecc546f81b37cffa0b04c2c5e8941cbc00e","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_2026071918"
[INFO] [1784486873.507783719] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [1784486873.507841302] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [1784486873.661917723] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [1784486873.662062722] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[WARN] [1784486873.721283602] [cloud_upload_node]: MQTTOPEN returned non-success: +MQTTOPEN: 1,1
[WARN] [1784486878.664060896] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784486878.864393802] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784486879.064670167] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784486879.264958699] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784486879.465226981] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784486883.667088093] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784486883.867374376] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784486884.067643409] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784486884.267933816] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784486884.468191558] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784486888.670034986] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784486888.870314062] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784486889.070568929] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784486889.270844921] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784486889.471097664] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784486893.672923450] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784486893.873200484] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784486894.073463395] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784486894.273743138] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784486894.474002215] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784486898.675864233] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784486898.876130019] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784486899.076378722] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784486899.276653592] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784486899.476904711] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784486903.678769753] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784486903.879041082] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784486904.079294994] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784486904.279560282] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784486904.479809028] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[ERROR] [1784486908.681593636] [cloud_upload_node]:MQTT connection failed (no +MQTTOPEN URC within 35s)
[ERROR] [1784486908.681867176] [cloud_upload_node]:Failed to connect to Huawei Cloud
[ERROR] [1784486908.778880293] [cloud_upload_node]:Exception: MQTT connect failed
[ros2run]: Process exited with failure 1
