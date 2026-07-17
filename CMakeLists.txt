cmake_minimum_required(VERSION 3.8)
project(iot_cloud_bridge_cpp)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(emotion_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)   # 用于 HMAC-SHA256 动态密码计算

include_directories(include)

# 串口工具库
add_library(serial_utils src/serial_utils.cpp)
target_include_directories(serial_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(serial_utils)

# 主节点
add_executable(cloud_upload_node src/cloud_upload_node.cpp)
target_include_directories(cloud_upload_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(cloud_upload_node
  rclcpp
  std_msgs
  sensor_msgs
  emotion_msgs)
target_link_libraries(cloud_upload_node
  serial_utils
  nlohmann_json::nlohmann_json
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

install(TARGETS serial_utils cloud_upload_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME})

install(FILES
  package.xml
  DESTINATION share/${PROJECT_NAME})

ament_package()