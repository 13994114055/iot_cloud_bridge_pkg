root@ubuntu:/userdata/emotion_ros2_ws/install_arm64# ros2 run iot_cloud_bridge_pkg cloud_upload_node \
    --ros-args --params-file /userdata/emotion_ros2_ws/install_arm64/share/iot_cloud_bridge_pkg/config/l610_config.yaml
[L610Serial] Opened /dev/ttyS1 at 115200 baud
[INFO] [1784530294.169702399] [cloud_upload_node]: Serial port /dev/ttyS1 opened
[INFO] [1784530294.170009968] [cloud_upload_node]: Handshaking with L610...
[INFO] [1784530294.171929877] [cloud_upload_node]: Handshake successful.
[INFO] [1784530294.171990249] [cloud_upload_node]: Connecting to Huawei Cloud...
[INFO] [1784530294.177027803] [cloud_upload_node]: [CPIN] AT+CPIN?
[INFO] [1784530294.177100967] [cloud_upload_node]: [CPIN] +CPIN: READY
[INFO] [1784530294.177146298] [cloud_upload_node]: [CPIN] OK
[INFO] [1784530294.182078440] [cloud_upload_node]: [CSQ] AT+CSQ
[INFO] [1784530294.182147187] [cloud_upload_node]: [CSQ] +CSQ: 31,99
[INFO] [1784530294.182222517] [cloud_upload_node]: [CSQ] OK
[INFO] [1784530294.187235114] [cloud_upload_node]: [CREG] AT+CREG?
[INFO] [1784530294.187366774] [cloud_upload_node]: [CREG] +CREG: 0,1
[INFO] [1784530294.187417564] [cloud_upload_node]: [CREG] OK
[INFO] [1784530294.187462811] [cloud_upload_node]: Configuring APN...
[INFO] [1784530294.196399723] [cloud_upload_node]: [CGDCONT] AT+CGDCONT=1,"IP","UNINET"
[INFO] [1784530294.196531092] [cloud_upload_node]: [CGDCONT] OK
[INFO] [1784530294.196581673] [cloud_upload_node]: Attaching GPRS...
[INFO] [1784530294.201017464] [cloud_upload_node]: [CGATT] AT+CGATT=1
[INFO] [1784530294.201082669] [cloud_upload_node]: [CGATT] OK
[INFO] [1784530294.201129917] [cloud_upload_node]: Activating IP session (APN: UNINET)...
[INFO] [1784530294.208386116] [cloud_upload_node]: [MIPCALL] AT+MIPCALL=1,"UNINET"
[INFO] [1784530294.208532443] [cloud_upload_node]: [MIPCALL] OK
[INFO] [1784530297.215976578] [cloud_upload_node]: [CGPADDR] AT+CGPADDR=1
[INFO] [1784530297.216159878] [cloud_upload_node]: [CGPADDR] +CGPADDR: 1,"10.32.97.58"
[INFO] [1784530297.216224869] [cloud_upload_node]: [CGPADDR] OK
[INFO] [1784530297.222337840] [cloud_upload_node]: [TIME] AT+CCLK?
[INFO] [1784530297.222571456] [cloud_upload_node]: [TIME] +CCLK: "26/07/20,06:51:36+00"
[INFO] [1784530296.000098715] [cloud_upload_node]: [TIME] synced to UTC: 2026-07-20 06:51:36
[INFO] [1784530296.000375437] [cloud_upload_node]: [TIME] OK
[INFO] [1784530296.014022672] [cloud_upload_node]: [AUTH] timestamp=2026072006 password=d67be58f7545cb7ba9d123dba658d803b55900b5157fcee90e578c00eb7e62b2
[INFO] [1784530296.047626528] [cloud_upload_node]: [MQTTUSER] AT+MQTTUSER=1,"6a4b1560cbb0cf6bb96c615f_agent_2026_7_10","d67be58f7545cb7ba9d123dba658d803b55900b5157fcee90e578c00eb7e62b2","6a4b1560cbb0cf6bb96c615f_agent_2026_7_10_0_0_2026072006"
[INFO] [1784530296.047838403] [cloud_upload_node]: [MQTTUSER] OK
[INFO] [1784530298.048181084] [cloud_upload_node]: Opening MQTT to 64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883 (ssl=0)
[INFO] [1784530298.139008175] [cloud_upload_node]: [MQTTOPEN_RAW] AT+MQTTOPEN=1,"64410107ee.st1.iotda-device.cn-north-4.myhuaweicloud.com",1883,0,120
[INFO] [1784530298.139327663] [cloud_upload_node]: [MQTTOPEN_RAW] OK
[INFO] [1784530302.408874008] [cloud_upload_node]: TCP connected, waiting for MQTT... +MQTTOPEN: 1,0
[WARN] [1784530303.143232088] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784530303.343748579] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784530303.544228471] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784530303.744717784] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784530303.945220560] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 5s/35s
[WARN] [1784530308.148934195] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784530308.349481349] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784530308.549959063] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784530308.750451031] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784530308.950954461] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 10s/35s
[WARN] [1784530313.154366176] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784530313.354880474] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784530313.555346341] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784530313.755986427] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784530313.956464131] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 15s/35s
[WARN] [1784530318.159891068] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784530318.360240806] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784530318.560727287] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784530318.761158837] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784530318.961553962] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 20s/35s
[WARN] [1784530323.164966754] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784530323.365490662] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784530323.566048786] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784530323.766409992] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784530323.966886807] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 25s/35s
[WARN] [1784530328.169995981] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784530328.370536103] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784530328.570965288] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784530328.771285994] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[WARN] [1784530328.971766772] [cloud_upload_node]: Waiting for +MQTTOPEN URC... 30s/35s
[ERROR] [1784530333.174792709] [cloud_upload_node]: MQTT connection failed (no +MQTTOPEN URC within 35s)
[ERROR] [1784530333.175341959] [cloud_upload_node]: Failed to connect to Huawei Cloud
[ERROR] [1784530333.287667234] [cloud_upload_node]: Exception: MQTT connect failed
[ros2run]: Process exited with failure 1
